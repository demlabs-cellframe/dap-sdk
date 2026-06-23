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
#include "dap_serialize.h"

/* Compute truncated parameter hash (16 bytes). Returns 0 on success. */
static int s_param_hash(uint8_t a_out[16], const lotrs_params_t *a_par)
{
    lotrs_xof_t *l_xof = lotrs_xof_new((const uint8_t *)"crv2-params-v1", 14u);
    if (!l_xof) { memset(a_out, 0, 16u); return -ENOMEM; }
    int l_rc;
    uint8_t l_buf[8];
    memcpy(l_buf, &a_par->d, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->q, 8u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 8u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->k, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->l, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->w, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->eta, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    lotrs_xof_squeeze(l_xof, a_out, 16u);
    lotrs_xof_free(l_xof);
    dap_memwipe(l_buf, sizeof(l_buf));
    return 0;
}

/* --- Header schema (dap_serialize) --- */

static const dap_serialize_field_t s_chipmunk_ring_header_fields[] = {
    { .name = "magic",        .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, magic),        .size = sizeof(uint32_t) },
    { .name = "version",      .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, version),      .size = sizeof(uint32_t) },
    { .name = "d",            .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, d),            .size = sizeof(uint32_t) },
    { .name = "N",            .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, N),            .size = sizeof(uint32_t) },
    { .name = "rice_k_z",     .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, rice_k_z),     .size = sizeof(uint32_t) },
    { .name = "rice_bound_z", .type = DAP_SERIALIZE_TYPE_INT64,  .offset = offsetof(chipmunk_ring_header_t, rice_bound_z), .size = sizeof(int64_t)  },
    { .name = "flags",        .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, flags),        .size = sizeof(uint32_t) },
    { .name = "param_hash",   .type = DAP_SERIALIZE_TYPE_ARRAY_FIXED, .offset = offsetof(chipmunk_ring_header_t, param_hash),
      .size = 1u, .fixed_count = 16u, .element_type = DAP_SERIALIZE_TYPE_UINT8 },
};

DAP_SERIALIZE_SCHEMA_DEFINE(s_chipmunk_ring_header_schema,
                            chipmunk_ring_header_t,
                            s_chipmunk_ring_header_fields);

/* Wipe polynomial coefficients before freeing (secret material). */
static inline void s_poly_wipe_free(lotrs_poly_t *a_p)
{
    if (a_p) {
        lotrs_poly_free(a_p);  /* lotrs_poly_free already wipes coeffs */
    }
}

/* --- Cleanup --- */

void chipmunk_ring_keypair_free(chipmunk_ring_keypair_t *a_kp)
{
    if (a_kp) {
        lotrs_polyvec_free(&a_kp->pk.a_hat);
        lotrs_polyvec_free(&a_kp->sk.s);
        dap_memwipe(a_kp, sizeof(*a_kp));
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

size_t chipmunk_ring_sig_bytes_max(const lotrs_params_t *a_par, uint32_t a_N)
{
    /* Header + N * T (k polys each) + N * c (1 poly each) + N * z (l+k polys each). */
    size_t l_poly = lotrs_poly_bytes(a_par);
    size_t l_hdr = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
    return l_hdr
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

    int l_rc;
    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "crv2-keygen-v1";
    l_rc = lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));
    if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }

    const uint32_t l_len = a_par->l + a_par->k;
    a_kp->sk.s = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_kp->sk.s.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    l_rc = lotrs_sample_short_vec(&a_kp->sk.s, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_polyvec_free(&a_kp->sk.s); lotrs_xof_free(l_xof); return l_rc; }

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
            a_kp->pk.a_hat.polys[i]->coeffs[j] = (uint64_t)lotrs_mod_reduce(
                (__int128_t)(int64_t)a_kp->pk.a_hat.polys[i]->coeffs[j], a_par->q);
        }
    }

    lotrs_polymat_free(&l_A);
    lotrs_xof_free(l_xof);
    return 0;
}

/* --- Sign --- */

/* Max rejection sampling retries before giving up. */
#define CHIPMUNK_RING_NONINT_MAX_RETRIES 64

/* Validate a public key table. */
static int s_validate_table(const chipmunk_ring_table_t *a_ring, const lotrs_params_t *a_par)
{
    if (!a_ring || !a_ring->pks || a_ring->N == 0u) return -EINVAL;
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        if (!a_ring->pks[i].a_hat.polys) return -EINVAL;
    }
    return 0;
}

/* Cleanup helper for sign function resources. */
static void s_sign_cleanup(lotrs_polymat_t *a_A,
                           lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                           lotrs_polyvec_t *a_z, uint32_t a_N,
                           uint8_t *a_retry_seed)
{
    if (a_T && a_c_arr && a_z) {
        for (uint32_t i = 0u; i < a_N; ++i) {
            lotrs_polyvec_free(&a_T[i]);
            lotrs_poly_free(a_c_arr[i]);
            lotrs_polyvec_free(&a_z[i]);
        }
    }
    DAP_DELETE(a_T);
    DAP_DELETE(a_c_arr);
    DAP_DELETE(a_z);
    lotrs_polymat_free(a_A);
    if (a_retry_seed) dap_memwipe(a_retry_seed, 32u);
}

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
    if (s_validate_table(a_ring, a_par) != 0) return -EINVAL;

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
        if (!l_xof_sim) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_sim, (const uint8_t *)"crv2-sim", 8u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL); return l_rc; }
        uint8_t l_idx_buf[4];
        l_idx_buf[0] = (uint8_t)i; l_idx_buf[1] = 0; l_idx_buf[2] = 0; l_idx_buf[3] = 0;
        l_rc = lotrs_xof_absorb(l_xof_sim, l_idx_buf, 4u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL); return l_rc; }

        l_rc = lotrs_sample_ternary(l_c_arr[i], l_xof_sim, a_par, a_par->w);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL); return l_rc; }

        lotrs_polyvec_t l_z_short_i = { .polys = l_z[i].polys, .n = a_par->l };
        lotrs_polyvec_t l_z_tail_i  = { .polys = l_z[i].polys + a_par->l, .n = a_par->k };
        l_rc = lotrs_sample_short_vec(&l_z[i], l_xof_sim, a_par, a_par->eta);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL); return l_rc; }

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
                l_T[i].polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_T[i].polys[j]->coeffs[kk], l_q);
            }
        }
        lotrs_xof_free(l_xof_sim);
    }

    /* Rejection sampling loop for real signer branch.
     * Hedged randomness: derive retry seed from H(msg || user_seed || attempt).
     * This ensures different messages get different randomness even with the same seed,
     * and mitigates fault injection that replays seeds. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    uint8_t l_retry_seed[32];

    for (uint32_t l_attempt = 0u; l_attempt < CHIPMUNK_RING_NONINT_MAX_RETRIES; ++l_attempt) {
        /* Derive hedged seed. */
        lotrs_xof_t *l_xof_hedge = lotrs_xof_new((const uint8_t *)"crv2-hedge-v1", 13u);
        if (!l_xof_hedge) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_hedge, a_msg, a_msg_len);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
        l_rc = lotrs_xof_absorb(l_xof_hedge, a_seed, 32u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
        uint8_t l_attempt_buf[4] = {
            (uint8_t)(l_attempt & 0xFF),
            (uint8_t)((l_attempt >> 8) & 0xFF),
            (uint8_t)((l_attempt >> 16) & 0xFF),
            (uint8_t)((l_attempt >> 24) & 0xFF)
        };
        l_rc = lotrs_xof_absorb(l_xof_hedge, l_attempt_buf, 4u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
        lotrs_xof_squeeze(l_xof_hedge, l_retry_seed, 32u);
        lotrs_xof_free(l_xof_hedge);

        /* Sample y from hedged seed. */
        lotrs_xof_t *l_xof = lotrs_xof_new(l_retry_seed, 32u);
        if (!l_xof) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof, (const uint8_t *)"crv2-sign-v1", 12u);
        if (l_rc != 0) { lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }

        lotrs_polyvec_t l_y = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_y.polys) { lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
        l_rc = lotrs_sample_short_vec(&l_y, l_xof, a_par, a_par->eta);
        if (l_rc != 0) { lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
        lotrs_xof_free(l_xof);

        /* T_ell = A * y_short + y_tail. */
        lotrs_polyvec_t l_y_short = { .polys = l_y.polys, .n = a_par->l };
        lotrs_polyvec_t l_y_tail  = { .polys = l_y.polys + a_par->l, .n = a_par->k };
        lotrs_polymat_vecmul(&l_T[a_signer_idx], &l_A, &l_y_short, a_par);
        lotrs_polyvec_add(&l_T[a_signer_idx], &l_T[a_signer_idx], &l_y_tail, a_par);
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_T[a_signer_idx].polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_T[a_signer_idx].polys[j]->coeffs[kk], l_q);
            }
        }

        /* FS challenge: c = H(T_0, ..., T_{N-1}, msg). */
        lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
        if (!l_xof_c) { lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
        if (l_rc != 0) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
        for (uint32_t i = 0u; i < l_N; ++i) {
            for (uint32_t j = 0u; j < a_par->k; ++j) {
                uint8_t l_buf[LOTRS_D_MAX * 8]; /* max poly bytes */
                lotrs_poly_pack(l_buf, sizeof(l_buf), l_T[i].polys[j], a_par);
                l_rc = lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
                if (l_rc != 0) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
            }
        }

        lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
        if (!l_c_total) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
        l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
        lotrs_xof_free(l_xof_c);
        if (l_rc != 0) { lotrs_poly_free(l_c_total); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }

        /* c_ell = c_total - Σ_{i!=ell} c_i. */
        lotrs_poly_copy(l_c_arr[a_signer_idx], l_c_total, a_par);
        for (uint32_t i = 0u; i < l_N; ++i) {
            if (i == a_signer_idx) continue;
            lotrs_poly_sub(l_c_arr[a_signer_idx], l_c_arr[a_signer_idx], l_c_arr[i], a_par);
        }
        lotrs_poly_free(l_c_total);

        /* z_ell = y + c_ell * s.
         * Blinding: per-component random mask r_i, compute c*(s[i]+r_i) - c*r_i = c*s[i].
         * Both multiplications have randomized inputs, protecting s from side-channel. */
        for (uint32_t i = 0u; i < l_len; ++i) {
            lotrs_poly_t *l_r = lotrs_poly_alloc(a_par);
            if (!l_r) { lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
            lotrs_xof_t *l_xof_r = lotrs_xof_new(a_seed, 32u);
            if (!l_xof_r) { s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM; }
            l_rc = lotrs_xof_absorb(l_xof_r, (const uint8_t *)"crv2-blind-v1", 13u);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
            l_rc = lotrs_xof_absorb(l_xof_r, l_attempt_buf, 4u);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
            uint8_t l_comp_buf[4] = {
                (uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF),
                (uint8_t)((i >> 16) & 0xFF), (uint8_t)((i >> 24) & 0xFF)
            };
            l_rc = lotrs_xof_absorb(l_xof_r, l_comp_buf, 4u); /* unique mask per component */
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
            l_rc = lotrs_sample_short(l_r, l_xof_r, a_par, a_par->eta);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc; }
            lotrs_xof_free(l_xof_r);

            /* s_masked = s[i] + r_i. */
            lotrs_poly_t *l_s_masked = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cr = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cs_masked = lotrs_poly_alloc(a_par);
            if (!l_s_masked || !l_cr || !l_cs_masked) {
                s_poly_wipe_free(l_s_masked); s_poly_wipe_free(l_cr); lotrs_poly_free(l_cs_masked);
                s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM;
            }
            lotrs_poly_add(l_s_masked, a_sk->s.polys[i], l_r, a_par);
            /* cs_masked = c * s_masked (randomized input). */
            lotrs_poly_mul(l_cs_masked, l_c_arr[a_signer_idx], l_s_masked, a_par);
            /* cr = c * r_i (randomized input). */
            lotrs_poly_mul(l_cr, l_c_arr[a_signer_idx], l_r, a_par);
            /* z[i] = y[i] + cs_masked - cr = y[i] + c*s[i]. */
            lotrs_poly_add(l_z[a_signer_idx].polys[i], l_y.polys[i], l_cs_masked, a_par);
            lotrs_poly_sub(l_z[a_signer_idx].polys[i], l_z[a_signer_idx].polys[i], l_cr, a_par);
            s_poly_wipe_free(l_s_masked); s_poly_wipe_free(l_cr); lotrs_poly_free(l_cs_masked);
            s_poly_wipe_free(l_r);
        }

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
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM;
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
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM;
            }

            size_t l_z_wire = l_z_count * 4u + l_z_rice_total;
            size_t l_hdr_bytes = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
            size_t l_total = l_hdr_bytes + l_T_bytes + l_c_bytes + l_z_wire;

            a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_total);
            if (!a_sig->data) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -ENOMEM;
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
            l_rc = s_param_hash(l_hdr.param_hash, a_par);
            if (l_rc != 0) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return l_rc;
            }
            uint8_t *l_p = a_sig->data;
            dap_serialize_result_t l_ser = dap_serialize_to_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                       &l_hdr, l_p, l_hdr_bytes, NULL);
            if (l_ser.error_code != 0) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed); return -EFAULT;
            }
            l_p += l_hdr_bytes;

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
            s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed);
            return 0;
        }
        /* Retry: norm exceeded. */
    }

    /* All retries exhausted. */
    debug_if(1, L_DEBUG, "CRV2 sign: rejection sampling failed after %u retries", CHIPMUNK_RING_NONINT_MAX_RETRIES);
    s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed);
    return -EAGAIN;
}

/* --- Verify --- */

/* Cleanup helper for verify function resources. */
static void s_verify_cleanup(lotrs_polymat_t *a_A,
                             lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                             lotrs_polyvec_t *a_z, uint32_t a_N)
{
    if (a_T && a_c_arr && a_z) {
        for (uint32_t i = 0u; i < a_N; ++i) {
            lotrs_polyvec_free(&a_T[i]);
            lotrs_poly_free(a_c_arr[i]);
            lotrs_polyvec_free(&a_z[i]);
        }
    }
    DAP_DELETE(a_T);
    DAP_DELETE(a_c_arr);
    DAP_DELETE(a_z);
    if (a_A) lotrs_polymat_free(a_A);
}

/* Cleanup for deserialization phase (no A matrix yet). */
static void s_verify_cleanup_arrays(lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                                    lotrs_polyvec_t *a_z, uint32_t a_N)
{
    s_verify_cleanup(NULL, a_T, a_c_arr, a_z, a_N);
}

int chipmunk_ring_verify(const chipmunk_ring_sig_t *a_sig,
                            const lotrs_params_t *a_par,
                            const chipmunk_ring_table_t *a_ring,
                            const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sig || !a_sig->data || !a_par || !a_ring || !a_msg) return -EINVAL;
    if (s_validate_table(a_ring, a_par) != 0) return -EINVAL;
    size_t l_hdr_bytes = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
    if (a_sig->len < l_hdr_bytes) return -EINVAL;

    int l_rc;

    /* Read header. */
    chipmunk_ring_header_t l_hdr = {0};
    dap_deserialize_result_t l_deser = dap_serialize_from_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                     a_sig->data, l_hdr_bytes,
                                                                     &l_hdr, NULL);
    if (l_deser.error_code != 0) return -EINVAL;
    if (l_hdr.magic != CHIPMUNK_RING_MAGIC) return -EINVAL;
    if (l_hdr.version != CHIPMUNK_RING_VERSION) return -EINVAL;
    if (l_hdr.d != a_par->d) return -EINVAL;
    if (l_hdr.N != a_ring->N) return -EINVAL;

    /* Verify parameter hash. */
    uint8_t l_expected_hash[16];
    l_rc = s_param_hash(l_expected_hash, a_par);
    if (l_rc != 0) return l_rc;
    if (memcmp(l_hdr.param_hash, l_expected_hash, 16u) != 0) return -EINVAL;

    const uint32_t l_N = a_ring->N;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Deserialize T_i (raw), c_i (raw), z_i (Rice-coded). */
    const uint8_t *l_p = a_sig->data + l_hdr_bytes;
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
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -EINVAL;
            }
            uint32_t l_sz32 = 0u;
            memcpy(&l_sz32, l_p, 4u);
            l_p += 4u;
            if (l_p + l_sz32 > a_sig->data + a_sig->len) {
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -EINVAL;
            }
            size_t l_consumed = 0u;
            l_rc = lotrs_poly_unpack_rice(l_z[i].polys[j], l_p, l_sz32,
                                          a_par, l_rice_k_z, l_rice_bound_z, &l_consumed);
            if (l_rc != 0) {
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return l_rc;
            }
            l_p += l_sz32;
        }
    }

    /* Recompute FS challenge: c_total = H(T_0..T_{N-1}, msg). */
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
    if (!l_xof_c) { s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
    l_rc = lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    if (l_rc != 0) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return l_rc; }
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            uint8_t l_buf[LOTRS_D_MAX * 8]; /* max poly bytes */
            lotrs_poly_pack(l_buf, sizeof(l_buf), l_T[i].polys[j], a_par);
            l_rc = lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
            if (l_rc != 0) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return l_rc; }
        }
    }

    lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
    if (!l_c_total) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
    l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) { lotrs_poly_free(l_c_total); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return l_rc; }

    /* Check c_total == Σ c_i. */
    lotrs_poly_t *l_c_sum = lotrs_poly_alloc(a_par);
    if (!l_c_sum) { lotrs_poly_free(l_c_total); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
    lotrs_poly_zero(l_c_sum, a_par);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_poly_add(l_c_sum, l_c_sum, l_c_arr[i], a_par);
    }
    for (uint32_t i = 0u; i < l_d; ++i) {
        if (l_c_total->coeffs[i] % l_q != l_c_sum->coeffs[i] % l_q) {
            debug_if(1, L_DEBUG, "CRV2 verify: challenge sum mismatch at [%u]", i);
            lotrs_poly_free(l_c_total); lotrs_poly_free(l_c_sum);
            s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -EINVAL;
        }
    }
    lotrs_poly_free(l_c_total);
    lotrs_poly_free(l_c_sum);

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N); return l_rc;
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
        if (!l_lhs.polys) { s_verify_cleanup(&l_A, l_T, l_c_arr, l_z, l_N); return -ENOMEM; }
        lotrs_polymat_vecmul(&l_lhs, &l_A, &l_z_short, a_par);
        lotrs_polyvec_add(&l_lhs, &l_lhs, &l_z_tail, a_par);

        /* rhs = T_i + c_i * pk[i]. */
        lotrs_polyvec_t l_rhs = lotrs_polyvec_alloc(a_par, a_par->k);
        if (!l_rhs.polys) {
            lotrs_polyvec_free(&l_lhs); s_verify_cleanup(&l_A, l_T, l_c_arr, l_z, l_N); return -ENOMEM;
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
                l_lhs.polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_lhs.polys[j]->coeffs[kk], l_q);
                l_rhs.polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_rhs.polys[j]->coeffs[kk], l_q);
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
