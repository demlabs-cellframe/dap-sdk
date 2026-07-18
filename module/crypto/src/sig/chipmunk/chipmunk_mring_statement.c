/*
 * CR-11.G Phase 7.7 — MRNG statement layer (M3.1 / G2 v2 §A1, §A6).
 *
 * Implements vector-commitment generator derivation and the C_b
 * commitment used by the prover (REL-6) and recomputed by the verifier
 * inside the unified inner-product statement.  All polynomial primitives
 * are reused from chipmunk_poly_* and chipmunk_lrs_* — no self-rolled
 * NTT, sampler, or hash (gate G5).
 */

#include <errno.h>
#include <string.h>

#include "chipmunk_mring_statement.h"
#include "chipmunk_poly.h"
#include "dap_common.h"
#include "dap_hash_sha3.h"

#define LOG_TAG "chipmunk_mring_statement"

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/*
 * Sample a uniform R_q polynomial from (ring_hash, nonce) via
 * chipmunk_poly_uniform.  The Chipmunk uniform sampler is itself a
 * SHAKE128-based rejection sampler over [0, q); we use distinct nonces
 * per generator slot to give cryptographic domain separation among
 * (a, H'_0, ..., H'_{K_pk-1}).
 *
 * Nonce assignment:
 *   0           -> a              (projection of b)
 *   1..K_PK     -> H'_0..H'_{K_PK-1} (randomness lane)
 *
 * This mirrors the convention used by chipmunk_lrs_derive_A_pk /
 * chipmunk_lrs_derive_A_I (per-slot nonces over a shared seed).
 */
static int s_sample_uniform_poly(chipmunk_poly_t *a_poly,
                                 const uint8_t a_ring_hash[32],
                                 uint16_t a_nonce)
{
    if (!a_poly || !a_ring_hash) {
        return -EINVAL;
    }
    return chipmunk_poly_uniform(a_poly, a_ring_hash, a_nonce);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int chipmunk_mring_derive_vcom_generators(chipmunk_mring_vcom_gens_t *a_out,
                                          const uint8_t a_ring_hash[32])
{
    if (!a_out || !a_ring_hash) {
        return -EINVAL;
    }

    int rc = s_sample_uniform_poly(&a_out->a, a_ring_hash, /*nonce=*/0u);
    if (rc != 0) {
        log_it(L_ERROR,
               "MRNG vcom: failed to sample projection generator a (rc=%d)", rc);
        return rc;
    }

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        rc = s_sample_uniform_poly(&a_out->H_prime[j],
                                   a_ring_hash,
                                   /*nonce=*/(uint16_t)(j + 1u));
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG vcom: failed to sample H'_%u (rc=%d)",
                   (unsigned)j, rc);
            return rc;
        }
    }
    return 0;
}

int chipmunk_mring_vcom_pack_b(chipmunk_poly_t *a_b_poly,
                               const uint8_t *a_b_indicator,
                               uint32_t a_n_ring)
{
    if (!a_b_poly || !a_b_indicator) {
        return -EINVAL;
    }
    if (a_n_ring < CHIPMUNK_MRING_N_MIN || a_n_ring > CHIPMUNK_MRING_N_MAX) {
        return -EINVAL;
    }

    memset(a_b_poly, 0, sizeof(*a_b_poly));

    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        const uint8_t b_i = a_b_indicator[i];
        if (b_i > 1u) {
            log_it(L_ERROR,
                   "MRNG vcom: b[%u] = %u is not a bit (REL-1 violated)",
                   (unsigned)i, (unsigned)b_i);
            return -EINVAL;
        }
        a_b_poly->coeffs[i] = (int32_t)b_i;
    }
    /* The high (n - N) coefficients remain 0 (memset above). */
    return 0;
}

int chipmunk_mring_vcom_commit(chipmunk_poly_t *a_Cb,
                               const chipmunk_mring_vcom_gens_t *a_gens,
                               const chipmunk_poly_t *a_b_poly,
                               const chipmunk_poly_t a_r_b[CHIPMUNK_MRING_K_PK],
                               uint64_t q)
{
    if (!a_Cb || !a_gens || !a_b_poly || !a_r_b) {
        return -EINVAL;
    }

    /*
     * We compute  C_b = a · b + Σ_j H'_j · r_b[j]   in R_q.
     *
     * Strategy: take working copies of each operand, NTT them, multiply
     * pointwise inside the NTT domain, accumulate, then invNTT once.
     * This costs (1 + K_pk) NTT + (1 + K_pk) pointwise multiplications +
     * K_pk additions + 1 invNTT — minimal for this many summands.
     */

    chipmunk_poly_t a_ntt   = a_gens->a;
    chipmunk_poly_t b_ntt   = *a_b_poly;
    chipmunk_poly_t acc_ntt;
    chipmunk_poly_t tmp_ntt;

    int rc = chipmunk_poly_ntt(&a_ntt);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG vcom: NTT(a) failed (rc=%d)", rc);
        return rc;
    }
    rc = chipmunk_poly_ntt(&b_ntt);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG vcom: NTT(b) failed (rc=%d)", rc);
        return rc;
    }

    chipmunk_poly_mul_ntt_q(&acc_ntt, &a_ntt, &b_ntt, q);

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        chipmunk_poly_t hp_ntt = a_gens->H_prime[j];
        chipmunk_poly_t rb_ntt = a_r_b[j];

        rc = chipmunk_poly_ntt(&hp_ntt);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG vcom: NTT(H'_%u) failed (rc=%d)",
                   (unsigned)j, rc);
            return rc;
        }
        rc = chipmunk_poly_ntt(&rb_ntt);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG vcom: NTT(r_b[%u]) failed (rc=%d)",
                   (unsigned)j, rc);
            return rc;
        }

        chipmunk_poly_mul_ntt_q(&tmp_ntt, &hp_ntt, &rb_ntt, q);
        chipmunk_poly_add_ntt_q(&acc_ntt, &acc_ntt, &tmp_ntt, q);
    }

    *a_Cb = acc_ntt;
    rc = chipmunk_poly_invntt(a_Cb);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG vcom: invNTT(C_b) failed (rc=%d)", rc);
        return rc;
    }
    return 0;
}

int chipmunk_mring_vcom_open(chipmunk_poly_t *a_v_out,
                             const chipmunk_poly_t *a_C,
                             const chipmunk_mring_vcom_gens_t *a_gens,
                             const chipmunk_poly_t a_r[CHIPMUNK_MRING_K_PK],
                             uint64_t q)
{
    if (!a_v_out || !a_C || !a_gens || !a_r) {
        return -EINVAL;
    }

    chipmunk_poly_t h_part;
    memset(&h_part, 0, sizeof(h_part));

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        chipmunk_poly_t hp_ntt = a_gens->H_prime[j];
        chipmunk_poly_t rb_ntt = a_r[j];
        chipmunk_poly_t prod_ntt;

        int rc = chipmunk_poly_ntt(&hp_ntt);
        if (rc != 0) {
            return rc;
        }
        rc = chipmunk_poly_ntt(&rb_ntt);
        if (rc != 0) {
            return rc;
        }
        chipmunk_poly_mul_ntt_q(&prod_ntt, &hp_ntt, &rb_ntt, q);
        if (j == 0u) {
            h_part = prod_ntt;
        } else {
            chipmunk_poly_add_ntt_q(&h_part, &h_part, &prod_ntt, q);
        }
    }

    int rc = chipmunk_poly_invntt(&h_part);
    if (rc != 0) {
        return rc;
    }

    chipmunk_poly_t residual;
    rc = chipmunk_poly_sub_q(&residual, a_C, &h_part, q);
    if (rc != 0) {
        return rc;
    }

    chipmunk_poly_t a_inv;
    rc = chipmunk_mring_poly_invert_q(&a_inv, &a_gens->a, q);
    if (rc != 0) {
        return rc;
    }

    chipmunk_poly_t res_ntt = residual;
    rc = chipmunk_poly_ntt(&res_ntt);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_poly_ntt(&a_inv);
    if (rc != 0) {
        return rc;
    }
    chipmunk_poly_mul_ntt_q(a_v_out, &a_inv, &res_ntt, q);
    return chipmunk_poly_invntt(a_v_out);
}

int chipmunk_mring_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound,
                           uint64_t q)
{
    if (!a_poly || a_bound < 0) {
        return -EINVAL;
    }
    /*
     * chipmunk_lrs_poly_chknorm_centered uses the chipmunk convention
     * (0 = within bound, 1 = bound violation).  Translate to a POSIX-style
     * error so the MRNG layer can return -ERANGE consistently.
     */
    if (chipmunk_lrs_poly_chknorm_centered(a_poly, a_bound, q) != 0) {
        return -ERANGE;
    }
    return 0;
}

/* =========================================================================
 *  M3.2 — Unified inner-product statement (G2 v2.1 §3).
 * ====================================================================== */

uint32_t chipmunk_mring_augmented_dim(uint32_t a_n_ring)
{
    if (a_n_ring < CHIPMUNK_MRING_N_MIN || a_n_ring > CHIPMUNK_MRING_N_MAX) {
        return 0u;
    }
    return 2u * a_n_ring;
}

int chipmunk_mring_polyvec_alloc(chipmunk_mring_polyvec_t *a_vec,
                                 uint32_t a_length)
{
    if (!a_vec || a_length == 0u) {
        return -EINVAL;
    }
    a_vec->slots = DAP_NEW_Z_COUNT(chipmunk_poly_t, a_length);
    if (!a_vec->slots) {
        a_vec->length = 0u;
        return -ENOMEM;
    }
    a_vec->length = a_length;
    return 0;
}

void chipmunk_mring_polyvec_free(chipmunk_mring_polyvec_t *a_vec)
{
    if (!a_vec) {
        return;
    }
    if (a_vec->slots) {
        DAP_DELETE(a_vec->slots);
    }
    a_vec->slots = NULL;
    a_vec->length = 0u;
}

int chipmunk_mring_augment_witness(chipmunk_mring_polyvec_t *a_b_tilde,
                                   const uint8_t *a_b_indicator,
                                   uint32_t a_n_ring)
{
    if (!a_b_tilde || !a_b_indicator || !a_b_tilde->slots) {
        return -EINVAL;
    }
    const uint32_t l_expected = chipmunk_mring_augmented_dim(a_n_ring);
    if (l_expected == 0u || a_b_tilde->length != l_expected) {
        return -EINVAL;
    }

    /* First N slots: b_i as a degree-0 polynomial.
     * Second N slots: b_i(b_i − 1) as a degree-0 polynomial.
     * For honest b_i ∈ {0,1}, b_i(b_i−1) = 0.  For dishonest b_i ∉ {0,1},
     * the non-zero value is caught algebraically by the fold verifier's
     * inner-product check against c²·(b_i²−b_i). */
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        const uint8_t b_i = a_b_indicator[i];
        if (b_i > 1u) {
            log_it(L_ERROR,
                   "MRNG augment: b[%u] = %u not a bit (REL-1)",
                   (unsigned)i, (unsigned)b_i);
            return -EINVAL;
        }
        memset(&a_b_tilde->slots[i], 0, sizeof(chipmunk_poly_t));
        a_b_tilde->slots[i].coeffs[0] = (int32_t)b_i;
        memset(&a_b_tilde->slots[a_n_ring + i], 0, sizeof(chipmunk_poly_t));
        a_b_tilde->slots[a_n_ring + i].coeffs[0] =
            (int32_t)b_i * ((int32_t)b_i - 1);
    }
    return 0;
}

/*
 * Multiply two polynomials in the time domain, returning a time-domain
 * result via NTT-pointwise-invNTT.  Inputs are NOT mutated.
 */
static int s_poly_mul_time(chipmunk_poly_t *a_out,
                           const chipmunk_poly_t *a_left,
                           const chipmunk_poly_t *a_right,
                           uint64_t q)
{
    chipmunk_poly_t l = *a_left;
    chipmunk_poly_t r = *a_right;
    int rc = chipmunk_poly_ntt(&l);
    if (rc != 0) return rc;
    rc = chipmunk_poly_ntt(&r);
    if (rc != 0) return rc;
    chipmunk_poly_mul_ntt_q(a_out, &l, &r, q);
    return chipmunk_poly_invntt(a_out);
}

/* NTT-native variant: left operand already in NTT domain, right operand is
 * forward-NTT'd internally. Output is in time domain (invNTT applied).
 * Saves one forward NTT of the reused left operand per call. */
static int s_poly_mul_ntt_lhs(chipmunk_poly_t *a_out,
                              const chipmunk_poly_t *a_left_ntt,
                              const chipmunk_poly_t *a_right,
                              uint64_t q)
{
    chipmunk_poly_t r = *a_right;
    int rc = chipmunk_poly_ntt(&r);
    if (rc != 0) return rc;
    chipmunk_poly_mul_ntt_q(a_out, a_left_ntt, &r, q);
    return chipmunk_poly_invntt(a_out);
}

int chipmunk_mring_eval_public_P(chipmunk_mring_polyvec_t *a_P_tilde,
                                 const chipmunk_poly_t *a_c,
                                 const chipmunk_poly_t *a_pks,
                                 uint32_t a_n_ring,
                                 uint64_t q)
{
    if (!a_P_tilde || !a_c || !a_pks || !a_P_tilde->slots) {
        return -EINVAL;
    }
    const uint32_t l_expected = chipmunk_mring_augmented_dim(a_n_ring);
    if (l_expected == 0u || a_P_tilde->length != l_expected) {
        return -EINVAL;
    }

    /* Pre-compute c² and c³ once. */
    chipmunk_poly_t c2, c3;
    int rc = s_poly_mul_time(&c2, a_c, a_c, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG eval_P: c² compute failed (rc=%d)", rc);
        return rc;
    }
    rc = s_poly_mul_time(&c3, &c2, a_c, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG eval_P: c³ compute failed (rc=%d)", rc);
        return rc;
    }

    /* Pre-NTT c³ for reuse across all ring members. */
    chipmunk_poly_t c3_ntt = c3;
    rc = chipmunk_poly_ntt(&c3_ntt);
    if (rc != 0) return rc;

    /* Lower half: P̃[i] = c + c³ · pk_i. */
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        chipmunk_poly_t tmp;
        rc = s_poly_mul_ntt_lhs(&tmp, &c3_ntt, &a_pks[i], q);
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG eval_P: c³·pk[%u] compute failed (rc=%d)",
                   (unsigned)i, rc);
            return rc;
        }
        rc = chipmunk_poly_add_q(&a_P_tilde->slots[i], a_c, &tmp, q);
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG eval_P: P̃[%u] add failed (rc=%d)",
                   (unsigned)i, rc);
            return rc;
        }
    }
    /* Upper half: P̃[N+i] = c² (constant across i). */
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        a_P_tilde->slots[a_n_ring + i] = c2;
    }
    return 0;
}

int chipmunk_mring_eval_public_rho_q(chipmunk_poly_t *a_rho,
                                     const chipmunk_poly_t *a_c,
                                     uint32_t a_t,
                                     const chipmunk_poly_t *a_Y_pk,
                                     uint64_t q)
{
    if (!a_rho || !a_c || !a_Y_pk) {
        return -EINVAL;
    }
    if (a_t == 0u || a_t > CHIPMUNK_MRING_T_MAX) {
        return -EINVAL;
    }

    /* term1 = c · t   (scalar mul; t < q by static assert in params.h) */
    chipmunk_poly_t term1;
    for (uint32_t k = 0u; k < CHIPMUNK_N; ++k) {
        /* The product (coeff · t) easily fits in int64 for our params;
         * we then reduce modulo q to keep coefficients balanced.  The
         * NTT layer expects coefficients in (−q, q); we centre into
         * [−q/2, q/2] for canonical form. */
        const int64_t l_prod = (int64_t)a_c->coeffs[k] * (int64_t)a_t;
        int64_t l_red = l_prod % (int64_t)q;
        if (l_red >  (int64_t)(q / 2)) l_red -= (int64_t)q;
        if (l_red <= -(int64_t)(q / 2)) l_red += (int64_t)q;
        term1.coeffs[k] = (int32_t)l_red;
    }

    /* term2 = c³ · Y_pk */
    chipmunk_poly_t c2, c3, term2;
    int rc = s_poly_mul_time(&c2, a_c, a_c, q);
    if (rc != 0) return rc;
    rc = s_poly_mul_time(&c3, &c2, a_c, q);
    if (rc != 0) return rc;
    rc = s_poly_mul_time(&term2, &c3, a_Y_pk, q);
    if (rc != 0) return rc;

    return chipmunk_poly_add_q(a_rho, &term1, &term2, q);
}

int chipmunk_mring_aggregate_X(chipmunk_poly_t a_X_out[CHIPMUNK_MRING_K_PK],
                               const uint8_t *a_b_indicator,
                               const chipmunk_poly_t *a_x_flat,
                               uint32_t a_n_ring,
                               uint64_t q)
{
    if (!a_X_out || !a_b_indicator || !a_x_flat) {
        return -EINVAL;
    }
    if (a_n_ring < CHIPMUNK_MRING_N_MIN || a_n_ring > CHIPMUNK_MRING_N_MAX) {
        return -EINVAL;
    }

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        memset(&a_X_out[j], 0, sizeof(chipmunk_poly_t));
    }

    /*
     * CT-safe aggregation: always iterate over all ring members and always
     * perform the polynomial addition.  The contribution is zeroed when
     * b_i == 0 via a constant-time mask (no secret-dependent branch).
     *
     * m = (uint32_t)(-b_i)  →  0x00000000 when b_i=0, 0xFFFFFFFF when b_i=1.
     * We apply m coefficient-wise: tmp.coeffs[k] = x_flat.coeffs[k] & m.
     */
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        const uint8_t b_i = a_b_indicator[i];
        if (b_i > 1u) {
            log_it(L_ERROR,
                   "MRNG aggregate_X: b[%u] = %u not a bit (REL-1)",
                   (unsigned)i, (unsigned)b_i);
            return -EINVAL;
        }
        const uint32_t l_mask = (uint32_t)(-(int32_t)b_i);
        for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
            chipmunk_poly_t l_tmp;
            const chipmunk_poly_t *l_src =
                &a_x_flat[i * CHIPMUNK_MRING_K_PK + j];
            for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
                l_tmp.coeffs[k] = (int32_t)((uint32_t)l_src->coeffs[k] & l_mask);
            }
            const int rc = chipmunk_poly_add_q(&a_X_out[j], &a_X_out[j], &l_tmp, q);
            if (rc != 0) {
                log_it(L_ERROR,
                       "MRNG aggregate_X: poly_add (i=%u, j=%u) failed (rc=%d)",
                       (unsigned)i, (unsigned)j, rc);
                return rc;
            }
        }
    }
    return 0;
}

/* =========================================================================
 *  M4.0a — R_q inversion (pointwise modular inverse in the NTT domain).
 * ====================================================================== */

/*
 * Modular inverse of a ∈ [0, q) modulo the Chipmunk prime q via the
 * extended Euclidean algorithm.  Returns the inverse in [1, q), or -1 if
 * a == 0 (or, defensively, if gcd(a, q) ≠ 1 — impossible for prime q and
 * a ≠ 0, but checked anyway).  Not constant-time: only used on PUBLIC
 * Fiat-Shamir challenges.
 */
static int32_t s_modinv_q(int32_t a_val, uint64_t q)
{
    if (a_val <= 0) {
        return -1;
    }
    int64_t l_t = 0, l_newt = 1;
    int64_t l_r = (int64_t)q, l_newr = (int64_t)a_val;
    while (l_newr != 0) {
        int64_t l_quot = l_r / l_newr;
        int64_t l_tmp = l_t - l_quot * l_newt;
        l_t = l_newt;
        l_newt = l_tmp;
        l_tmp = l_r - l_quot * l_newr;
        l_r = l_newr;
        l_newr = l_tmp;
    }
    if (l_r != 1) {
        return -1; /* not invertible */
    }
    if (l_t < 0) {
        l_t += (int64_t)q;
    }
    return (int32_t)l_t;
}

int chipmunk_mring_poly_invert_q(chipmunk_poly_t *a_inv_out,
                                 const chipmunk_poly_t *a_x,
                                 uint64_t q)
{
    if (!a_inv_out || !a_x) {
        return -EINVAL;
    }
    chipmunk_poly_t l_xn = *a_x;
    int rc = chipmunk_poly_ntt(&l_xn);
    if (rc != 0) {
        return rc;
    }
    chipmunk_poly_t l_invn;
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_v = l_xn.coeffs[i] % (int32_t)q;
        if (l_v < 0) {
            l_v += (int32_t)q;
        }
        const int32_t l_iv = s_modinv_q(l_v, q);
        if (l_iv < 0) {
            /* Some NTT coordinate is zero ⇒ x is not invertible in R_q. */
            return -EDOM;
        }
        l_invn.coeffs[i] = l_iv;
    }
    *a_inv_out = l_invn;
    return chipmunk_poly_invntt(a_inv_out);
}

/* =========================================================================
 *  M3.3 — Bind-block helpers (G2 v2.1 §4).
 * ====================================================================== */

int chipmunk_mring_derive_A_T(chipmunk_poly_t a_A_T_out[CHIPMUNK_MRING_K_PK],
                              const uint8_t a_ring_hash[32],
                              const uint8_t a_ctx_hash[32])
{
    if (!a_A_T_out || !a_ring_hash || !a_ctx_hash) {
        return -EINVAL;
    }
    /*
     * Hash a domain-separator with (ring_hash || ctx_hash) into a 32-byte
     * seed, then use chipmunk_poly_uniform with per-slot nonces — the
     * same pattern as chipmunk_mring_derive_vcom_generators but bound
     * to (ring, ctx) instead of just (ring).
     */
    static const char DOMAIN[] = "chipmunk-mring-AT-v1";
    uint8_t l_input[sizeof(DOMAIN) - 1u + 32u + 32u];
    memcpy(l_input, DOMAIN, sizeof(DOMAIN) - 1u);
    memcpy(l_input + sizeof(DOMAIN) - 1u, a_ring_hash, 32u);
    memcpy(l_input + sizeof(DOMAIN) - 1u + 32u, a_ctx_hash, 32u);

    dap_hash_sha3_256_t l_seed = { 0 };
    if (!dap_hash_sha3_256(l_input, sizeof(l_input), &l_seed)) {
        return -EIO;
    }

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        const int rc =
            chipmunk_poly_uniform(&a_A_T_out[j], l_seed.raw, (uint16_t)j);
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG derive_A_T: slot %u sample failed (rc=%d)",
                   (unsigned)j, rc);
            return rc;
        }
    }
    return 0;
}

int chipmunk_mring_bind_mask_sample(chipmunk_poly_t a_rho_x_out[CHIPMUNK_MRING_K_PK],
                                    const uint8_t a_seed[32],
                                    uint32_t a_attempt)
{
    if (!a_rho_x_out || !a_seed) {
        return -EINVAL;
    }
    /*
     * Reuse chipmunk_lrs_h_to_bounded_poly: it accepts an arbitrary
     * seed-material buffer (so we can mix in the attempt counter as a
     * domain separator) and an explicit bound.  MASK_BOUND = 524 769
     * gives the LRS bounded-uniform mask.
     */
    uint8_t l_seed_material[32u + sizeof(uint32_t)];
    memcpy(l_seed_material, a_seed, 32u);
    /* Little-endian attempt counter. */
    l_seed_material[32] = (uint8_t)(a_attempt & 0xFFu);
    l_seed_material[33] = (uint8_t)((a_attempt >> 8)  & 0xFFu);
    l_seed_material[34] = (uint8_t)((a_attempt >> 16) & 0xFFu);
    l_seed_material[35] = (uint8_t)((a_attempt >> 24) & 0xFFu);

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        const int rc =
            chipmunk_lrs_h_to_bounded_poly(&a_rho_x_out[j],
                                           "mring-bind-mask-v1",
                                           CHIPMUNK_LRS_PARAMS_C0,
                                           l_seed_material,
                                           sizeof(l_seed_material),
                                           /*index=*/j,
                                           CHIPMUNK_MRING_MASK_BOUND);
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG bind_mask_sample: slot %u sample failed (rc=%d)",
                   (unsigned)j, rc);
            return rc;
        }
    }
    return 0;
}

int chipmunk_mring_bind_prove_z_x(chipmunk_poly_t a_z_x_out[CHIPMUNK_MRING_K_PK],
                                  const chipmunk_poly_t a_rho_x[CHIPMUNK_MRING_K_PK],
                                  const chipmunk_poly_t *a_c_star,
                                  const chipmunk_poly_t a_X[CHIPMUNK_MRING_K_PK],
                                  uint64_t q)
{
    if (!a_z_x_out || !a_rho_x || !a_c_star || !a_X) {
        return -EINVAL;
    }
    /*
     * For each j ∈ [0, K_pk):
     *   z_x[j] = ρ_x[j] + c*·X[j]   (R_q multiplication via NTT)
     *
     * After assembly, check ‖z_x‖∞ < RESPONSE_BOUND; if any coefficient
     * is out of range return -EAGAIN so the caller can resample ρ_x.
     */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        chipmunk_poly_t l_prod;
        const int rc_mul = s_poly_mul_time(&l_prod, a_c_star, &a_X[j], q);
        if (rc_mul != 0) {
            log_it(L_ERROR,
                   "MRNG bind_prove: c*·X[%u] failed (rc=%d)",
                   (unsigned)j, rc_mul);
            return rc_mul;
        }
        const int rc_add = chipmunk_poly_add_q(&a_z_x_out[j], &a_rho_x[j], &l_prod, q);
        if (rc_add != 0) {
            log_it(L_ERROR,
                   "MRNG bind_prove: z_x[%u] add failed (rc=%d)",
                   (unsigned)j, rc_add);
            return rc_add;
        }
        /*
         * Norm check at the LRS-standard RESPONSE_BOUND (closed interval).
         * Mirrors the LRS sign-path acceptance gate so the bounded-
         * uniform abort statistical-distance proof transfers verbatim.
         */
        if (chipmunk_lrs_poly_chknorm_centered(
                &a_z_x_out[j], CHIPMUNK_MRING_RESPONSE_BOUND, q) != 0) {
            return -EAGAIN;
        }
    }
    return 0;
}

int chipmunk_mring_bind_verify_reconstruct(chipmunk_poly_t *a_M_pk_out,
                                           chipmunk_poly_t *a_M_T_out,
                                           const chipmunk_poly_t a_A_pk[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t a_A_T[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t a_z_x[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t *a_c_star,
                                           const chipmunk_poly_t *a_Y_pk,
                                           const chipmunk_poly_t *a_T,
                                           uint64_t q)
{
    if (!a_M_pk_out || !a_M_T_out || !a_A_pk || !a_A_T || !a_z_x ||
        !a_c_star || !a_Y_pk || !a_T) {
        return -EINVAL;
    }

    /* G2 v2 §A6: verifier recomputes Π_norm from unpacked z_x. */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        if (chipmunk_lrs_poly_chknorm_centered(
                &a_z_x[j], CHIPMUNK_MRING_RESPONSE_BOUND, q) != 0) {
            log_it(L_ERROR,
                   "MRNG bind_verify: ‖z_x[%u]‖∞ exceeds RESPONSE_BOUND",
                   (unsigned)j);
            return -ERANGE;
        }
    }

    /* M_pk := A_pk · z_x − c*·Y_pk. */
    chipmunk_poly_t l_lhs_pk, l_rhs_pk;
    int rc = chipmunk_lrs_relation_eval(&l_lhs_pk, a_A_pk, a_z_x, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: relation_eval(A_pk, z_x) failed (rc=%d)", rc);
        return rc;
    }
    rc = s_poly_mul_time(&l_rhs_pk, a_c_star, a_Y_pk, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: c*·Y_pk failed (rc=%d)", rc);
        return rc;
    }
    rc = chipmunk_poly_sub_q(a_M_pk_out, &l_lhs_pk, &l_rhs_pk, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: M_pk reconstruct sub failed (rc=%d)", rc);
        return rc;
    }

    /* M_T := A_T · z_x − c*·T. */
    chipmunk_poly_t l_lhs_T, l_rhs_T;
    rc = chipmunk_lrs_relation_eval(&l_lhs_T, a_A_T, a_z_x, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: relation_eval(A_T, z_x) failed (rc=%d)", rc);
        return rc;
    }
    rc = s_poly_mul_time(&l_rhs_T, a_c_star, a_T, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: c*·T failed (rc=%d)", rc);
        return rc;
    }
    rc = chipmunk_poly_sub_q(a_M_T_out, &l_lhs_T, &l_rhs_T, q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG bind_verify: M_T reconstruct sub failed (rc=%d)", rc);
        return rc;
    }
    return 0;
}

int chipmunk_mring_inner_product(chipmunk_poly_t *a_out,
                                 const chipmunk_mring_polyvec_t *a_b_tilde,
                                 const chipmunk_mring_polyvec_t *a_P_tilde,
                                 uint64_t q)
{
    if (!a_out || !a_b_tilde || !a_P_tilde) {
        return -EINVAL;
    }
    if (!a_b_tilde->slots || !a_P_tilde->slots) {
        return -EINVAL;
    }
    if (a_b_tilde->length != a_P_tilde->length || a_b_tilde->length == 0u) {
        return -EINVAL;
    }

    memset(a_out, 0, sizeof(chipmunk_poly_t));
    for (uint32_t k = 0u; k < a_b_tilde->length; ++k) {
        chipmunk_poly_t term;
        const int rc = s_poly_mul_time(&term,
                                       &a_b_tilde->slots[k],
                                       &a_P_tilde->slots[k], q);
        if (rc != 0) {
            log_it(L_ERROR,
                   "MRNG inner_product: term[%u] mul failed (rc=%d)",
                   (unsigned)k, rc);
            return rc;
        }
        const int rc_add = chipmunk_poly_add_q(a_out, a_out, &term, q);
        if (rc_add != 0) {
            log_it(L_ERROR,
                   "MRNG inner_product: term[%u] add failed (rc=%d)",
                   (unsigned)k, rc_add);
            return rc_add;
        }
    }
    return 0;
}
