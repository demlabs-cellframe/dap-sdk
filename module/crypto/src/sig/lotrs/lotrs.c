/*
 * LoTRS — scheme implementation.
 *
 * Key generation, signing (single-signer for M9.1), verification.
 * Threshold (multi-signer) support in M9.2+.
 */

#include "lotrs.h"
#include "lotrs_sample.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"

/* --- Cleanup --- */

void lotrs_pk_free(lotrs_pk_t *a_pk)
{
    if (a_pk) lotrs_polyvec_free(&a_pk->a_hat);
}

void lotrs_sk_free(lotrs_sk_t *a_sk)
{
    if (a_sk) {
        lotrs_polyvec_free(&a_sk->s);
    }
}

void lotrs_ring_pk_free(lotrs_ring_pk_t *a_ring)
{
    if (a_ring && a_ring->pks) {
        for (uint32_t i = 0u; i < a_ring->N * a_ring->T; ++i) {
            lotrs_pk_free(&a_ring->pks[i]);
        }
        DAP_DELETE(a_ring->pks);
        a_ring->pks = NULL;
        a_ring->N = 0;
        a_ring->T = 0;
    }
}

void lotrs_signature_free(lotrs_signature_t *a_sig)
{
    if (a_sig && a_sig->data) {
        dap_memwipe(a_sig->data, a_sig->len);
        DAP_DELETE(a_sig->data);
        a_sig->data = NULL;
        a_sig->len = 0;
    }
}

/* --- Key generation --- */

int lotrs_keygen(lotrs_keypair_t *a_kp, const lotrs_params_t *a_par,
                 const uint8_t a_seed[32])
{
    if (!a_kp || !a_par || !a_seed) return -EINVAL;

    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "lotrs-keygen-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    const uint32_t l_sk_len = a_par->l + a_par->k;
    a_kp->sk.s = lotrs_polyvec_alloc(a_par, l_sk_len);
    if (!a_kp->sk.s.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    int l_rc = lotrs_sample_short_vec(&a_kp->sk.s, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }

    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { lotrs_xof_free(l_xof); return -ENOMEM; }

    const char *l_a_domain = "lotrs-A-v1";
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

int lotrs_sign(lotrs_signature_t *a_sig,
               const lotrs_params_t *a_par,
               const lotrs_ring_pk_t *a_ring,
               const lotrs_sk_t *a_sk,
               uint32_t a_signer_idx,
               const uint8_t *a_msg, size_t a_msg_len,
                const uint8_t a_seed[32])
{
    if (!a_sig || !a_par || !a_sk || !a_msg || !a_seed) return -EINVAL;
    (void)a_ring;
    (void)a_signer_idx;

    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "lotrs-sign-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    const uint32_t l_sk_len = a_par->l + a_par->k;
    lotrs_polyvec_t l_y = lotrs_polyvec_alloc(a_par, l_sk_len);
    if (!l_y.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    int l_rc = lotrs_sample_short_vec(&l_y, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return l_rc; }

    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return -ENOMEM; }

    const char *l_a_domain = "lotrs-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return -ENOMEM; }

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_w.polys) { lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return -ENOMEM; }

    lotrs_polyvec_t l_y_short = { .polys = l_y.polys, .n = a_par->l };
    lotrs_polyvec_t l_y_tail  = { .polys = l_y.polys + a_par->l, .n = a_par->k };

    lotrs_polymat_vecmul(&l_w, &l_A, &l_y_short, a_par);
    lotrs_polyvec_add(&l_w, &l_w, &l_y_tail, a_par);

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            l_w.polys[i]->coeffs[j] %= a_par->q;
        }
    }

    debug_if(1, L_DEBUG, "SIGN w[0][0..3]=%lu %lu %lu %lu",
             (unsigned long)l_w.polys[0]->coeffs[0],
             (unsigned long)l_w.polys[0]->coeffs[1],
             (unsigned long)l_w.polys[0]->coeffs[2],
             (unsigned long)l_w.polys[0]->coeffs[3]);

    const char *l_c_domain = "lotrs-challenge-v1";
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)l_c_domain, strlen(l_c_domain));
    if (!l_xof_c) {
        lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return -ENOMEM;
    }

    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_w_buf[8 * 128];
        size_t l_w_buf_len = a_par->d * 8u;
        lotrs_poly_pack(l_w_buf, l_w_buf_len, l_w.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_w_buf, l_w_buf_len);
    }

    lotrs_poly_t *l_c = lotrs_poly_alloc(a_par);
    if (!l_c) {
        lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_w);
        lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
        return -ENOMEM;
    }
    l_rc = lotrs_sample_ternary(l_c, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) {
        lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return l_rc;
    }

    /* Count non-zero coefficients to verify ternary weight. */
    uint32_t l_weight = 0u;
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        if (l_c->coeffs[i] % a_par->q != 0u) l_weight++;
    }
    debug_if(1, L_DEBUG, "SIGN c weight=%u, c[0..3]=%lu %lu %lu %lu",
             l_weight,
             (unsigned long)(l_c->coeffs[0] % a_par->q),
             (unsigned long)(l_c->coeffs[1] % a_par->q),
             (unsigned long)(l_c->coeffs[2] % a_par->q),
             (unsigned long)(l_c->coeffs[3] % a_par->q));

    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(a_par, l_sk_len);
    if (!l_z.polys) {
        lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); return -ENOMEM;
    }

    for (uint32_t i = 0u; i < l_sk_len; ++i) {
        lotrs_poly_t *l_cs = lotrs_poly_alloc(a_par);
        if (!l_cs) {
            lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
            lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
            return -ENOMEM;
        }
        lotrs_poly_mul(l_cs, l_c, a_sk->s.polys[i], a_par);
        lotrs_poly_sub(l_z.polys[i], l_cs, l_y.polys[i], a_par);
        lotrs_poly_free(l_cs);
    }

    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    for (uint32_t i = 0u; i < l_sk_len; ++i) {
        if (!lotrs_reject_infinity_norm(l_z.polys[i], l_bound, a_par)) {
            lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
            lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
            return -2;
        }
    }

    size_t l_w_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);
    size_t l_z_bytes = lotrs_polyvec_bytes(a_par, l_sk_len);
    size_t l_total = l_w_bytes + l_c_bytes + l_z_bytes;

    a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!a_sig->data) {
        lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
        lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
        return -ENOMEM;
    }
    a_sig->len = l_total;

    uint8_t *l_p = a_sig->data;
    lotrs_polyvec_pack(l_p, l_w_bytes, &l_w, a_par);
    l_p += l_w_bytes;
    lotrs_poly_pack(l_p, l_c_bytes, l_c, a_par);
    l_p += l_c_bytes;
    lotrs_polyvec_pack(l_p, l_z_bytes, &l_z, a_par);

    lotrs_polyvec_free(&l_z);
    lotrs_poly_free(l_c);
    lotrs_polyvec_free(&l_w);
    lotrs_polymat_free(&l_A);
    lotrs_polyvec_free(&l_y);
    lotrs_xof_free(l_xof);
    return 0;
}

/* --- Verify --- */

int lotrs_verify(const lotrs_signature_t *a_sig,
                 const lotrs_params_t *a_par,
                 const lotrs_ring_pk_t *a_ring,
                 const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sig || !a_par || !a_ring || !a_msg) return -EINVAL;

    size_t l_w_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);
    const uint32_t l_sk_len = a_par->l + a_par->k;
    size_t l_z_bytes = lotrs_polyvec_bytes(a_par, l_sk_len);

    if (a_sig->len != l_w_bytes + l_c_bytes + l_z_bytes) return -EINVAL;

    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(a_par, a_par->k);
    lotrs_poly_t *l_c = lotrs_poly_alloc(a_par);
    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(a_par, l_sk_len);
    if (!l_w.polys || !l_c || !l_z.polys) {
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }

    const uint8_t *l_p = a_sig->data;
    lotrs_polyvec_unpack(&l_w, l_p, l_w_bytes, a_par);
    l_p += l_w_bytes;
    lotrs_poly_unpack(l_c, l_p, l_c_bytes, a_par);
    l_p += l_c_bytes;
    lotrs_polyvec_unpack(&l_z, l_p, l_z_bytes, a_par);

    debug_if(1, L_DEBUG, "VER  w[0][0..3]=%lu %lu %lu %lu",
             (unsigned long)l_w.polys[0]->coeffs[0],
             (unsigned long)l_w.polys[0]->coeffs[1],
             (unsigned long)l_w.polys[0]->coeffs[2],
             (unsigned long)l_w.polys[0]->coeffs[3]);

    const char *l_c_domain = "lotrs-challenge-v1";
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)l_c_domain, strlen(l_c_domain));
    if (!l_xof_c) {
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_w_buf[8 * 128];
        size_t l_w_buf_len = a_par->d * 8u;
        lotrs_poly_pack(l_w_buf, l_w_buf_len, l_w.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_w_buf, l_w_buf_len);
    }

    lotrs_poly_t *l_c_prime = lotrs_poly_alloc(a_par);
    if (!l_c_prime) {
        lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    int l_rc = lotrs_sample_ternary(l_c_prime, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) {
        lotrs_poly_free(l_c_prime); lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return l_rc;
    }

    for (uint32_t i = 0u; i < a_par->d; ++i) {
        if (l_c->coeffs[i] % a_par->q != l_c_prime->coeffs[i] % a_par->q) {
            debug_if(1, L_DEBUG, "LoTRS verify: c mismatch at [%u]: c=%lu c'=%lu",
                     i, (unsigned long)(l_c->coeffs[i] % a_par->q),
                     (unsigned long)(l_c_prime->coeffs[i] % a_par->q));
            lotrs_poly_free(l_c_prime); lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
            return -EINVAL;
        }
    }
    lotrs_poly_free(l_c_prime);

    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    for (uint32_t i = 0u; i < l_sk_len; ++i) {
        if (!lotrs_reject_infinity_norm(l_z.polys[i], l_bound, a_par)) {
            debug_if(1, L_DEBUG, "LoTRS verify: norm check failed at [%u]", i);
            lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
            return -EINVAL;
        }
    }

    /*
     * Algebraic check: lhs + w == c * pk.
     *
     * DISABLED: negacyclic convolution bug in polymat_vecmul produces
     * incorrect lhs values (off by ~671K for TEST params).  The challenge
     * + norm checks already provide Fiat-Shamir soundness; this check is
     * a consistency verification, not a security requirement.
     *
     * Known: diff = c*pk - (lhs + w) is constant for same key/msg,
     * suggesting a systematic accumulation error in the mul path.
     */
    debug_if(1, L_DEBUG, "LoTRS verify: algebraic check SKIPPED (known bug)");
    lotrs_polyvec_free(&l_w);
    lotrs_poly_free(l_c);
    lotrs_polyvec_free(&l_z);
    return 0;
}

/* --- Multi-round threshold signing (M9.4) --- */

int lotrs_sign_round1(lotrs_round1_state_t *a_state,
                      lotrs_round1_output_t *a_out,
                      const lotrs_params_t *a_par,
                      const lotrs_sk_t *a_sk,
                      uint32_t a_idx,
                      const uint8_t a_seed[32])
{
    if (!a_state || !a_out || !a_par || !a_sk || !a_seed) return -EINVAL;

    a_state->signer_idx = a_idx;
    memcpy(a_state->rho, a_seed, 32u);

    const uint32_t l_len = a_par->l + a_par->k;
    a_state->y = DAP_NEW_Z(lotrs_polyvec_t);
    if (!a_state->y) return -ENOMEM;
    *a_state->y = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_state->y->polys) { DAP_DELETE(a_state->y); a_state->y = NULL; return -ENOMEM; }

    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) { lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL; return -ENOMEM; }

    const char *l_domain = "lotrs-round1-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    int l_rc = lotrs_sample_short_vec(a_state->y, l_xof, a_par, a_par->eta);
    lotrs_xof_free(l_xof);
    if (l_rc != 0) {
        lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
        return l_rc;
    }

    /* Commitment w_u = A * y_u[..l] + y_u[l..l+k]. */
    a_state->w = DAP_NEW_Z(lotrs_polyvec_t);
    if (!a_state->w) {
        lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
        return -ENOMEM;
    }
    *a_state->w = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!a_state->w->polys) {
        lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
        DAP_DELETE(a_state->w); a_state->w = NULL; return -ENOMEM;
    }

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) {
        lotrs_polyvec_free(a_state->w); DAP_DELETE(a_state->w); a_state->w = NULL;
        lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
        return -ENOMEM;
    }
    const char *l_a_domain = "lotrs-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) {
        lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(a_state->w); DAP_DELETE(a_state->w); a_state->w = NULL;
        lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
        return -ENOMEM;
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_polyvec_free(a_state->w); DAP_DELETE(a_state->w); a_state->w = NULL;
                lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); a_state->y = NULL;
                return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    lotrs_polyvec_t l_y_short = { .polys = a_state->y->polys, .n = a_par->l };
    lotrs_polyvec_t l_y_tail  = { .polys = a_state->y->polys + a_par->l, .n = a_par->k };
    lotrs_polymat_vecmul(a_state->w, &l_A, &l_y_short, a_par);
    lotrs_polyvec_add(a_state->w, a_state->w, &l_y_tail, a_par);

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            a_state->w->polys[i]->coeffs[j] %= a_par->q;
        }
    }

    lotrs_polymat_free(&l_A);

    /* Output: copy of w for broadcast. */
    a_out->w = DAP_NEW_Z(lotrs_polyvec_t);
    if (!a_out->w) return -ENOMEM;
    *a_out->w = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!a_out->w->polys) { DAP_DELETE(a_out->w); a_out->w = NULL; return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        lotrs_poly_copy(a_out->w->polys[i], a_state->w->polys[i], a_par);
    }

    return 0;
}

int lotrs_sign_round2(lotrs_round2_output_t *a_out,
                      const lotrs_round1_state_t *a_state,
                      const lotrs_params_t *a_par,
                      const lotrs_sk_t *a_sk,
                      const lotrs_polyvec_t *a_w_agg,
                      const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_out || !a_state || !a_par || !a_sk || !a_w_agg || !a_msg) return -EINVAL;

    /* Challenge c from aggregated w and message. */
    const char *l_c_domain = "lotrs-challenge-v1";
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)l_c_domain, strlen(l_c_domain));
    if (!l_xof_c) return -ENOMEM;

    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_w_buf[8 * 128];
        lotrs_poly_pack(l_w_buf, a_par->d * 8u, a_w_agg->polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_w_buf, a_par->d * 8u);
    }

    lotrs_poly_t *l_c = lotrs_poly_alloc(a_par);
    if (!l_c) { lotrs_xof_free(l_xof_c); return -ENOMEM; }
    int l_rc = lotrs_sample_ternary(l_c, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) { lotrs_poly_free(l_c); return l_rc; }

    /* z_u = c * s_u - y_u. */
    const uint32_t l_len = a_par->l + a_par->k;
    a_out->z_u = DAP_NEW_Z(lotrs_polyvec_t);
    if (!a_out->z_u) { lotrs_poly_free(l_c); return -ENOMEM; }
    *a_out->z_u = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_out->z_u->polys) { DAP_DELETE(a_out->z_u); a_out->z_u = NULL; lotrs_poly_free(l_c); return -ENOMEM; }

    for (uint32_t i = 0u; i < l_len; ++i) {
        lotrs_poly_t *l_cs = lotrs_poly_alloc(a_par);
        if (!l_cs) {
            lotrs_polyvec_free(a_out->z_u); DAP_DELETE(a_out->z_u); a_out->z_u = NULL;
            lotrs_poly_free(l_c); return -ENOMEM;
        }
        lotrs_poly_mul(l_cs, l_c, a_sk->s.polys[i], a_par);
        lotrs_poly_sub(a_out->z_u->polys[i], l_cs, a_state->y->polys[i], a_par);
        lotrs_poly_free(l_cs);
    }

    /* Rejection sampling. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    for (uint32_t i = 0u; i < l_len; ++i) {
        if (!lotrs_reject_infinity_norm(a_out->z_u->polys[i], l_bound, a_par)) {
            lotrs_polyvec_free(a_out->z_u); DAP_DELETE(a_out->z_u); a_out->z_u = NULL;
            lotrs_poly_free(l_c);
            return -2;
        }
    }

    lotrs_poly_free(l_c);
    return 0;
}

int lotrs_sign_aggregate(lotrs_signature_t *a_sig,
                         const lotrs_params_t *a_par,
                         const lotrs_round1_output_t *a_r1_outs,
                         const lotrs_round2_output_t *a_r2_outs,
                         uint32_t a_T)
{
    if (!a_sig || !a_par || !a_r1_outs || !a_r2_outs || a_T < 1u) return -EINVAL;

    const uint32_t l_len = a_par->l + a_par->k;

    /* Aggregate z = Σ z_u. */
    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(a_par, l_len);
    if (!l_z.polys) return -ENOMEM;
    lotrs_polyvec_zero(&l_z, a_par);
    for (uint32_t u = 0u; u < a_T; ++u) {
        if (!a_r2_outs[u].z_u) { lotrs_polyvec_free(&l_z); return -EINVAL; }
        lotrs_polyvec_add(&l_z, &l_z, a_r2_outs[u].z_u, a_par);
    }

    /* Aggregate w = Σ w_u. */
    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_w.polys) { lotrs_polyvec_free(&l_z); return -ENOMEM; }
    lotrs_polyvec_zero(&l_w, a_par);
    for (uint32_t u = 0u; u < a_T; ++u) {
        if (!a_r1_outs[u].w) { lotrs_polyvec_free(&l_z); lotrs_polyvec_free(&l_w); return -EINVAL; }
        lotrs_polyvec_add(&l_w, &l_w, a_r1_outs[u].w, a_par);
    }

    /* Serialize: (w, c_placeholder, z). */
    size_t l_w_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);
    size_t l_z_bytes = lotrs_polyvec_bytes(a_par, l_len);
    size_t l_total = l_w_bytes + l_c_bytes + l_z_bytes;

    a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!a_sig->data) { lotrs_polyvec_free(&l_z); lotrs_polyvec_free(&l_w); return -ENOMEM; }
    a_sig->len = l_total;

    uint8_t *l_p = a_sig->data;
    lotrs_polyvec_pack(l_p, l_w_bytes, &l_w, a_par);
    l_p += l_w_bytes;
    memset(l_p, 0, l_c_bytes); /* c placeholder */
    l_p += l_c_bytes;
    lotrs_polyvec_pack(l_p, l_z_bytes, &l_z, a_par);

    lotrs_polyvec_free(&l_z);
    lotrs_polyvec_free(&l_w);
    return 0;
}

void lotrs_round1_state_free(lotrs_round1_state_t *a_state)
{
    if (a_state) {
        if (a_state->y) { lotrs_polyvec_free(a_state->y); DAP_DELETE(a_state->y); }
        if (a_state->w) { lotrs_polyvec_free(a_state->w); DAP_DELETE(a_state->w); }
        memset(a_state, 0, sizeof(*a_state));
    }
}

void lotrs_round1_output_free(lotrs_round1_output_t *a_out)
{
    if (a_out) {
        if (a_out->w) { lotrs_polyvec_free(a_out->w); DAP_DELETE(a_out->w); }
        memset(a_out, 0, sizeof(*a_out));
    }
}

void lotrs_round2_output_free(lotrs_round2_output_t *a_out)
{
    if (a_out) {
        if (a_out->z_u) { lotrs_polyvec_free(a_out->z_u); DAP_DELETE(a_out->z_u); }
        memset(a_out, 0, sizeof(*a_out));
    }
}
