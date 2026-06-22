/*
 * Chipmunk Ring V2 — non-interactive lattice ring signature.
 *
 * Based on LoTRS binary ring proof (RS) with Fiat-Shamir.
 * Single-signer, non-interactive, O(N) signature size.
 */

#include "chipmunk_ring.h"
#include "lotrs_sample.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "chipmunk_ring"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"

/* --- Cleanup --- */

void chipmunk_ring_keypair_free(chipmunk_ring_keypair_t *a_kp)
{
    if (a_kp) {
        lotrs_polyvec_free(&a_kp->pk.a_hat);
        lotrs_polyvec_free(&a_kp->sk.s);
        memset(a_kp, 0, sizeof(*a_kp));
    }
}

void chipmunk_ring_table_free(chipmunk_ring_table_t *a_ring)
{
    if (a_ring && a_ring->pks) {
        for (uint32_t i = 0u; i < a_ring->N; ++i) {
            lotrs_polyvec_free(&a_ring->pks[i].a_hat);
        }
        DAP_DELETE(a_ring->pks);
        a_ring->pks = NULL;
        a_ring->N = 0;
    }
}

void chipmunk_ring_sig_free(chipmunk_ring_sig_t *a_sig)
{
    if (a_sig && a_sig->data) {
        dap_memwipe(a_sig->data, a_sig->len);
        DAP_DELETE(a_sig->data);
        a_sig->data = NULL;
        a_sig->len = 0;
    }
}

size_t chipmunk_ring_sig_bytes(const lotrs_params_t *a_par, uint32_t a_N)
{
    /* Header + N * T (k polys each) + N * c (1 poly each) + N * z (l+k polys each). */
    size_t l_poly = lotrs_poly_bytes(a_par);
    return CHIPMUNK_RING_HEADER_BYTES
         + (size_t)a_N * (size_t)a_par->k * l_poly   /* T_i */
         + (size_t)a_N * l_poly                       /* c_i */
         + (size_t)a_N * ((size_t)a_par->l + a_par->k) * l_poly; /* z_i */
}

/* --- Keygen --- */

int chipmunk_ring_keygen(chipmunk_ring_keypair_t *a_kp,
                            const lotrs_params_t *a_par,
                            const uint8_t a_seed[32])
{
    if (!a_kp || !a_par || !a_seed) return -EINVAL;

    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "crv2-keygen-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    const uint32_t l_len = a_par->l + a_par->k;
    a_kp->sk.s = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_kp->sk.s.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    int l_rc = lotrs_sample_short_vec(&a_kp->sk.s, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { lotrs_xof_free(l_xof); return -ENOMEM; }

    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); lotrs_xof_free(l_xof); return -ENOMEM; }

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_xof_free(l_xof); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* pk = A * s_short + s_tail, canonicalized. */
    a_kp->pk.a_hat = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!a_kp->pk.a_hat.polys) {
        lotrs_polymat_free(&l_A); lotrs_xof_free(l_xof); return -ENOMEM;
    }

    lotrs_polyvec_t l_s_short = { .polys = a_kp->sk.s.polys, .n = a_par->l };
    lotrs_polyvec_t l_s_tail  = { .polys = a_kp->sk.s.polys + a_par->l, .n = a_par->k };

    lotrs_polymat_vecmul(&a_kp->pk.a_hat, &l_A, &l_s_short, a_par);
    lotrs_polyvec_add(&a_kp->pk.a_hat, &a_kp->pk.a_hat, &l_s_tail, a_par);

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            a_kp->pk.a_hat.polys[i]->coeffs[j] %= a_par->q;
        }
    }

    lotrs_polymat_free(&l_A);
    lotrs_xof_free(l_xof);
    return 0;
}

/* --- Sign --- */

/* Max rejection sampling retries before giving up. */
#define CHIPMUNK_RING_NONINT_MAX_RETRIES 64

int chipmunk_ring_sign(chipmunk_ring_sig_t *a_sig,
                          const lotrs_params_t *a_par,
                          const chipmunk_ring_table_t *a_ring,
                          const chipmunk_ring_sk_t *a_sk,
                          uint32_t a_signer_idx,
                          const uint8_t *a_msg, size_t a_msg_len,
                          const uint8_t a_seed[32])
{
    if (!a_sig || !a_par || !a_ring || !a_sk || !a_msg || !a_seed) return -EINVAL;
    if (a_signer_idx >= a_ring->N) return -EINVAL;

    int l_rc;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;
    const uint32_t l_N = a_ring->N;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Generate A matrix (shared across retries). */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) return -ENOMEM;
    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* Allocate per-member arrays (shared across retries). */
    lotrs_polyvec_t *l_T = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    lotrs_poly_t **l_c_arr = DAP_NEW_Z_COUNT(lotrs_poly_t *, l_N);
    lotrs_polyvec_t *l_z = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    if (!l_T || !l_c_arr || !l_z) {
        DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
        lotrs_polymat_free(&l_A); return -ENOMEM;
    }

    for (uint32_t i = 0u; i < l_N; ++i) {
        l_T[i] = lotrs_polyvec_alloc(a_par, a_par->k);
        l_c_arr[i] = lotrs_poly_alloc(a_par);
        l_z[i] = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_T[i].polys || !l_c_arr[i] || !l_z[i].polys) {
            for (uint32_t j = 0u; j <= i; ++j) {
                lotrs_polyvec_free(&l_T[j]); lotrs_poly_free(l_c_arr[j]); lotrs_polyvec_free(&l_z[j]);
            }
            DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
            lotrs_polymat_free(&l_A); return -ENOMEM;
        }
    }

    /* Simulate non-signer branches: random c_i, z_i, compute T_i.
     * These are deterministic from a_seed and don't change across retries. */
    for (uint32_t i = 0u; i < l_N; ++i) {
        if (i == a_signer_idx) continue;
        lotrs_xof_t *l_xof_sim = lotrs_xof_new(a_seed, 32u);
        lotrs_xof_absorb(l_xof_sim, (const uint8_t *)"crv2-sim", 8u);
        uint8_t l_idx_buf[4];
        l_idx_buf[0] = (uint8_t)i; l_idx_buf[1] = 0; l_idx_buf[2] = 0; l_idx_buf[3] = 0;
        lotrs_xof_absorb(l_xof_sim, l_idx_buf, 4u);

        lotrs_sample_ternary(l_c_arr[i], l_xof_sim, a_par, a_par->w);

        lotrs_polyvec_t l_z_short_i = { .polys = l_z[i].polys, .n = a_par->l };
        lotrs_polyvec_t l_z_tail_i  = { .polys = l_z[i].polys + a_par->l, .n = a_par->k };
        lotrs_sample_short_vec(&l_z[i], l_xof_sim, a_par, a_par->eta);

        lotrs_polymat_vecmul(&l_T[i], &l_A, &l_z_short_i, a_par);
        lotrs_polyvec_add(&l_T[i], &l_T[i], &l_z_tail_i, a_par);

        for (uint32_t j = 0u; j < a_par->k; ++j) {
            lotrs_poly_t *l_cp = lotrs_poly_alloc(a_par);
            lotrs_poly_mul(l_cp, l_c_arr[i], a_ring->pks[i].a_hat.polys[j], a_par);
            lotrs_poly_sub(l_T[i].polys[j], l_T[i].polys[j], l_cp, a_par);
            lotrs_poly_free(l_cp);
        }
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_T[i].polys[j]->coeffs[kk] %= l_q;
            }
        }
        lotrs_xof_free(l_xof_sim);
    }

    /* Rejection sampling loop for real signer branch.
     * Hedged randomness: derive retry seed from H(msg || user_seed || attempt).
     * This ensures different messages get different randomness even with the same seed,
     * and mitigates fault injection that replays seeds. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);

    for (uint32_t l_attempt = 0u; l_attempt < CHIPMUNK_RING_NONINT_MAX_RETRIES; ++l_attempt) {
        /* Derive hedged seed. */
        uint8_t l_retry_seed[32];
        lotrs_xof_t *l_xof_hedge = lotrs_xof_new((const uint8_t *)"crv2-hedge-v1", 13u);
        if (!l_xof_hedge) { /* cleanup */ return -ENOMEM; }
        lotrs_xof_absorb(l_xof_hedge, a_msg, a_msg_len);
        lotrs_xof_absorb(l_xof_hedge, a_seed, 32u);
        uint8_t l_attempt_buf[4] = {
            (uint8_t)(l_attempt & 0xFF),
            (uint8_t)((l_attempt >> 8) & 0xFF),
            (uint8_t)((l_attempt >> 16) & 0xFF),
            (uint8_t)((l_attempt >> 24) & 0xFF)
        };
        lotrs_xof_absorb(l_xof_hedge, l_attempt_buf, 4u);
        lotrs_xof_squeeze(l_xof_hedge, l_retry_seed, 32u);
        lotrs_xof_free(l_xof_hedge);

        /* Sample y from hedged seed. */
        lotrs_xof_t *l_xof = lotrs_xof_new(l_retry_seed, 32u);
        if (!l_xof) { /* cleanup */ return -ENOMEM; }
        lotrs_xof_absorb(l_xof, (const uint8_t *)"crv2-sign-v1", 12u);

        lotrs_polyvec_t l_y = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_y.polys) { lotrs_xof_free(l_xof); /* cleanup */ return -ENOMEM; }
        l_rc = lotrs_sample_short_vec(&l_y, l_xof, a_par, a_par->eta);
        if (l_rc != 0) { lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); /* cleanup */ return l_rc; }
        lotrs_xof_free(l_xof);

        /* T_ell = A * y_short + y_tail. */
        lotrs_polyvec_t l_y_short = { .polys = l_y.polys, .n = a_par->l };
        lotrs_polyvec_t l_y_tail  = { .polys = l_y.polys + a_par->l, .n = a_par->k };
        lotrs_polymat_vecmul(&l_T[a_signer_idx], &l_A, &l_y_short, a_par);
        lotrs_polyvec_add(&l_T[a_signer_idx], &l_T[a_signer_idx], &l_y_tail, a_par);
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_T[a_signer_idx].polys[j]->coeffs[kk] %= l_q;
            }
        }

        /* FS challenge: c = H(T_0, ..., T_{N-1}, msg). */
        lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
        if (!l_xof_c) { lotrs_polyvec_free(&l_y); /* cleanup */ return -ENOMEM; }
        lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
        for (uint32_t i = 0u; i < l_N; ++i) {
            for (uint32_t j = 0u; j < a_par->k; ++j) {
                uint8_t l_buf[8 * 128];
                lotrs_poly_pack(l_buf, lotrs_poly_bytes(a_par), l_T[i].polys[j], a_par);
                lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
            }
        }

        lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
        if (!l_c_total) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); /* cleanup */ return -ENOMEM; }
        l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
        lotrs_xof_free(l_xof_c);
        if (l_rc != 0) { lotrs_poly_free(l_c_total); lotrs_polyvec_free(&l_y); /* cleanup */ return l_rc; }

        /* c_ell = c_total - Σ_{i!=ell} c_i. */
        lotrs_poly_copy(l_c_arr[a_signer_idx], l_c_total, a_par);
        for (uint32_t i = 0u; i < l_N; ++i) {
            if (i == a_signer_idx) continue;
            lotrs_poly_sub(l_c_arr[a_signer_idx], l_c_arr[a_signer_idx], l_c_arr[i], a_par);
        }
        lotrs_poly_free(l_c_total);

        /* z_ell = y + c_ell * s.
         * Blinding: generate random mask r, compute c*(s+r) - c*r = c*s.
         * Both multiplications have randomized inputs, protecting s from side-channel. */
        lotrs_poly_t *l_r = lotrs_poly_alloc(a_par);
        if (!l_r) { lotrs_polyvec_free(&l_y); /* cleanup */ return -ENOMEM; }
        lotrs_xof_t *l_xof_r = lotrs_xof_new(a_seed, 32u);
        if (!l_xof_r) { lotrs_poly_free(l_r); lotrs_polyvec_free(&l_y); return -ENOMEM; }
        lotrs_xof_absorb(l_xof_r, (const uint8_t *)"crv2-blind-v1", 13u);
        lotrs_sample_short(l_r, l_xof_r, a_par, a_par->eta);
        lotrs_xof_free(l_xof_r);

        for (uint32_t i = 0u; i < l_len; ++i) {
            /* s_masked = s[i] + r. */
            lotrs_poly_t *l_s_masked = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cr = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cs_masked = lotrs_poly_alloc(a_par);
            if (!l_s_masked || !l_cr || !l_cs_masked) {
                lotrs_poly_free(l_s_masked); lotrs_poly_free(l_cr); lotrs_poly_free(l_cs_masked);
                lotrs_poly_free(l_r); lotrs_polyvec_free(&l_y); return -ENOMEM;
            }
            lotrs_poly_add(l_s_masked, a_sk->s.polys[i], l_r, a_par);
            /* cs_masked = c * s_masked (randomized input). */
            lotrs_poly_mul(l_cs_masked, l_c_arr[a_signer_idx], l_s_masked, a_par);
            /* cr = c * r (randomized input). */
            lotrs_poly_mul(l_cr, l_c_arr[a_signer_idx], l_r, a_par);
            /* z[i] = y[i] + cs_masked - cr = y[i] + c*s[i]. */
            lotrs_poly_add(l_z[a_signer_idx].polys[i], l_y.polys[i], l_cs_masked, a_par);
            lotrs_poly_sub(l_z[a_signer_idx].polys[i], l_z[a_signer_idx].polys[i], l_cr, a_par);
            lotrs_poly_free(l_s_masked); lotrs_poly_free(l_cr); lotrs_poly_free(l_cs_masked);
        }
        lotrs_poly_free(l_r);

        lotrs_polyvec_free(&l_y);

        /* Check norm bound. */
        int l_ok = 1;
        for (uint32_t i = 0u; i < l_len; ++i) {
            if (!lotrs_reject_infinity_norm(l_z[a_signer_idx].polys[i], l_bound, a_par)) {
                l_ok = 0;
                break;
            }
        }
        if (l_ok) {
            /* Serialize: header + T (raw) + c (raw) + z (Rice-coded). */
            size_t l_T_bytes = (size_t)l_N * lotrs_polyvec_bytes(a_par, a_par->k);
            size_t l_c_bytes = (size_t)l_N * lotrs_poly_bytes(a_par);

            /* Pre-encode z with Rice coding. */
            int64_t l_rice_bound_z = l_bound;
            uint32_t l_rice_k_z = lotrs_optimal_rice_k((double)l_rice_bound_z);
            size_t l_z_count = (size_t)l_N * l_len;
            size_t *l_z_sizes = DAP_NEW_Z_SIZE(size_t, l_z_count * sizeof(size_t));
            uint8_t **l_z_bufs = DAP_NEW_Z_COUNT(uint8_t *, l_z_count);
            if (!l_z_sizes || !l_z_bufs) {
                DAP_DELETE(l_z_sizes); DAP_DELETE(l_z_bufs);
                /* cleanup */ return -ENOMEM;
            }

            size_t l_z_rice_total = 0u;
            int l_rice_ok = 1;
            for (size_t idx = 0u; idx < l_z_count && l_rice_ok; ++idx) {
                uint32_t ri = (uint32_t)(idx / l_len);
                uint32_t rj = (uint32_t)(idx % l_len);
                size_t l_cap = lotrs_poly_bytes(a_par);
                l_z_bufs[idx] = DAP_NEW_Z_SIZE(uint8_t, l_cap);
                if (!l_z_bufs[idx]) { l_rice_ok = 0; break; }
                size_t l_written = 0u;
                int l_rc = lotrs_poly_pack_rice(l_z_bufs[idx], l_cap,
                                                l_z[ri].polys[rj], a_par,
                                                l_rice_k_z, l_rice_bound_z, &l_written);
                if (l_rc != 0) { l_rice_ok = 0; break; }
                l_z_sizes[idx] = l_written;
                l_z_rice_total += l_written;
            }

            if (!l_rice_ok) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                /* cleanup */ return -ENOMEM;
            }

            size_t l_z_wire = l_z_count * 4u + l_z_rice_total;
            size_t l_total = CHIPMUNK_RING_HEADER_BYTES + l_T_bytes + l_c_bytes + l_z_wire;

            a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_total);
            if (!a_sig->data) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                return -ENOMEM;
            }
            a_sig->len = l_total;

            chipmunk_ring_header_t l_hdr = {
                .magic = CHIPMUNK_RING_MAGIC,
                .version = CHIPMUNK_RING_VERSION,
                .d = a_par->d,
                .N = l_N,
                .rice_k_z = l_rice_k_z,
                .rice_bound_z = l_rice_bound_z,
                .flags = 0u,
            };
            uint8_t *l_p = a_sig->data;
            memcpy(l_p, &l_hdr, CHIPMUNK_RING_HEADER_BYTES);
            l_p += CHIPMUNK_RING_HEADER_BYTES;

            /* Write raw T. */
            for (uint32_t i = 0u; i < l_N; ++i) {
                lotrs_polyvec_pack(l_p, lotrs_polyvec_bytes(a_par, a_par->k), &l_T[i], a_par);
                l_p += lotrs_polyvec_bytes(a_par, a_par->k);
            }

            /* Write raw c. */
            for (uint32_t i = 0u; i < l_N; ++i) {
                lotrs_poly_pack(l_p, lotrs_poly_bytes(a_par), l_c_arr[i], a_par);
                l_p += lotrs_poly_bytes(a_par);
            }

            /* Write Rice-coded z. */
            for (size_t idx = 0u; idx < l_z_count; ++idx) {
                uint32_t l_sz32 = (uint32_t)l_z_sizes[idx];
                memcpy(l_p, &l_sz32, 4u);
                l_p += 4u;
                memcpy(l_p, l_z_bufs[idx], l_z_sizes[idx]);
                l_p += l_z_sizes[idx];
            }

            for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
            DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);

            /* Cleanup. */
            for (uint32_t i = 0u; i < l_N; ++i) {
                lotrs_polyvec_free(&l_T[i]); lotrs_poly_free(l_c_arr[i]); lotrs_polyvec_free(&l_z[i]);
            }
            DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
            lotrs_polymat_free(&l_A);
            return 0;
        }
        /* Retry: norm exceeded. */
    }

    /* All retries exhausted. */
    debug_if(1, L_DEBUG, "CRV2 sign: rejection sampling failed after %u retries", CHIPMUNK_RING_NONINT_MAX_RETRIES);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_polyvec_free(&l_T[i]); lotrs_poly_free(l_c_arr[i]); lotrs_polyvec_free(&l_z[i]);
    }
    DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
    lotrs_polymat_free(&l_A);
    return -EAGAIN;
}

/* --- Verify --- */

int chipmunk_ring_verify(const chipmunk_ring_sig_t *a_sig,
                            const lotrs_params_t *a_par,
                            const chipmunk_ring_table_t *a_ring,
                            const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sig || !a_sig->data || !a_par || !a_ring || !a_msg) return -EINVAL;
    if (a_sig->len < CHIPMUNK_RING_HEADER_BYTES) return -EINVAL;

    int l_rc;

    /* Read header. */
    chipmunk_ring_header_t l_hdr = {0};
    memcpy(&l_hdr, a_sig->data, CHIPMUNK_RING_HEADER_BYTES);
    if (l_hdr.magic != CHIPMUNK_RING_MAGIC) return -EINVAL;
    if (l_hdr.version != CHIPMUNK_RING_VERSION) return -EINVAL;
    if (l_hdr.N != a_ring->N) return -EINVAL;

    const uint32_t l_N = a_ring->N;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Deserialize T_i (raw), c_i (raw), z_i (Rice-coded). */
    const uint8_t *l_p = a_sig->data + CHIPMUNK_RING_HEADER_BYTES;
    size_t l_T_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);

    lotrs_polyvec_t *l_T = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    lotrs_poly_t **l_c_arr = DAP_NEW_Z_COUNT(lotrs_poly_t *, l_N);
    lotrs_polyvec_t *l_z = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    if (!l_T || !l_c_arr || !l_z) {
        DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
        return -ENOMEM;
    }

    for (uint32_t i = 0u; i < l_N; ++i) {
        l_T[i] = lotrs_polyvec_alloc(a_par, a_par->k);
        l_c_arr[i] = lotrs_poly_alloc(a_par);
        l_z[i] = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_T[i].polys || !l_c_arr[i] || !l_z[i].polys) {
            for (uint32_t j = 0u; j <= i; ++j) {
                lotrs_polyvec_free(&l_T[j]); lotrs_poly_free(l_c_arr[j]); lotrs_polyvec_free(&l_z[j]);
            }
            DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
            return -ENOMEM;
        }
        lotrs_polyvec_unpack(&l_T[i], l_p, l_T_bytes, a_par);
        l_p += l_T_bytes;
    }

    /* Deserialize c_i (raw). */
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_poly_unpack(l_c_arr[i], l_p, l_c_bytes, a_par);
        l_p += l_c_bytes;
    }

    /* Deserialize z_i with Rice coding. */
    int64_t l_rice_bound_z = l_hdr.rice_bound_z;
    uint32_t l_rice_k_z = l_hdr.rice_k_z;
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t j = 0u; j < l_len; ++j) {
            if (l_p + 4u > a_sig->data + a_sig->len) {
                /* cleanup */ return -EINVAL;
            }
            uint32_t l_sz32 = 0u;
            memcpy(&l_sz32, l_p, 4u);
            l_p += 4u;
            if (l_p + l_sz32 > a_sig->data + a_sig->len) {
                /* cleanup */ return -EINVAL;
            }
            size_t l_consumed = 0u;
            l_rc = lotrs_poly_unpack_rice(l_z[i].polys[j], l_p, l_sz32,
                                          a_par, l_rice_k_z, l_rice_bound_z, &l_consumed);
            if (l_rc != 0) {
                /* cleanup */ return l_rc;
            }
            l_p += l_sz32;
        }
    }

    /* Recompute FS challenge: c_total = H(T_0..T_{N-1}, msg). */
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
    if (!l_xof_c) { /* cleanup */ return -ENOMEM; }
    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            uint8_t l_buf[8 * 128];
            lotrs_poly_pack(l_buf, lotrs_poly_bytes(a_par), l_T[i].polys[j], a_par);
            lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
        }
    }

    lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
    if (!l_c_total) { lotrs_xof_free(l_xof_c); /* cleanup */ return -ENOMEM; }
    l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) { lotrs_poly_free(l_c_total); /* cleanup */ return l_rc; }

    /* Check c_total == Σ c_i. */
    lotrs_poly_t *l_c_sum = lotrs_poly_alloc(a_par);
    if (!l_c_sum) { lotrs_poly_free(l_c_total); /* cleanup */ return -ENOMEM; }
    lotrs_poly_zero(l_c_sum, a_par);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_poly_add(l_c_sum, l_c_sum, l_c_arr[i], a_par);
    }
    for (uint32_t i = 0u; i < l_d; ++i) {
        if (l_c_total->coeffs[i] % l_q != l_c_sum->coeffs[i] % l_q) {
            debug_if(1, L_DEBUG, "CRV2 verify: challenge sum mismatch at [%u]", i);
            lotrs_poly_free(l_c_total); lotrs_poly_free(l_c_sum);
            /* cleanup */ return -EINVAL;
        }
    }
    lotrs_poly_free(l_c_total);
    lotrs_poly_free(l_c_sum);

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { /* cleanup */ return -ENOMEM; }
    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); /* cleanup */ return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A); /* cleanup */ return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* For each i: norm check + algebraic check A*z_short + z_tail == T_i + c_i*pk[i]. */
    int l_match = 1;
    int64_t l_bound = l_rice_bound_z;
    for (uint32_t i = 0u; i < l_N && l_match; ++i) {
        /* Norm check: ‖z_i‖∞ < φ·η. */
        for (uint32_t j = 0u; j < l_len; ++j) {
            if (!lotrs_reject_infinity_norm(l_z[i].polys[j], l_bound, a_par)) {
                debug_if(1, L_DEBUG, "CRV2 verify: norm check FAILED at member[%u][%u]", i, j);
                l_match = 0;
                break;
            }
        }
        if (!l_match) break;

        lotrs_polyvec_t l_z_short = { .polys = l_z[i].polys, .n = a_par->l };
        lotrs_polyvec_t l_z_tail  = { .polys = l_z[i].polys + a_par->l, .n = a_par->k };

        /* lhs = A * z_short + z_tail. */
        lotrs_polyvec_t l_lhs = lotrs_polyvec_alloc(a_par, a_par->k);
        if (!l_lhs.polys) { lotrs_polymat_free(&l_A); /* cleanup */ return -ENOMEM; }
        lotrs_polymat_vecmul(&l_lhs, &l_A, &l_z_short, a_par);
        lotrs_polyvec_add(&l_lhs, &l_lhs, &l_z_tail, a_par);

        /* rhs = T_i + c_i * pk[i]. */
        lotrs_polyvec_t l_rhs = lotrs_polyvec_alloc(a_par, a_par->k);
        if (!l_rhs.polys) {
            lotrs_polyvec_free(&l_lhs); lotrs_polymat_free(&l_A); /* cleanup */ return -ENOMEM;
        }
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            lotrs_poly_t *l_cp = lotrs_poly_alloc(a_par);
            lotrs_poly_mul(l_cp, l_c_arr[i], a_ring->pks[i].a_hat.polys[j], a_par);
            lotrs_poly_add(l_rhs.polys[j], l_T[i].polys[j], l_cp, a_par);
            lotrs_poly_free(l_cp);
        }

        /* Canonicalize. */
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_lhs.polys[j]->coeffs[kk] %= l_q;
                l_rhs.polys[j]->coeffs[kk] %= l_q;
            }
        }

        /* Compare lhs == rhs. */
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                if (l_lhs.polys[j]->coeffs[kk] != l_rhs.polys[j]->coeffs[kk]) {
                    debug_if(1, L_DEBUG, "CRV2 verify: algebraic FAILED at member[%u][%u][%u]", i, j, kk);
                    l_match = 0;
                    break;
                }
            }
            if (!l_match) break;
        }

        lotrs_polyvec_free(&l_lhs);
        lotrs_polyvec_free(&l_rhs);
    }

    lotrs_polymat_free(&l_A);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_polyvec_free(&l_T[i]); lotrs_poly_free(l_c_arr[i]); lotrs_polyvec_free(&l_z[i]);
    }
    DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);

    return l_match ? 0 : -EINVAL;
}
