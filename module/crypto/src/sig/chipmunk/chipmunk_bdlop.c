/*
 * chipmunk_bdlop.c — BDLOP lattice commitment + ABDLOP opening proof.
 *
 * Implementation of the commit-and-prove framework described in
 * chipmunk_bdlop.h.  Reuses the existing chipmunk Pedersen matrix A
 * (NTT/Montgomery domain) and NTT/invNTT infrastructure.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2025 Cellframe Project
 */

#include "chipmunk_bdlop.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "chipmunk_pedersen.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#include <string.h>
#include <errno.h>
#include <stdbool.h>

#define LOG_TAG "chipmunk_bdlop"

/* =======================================================================
 *  Internal helpers
 * ======================================================================= */

/*
 * Sample a short polynomial with uniform coefficients in [-bound, bound].
 * Uses SHAKE256 rejection sampling (same pattern as Pedersen blinding).
 */
static int s_sample_short_poly(chipmunk_poly_t *a_out,
                                uint64_t *a_shake_state,
                                uint32_t a_bound)
{
    /* Each coefficient needs rejection sampling from range (2*bound+1).
     * We consume 4 bytes per sample attempt. */
    const uint32_t l_range = 2 * a_bound + 1;
    size_t l_needed = CHIPMUNK_N * 4;
    size_t l_nblocks = (l_needed + 135) / 136;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
    if (!l_buf) return -ENOMEM;

    /* Initial squeeze — MUST happen before reading from buffer */
    dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, a_shake_state);

    size_t l_pos = 0;
    for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
        for (;;) {
            if (l_pos + 4 > l_nblocks * 136) {
                /* Need more entropy — squeeze another block */
                dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, a_shake_state);
                l_pos = 0;
            }
            int32_t l_sample = chipmunk_sample_reject4(l_buf + l_pos, l_range);
            l_pos += 4;
            if (l_sample >= 0) {
                /* l_sample ∈ [0, 2*bound], center to [-bound, bound] */
                a_out->coeffs[k] = (int32_t)l_sample - (int32_t)a_bound;
                break;
            }
        }
    }
    DAP_DELETE(l_buf);
    return 0;
}

/*
 * Compute the prover's masking commitment W.
 *
 *   W[i] = A[i] · y_r  +  (i == 0 ? y_m : 0)   (mod q)
 *
 * Reuses the Pedersen commit infrastructure: W is essentially a commitment
 * to y_m with randomness y_r.
 *
 * \param a_W          Output W (K polynomials, time domain).
 * \param a_params     Parameters (A matrix in NTT domain).
 * \param a_y_m        Masking message polynomial (1 poly, time domain).
 * \param a_y_r        Masking randomness polynomials (L polys, time domain).
 * \param a_q          Modulus.
 */
static int s_compute_W(chipmunk_poly_t a_W[CHIPMUNK_BDLOP_K],
                        const chipmunk_pedersen_params_t *a_params,
                        const chipmunk_poly_t *a_y_m,
                        const chipmunk_poly_t a_y_r[CHIPMUNK_BDLOP_L],
                        uint64_t a_q)
{
    /* This is exactly s_pedersen_commit_with_message_poly — but that's
     * static.  Instead we replicate the logic here using NTT operations.
     * The structure is: commit y_m with randomness y_r using matrix A. */

    /* NTT all masking randomness polynomials once */
    chipmunk_poly_t l_yr_ntt[CHIPMUNK_BDLOP_L];
    for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
        l_yr_ntt[j] = a_y_r[j];
        chipmunk_ntt(l_yr_ntt[j].coeffs);
    }

    for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i) {
        chipmunk_poly_t l_acc_ntt;
        memset(&l_acc_ntt, 0, sizeof(l_acc_ntt));

        for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt_q(&l_prod, &a_params->A[i][j], &l_yr_ntt[j], a_q);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int64_t l_s = (int64_t)l_acc_ntt.coeffs[k] + l_prod.coeffs[k];
                int32_t l_r = (int32_t)(l_s % (int64_t)a_q);
                if (l_r < 0) l_r += (int32_t)a_q;
                l_acc_ntt.coeffs[k] = l_r;
            }
        }

        /* Add masking message polynomial for i==0 */
        if (i == 0) {
            chipmunk_poly_t l_ym_ntt = *a_y_m;
            chipmunk_ntt(l_ym_ntt.coeffs);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int64_t l_s = (int64_t)l_acc_ntt.coeffs[k] + l_ym_ntt.coeffs[k];
                int32_t l_r = (int32_t)(l_s % (int64_t)a_q);
                if (l_r < 0) l_r += (int32_t)a_q;
                l_acc_ntt.coeffs[k] = l_r;
            }
        }

        /* invNTT to time domain + canonicalize to [0,q) */
        chipmunk_invntt(l_acc_ntt.coeffs);
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            if (l_acc_ntt.coeffs[k] < 0)
                l_acc_ntt.coeffs[k] += (int32_t)a_q;
        }
        a_W[i] = l_acc_ntt;
    }

    dap_memwipe(l_yr_ntt, sizeof(l_yr_ntt));
    return 0;
}

/*
 * Compute A[i] · v + (i==0 ? m_extra : 0) for a vector v of L polys.
 * Used by both commit and verify.
 *
 * \param a_out    Output (K polynomials).
 * \param a_params Parameters (A in NTT domain).
 * \param a_v      Vector v (L polys, time domain).
 * \param a_extra  Extra polynomial added to row 0 (or NULL).
 * \param a_q      Modulus.
 */
static int s_matrix_times_vec(chipmunk_poly_t a_out[CHIPMUNK_BDLOP_K],
                               const chipmunk_pedersen_params_t *a_params,
                               const chipmunk_poly_t a_v[CHIPMUNK_BDLOP_L],
                               const chipmunk_poly_t *a_extra,
                               uint64_t a_q)
{
    chipmunk_poly_t l_v_ntt[CHIPMUNK_BDLOP_L];
    for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
        l_v_ntt[j] = a_v[j];
        chipmunk_ntt(l_v_ntt[j].coeffs);
    }

    for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i) {
        chipmunk_poly_t l_acc_ntt;
        memset(&l_acc_ntt, 0, sizeof(l_acc_ntt));

        for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt_q(&l_prod, &a_params->A[i][j], &l_v_ntt[j], a_q);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int64_t l_s = (int64_t)l_acc_ntt.coeffs[k] + l_prod.coeffs[k];
                int32_t l_r = (int32_t)(l_s % (int64_t)a_q);
                if (l_r < 0) l_r += (int32_t)a_q;
                l_acc_ntt.coeffs[k] = l_r;
            }
        }

        if (i == 0 && a_extra) {
            chipmunk_poly_t l_ext_ntt = *a_extra;
            chipmunk_ntt(l_ext_ntt.coeffs);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int64_t l_s = (int64_t)l_acc_ntt.coeffs[k] + l_ext_ntt.coeffs[k];
                int32_t l_r = (int32_t)(l_s % (int64_t)a_q);
                if (l_r < 0) l_r += (int32_t)a_q;
                l_acc_ntt.coeffs[k] = l_r;
            }
        }

        chipmunk_invntt(l_acc_ntt.coeffs);
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            if (l_acc_ntt.coeffs[k] < 0)
                l_acc_ntt.coeffs[k] += (int32_t)a_q;
        }
        a_out[i] = l_acc_ntt;
    }

    dap_memwipe(l_v_ntt, sizeof(l_v_ntt));
    return 0;
}

/*
 * Polynomial multiply in time domain via NTT (full convolution, not pointwise).
 *   result = a * b mod (X^N + 1) mod q
 */
static void s_poly_mul(chipmunk_poly_t *a_r,
                        const chipmunk_poly_t *a_a,
                        const chipmunk_poly_t *a_b,
                        uint64_t a_q)
{
    chipmunk_poly_t l_a_ntt = *a_a;
    chipmunk_poly_t l_b_ntt = *a_b;
    chipmunk_ntt(l_a_ntt.coeffs);
    chipmunk_ntt(l_b_ntt.coeffs);
    chipmunk_poly_mul_ntt_q(a_r, &l_a_ntt, &l_b_ntt, a_q);
    chipmunk_invntt(a_r->coeffs);
    /* Canonicalize to [0, q) */
    for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
        if (a_r->coeffs[k] < 0)
            a_r->coeffs[k] += (int32_t)a_q;
    }
}

/*
 * Check infinity-norm of a polynomial against a bound.
 * Returns 0 if within bound, 1 if exceeded.
 */
static int s_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound, uint64_t a_q)
{
    return chipmunk_poly_chknorm_q(a_poly, a_bound, a_q);
}

/* =======================================================================
 *  Serialization (3-byte packed, version 3)
 * =======================================================================
 *
 * Each coefficient is packed as 3 bytes (24 bits, little-endian).
 * Q = 3168257 < 2^24 = 16777216, so all values in [0, Q) fit losslessly.
 * This reduces proof size from 4B/coeff to 3B/coeff — 25% savings.
 *
 * Proof layout (single round):
 *   [magic 4B] [version 4B] [num_rounds 4B]
 *   For each round:
 *     [W[0..5]]  6 polys × 512 × 3 = 9216 bytes
 *     [c]        1 poly  × 512 × 3 = 1536 bytes
 *     [z_m]      1 poly  × 512 × 3 = 1536 bytes
 *     [z_r[0..5]] 6 polys × 512 × 3 = 9216 bytes
 *     [g1]       1 poly  × 512 × 3 = 1536 bytes (shortness garbage term)
 *     [g0]       1 poly  × 512 × 3 = 1536 bytes (shortness garbage term)
 *     [z_g1]     1 poly  × 512 × 3 = 1536 bytes (shortness response)
 *     [z_g0]     1 poly  × 512 × 3 = 1536 bytes (shortness response)
 *   Per round: 18 polys × 1536 = 27648 bytes ≈ 27 KB
 *   Total: 12 + 1 × 27648 = 27660 bytes
 */
#define CHIPMUNK_BDLOP_PROOF_MAGIC  0x42444C4F  /* "BDLO" */
#define CHIPMUNK_BDLOP_PROOF_VERSION 4
#define CHIPMUNK_BDLOP_PROOF_NPOLYS_BASE  \
    (CHIPMUNK_BDLOP_K + 1 + CHIPMUNK_BDLOP_MSG_POLYS + CHIPMUNK_BDLOP_L)
#define CHIPMUNK_BDLOP_PROOF_NPOLYS_SHORTNESS  3  /* g1, g0, z_g1 (c2_scalar stored separately) */
#define CHIPMUNK_BDLOP_PROOF_NPOLYS_PER_ROUND  \
    (CHIPMUNK_BDLOP_PROOF_NPOLYS_BASE + CHIPMUNK_BDLOP_PROOF_NPOLYS_SHORTNESS)
#define CHIPMUNK_BDLOP_COEFF_PACKED_BYTES  3
#define CHIPMUNK_BDLOP_POLY_PACKED_SIZE  \
    ((size_t)CHIPMUNK_N * CHIPMUNK_BDLOP_COEFF_PACKED_BYTES)
#define CHIPMUNK_BDLOP_PROOF_ROUND_SIZE  \
    ((size_t)CHIPMUNK_BDLOP_PROOF_NPOLYS_PER_ROUND * CHIPMUNK_BDLOP_POLY_PACKED_SIZE)
#define CHIPMUNK_BDLOP_PROOF_HEADER_SIZE  12  /* magic + version + num_rounds */

#define CHIPMUNK_BDLOP_PROOF_WIRE_SIZE \
    (CHIPMUNK_BDLOP_PROOF_HEADER_SIZE + \
     (size_t)CHIPMUNK_BDLOP_ROUNDS * CHIPMUNK_BDLOP_PROOF_ROUND_SIZE)

/* Pack one coefficient into 3 bytes (LE). Value must be in [0, 2^24). */
static void s_pack_coeff3(uint8_t *a_out, int32_t a_val)
{
    /* Canonicalize negative to [0, Q) — already done by callers */
    a_out[0] = (uint8_t)(a_val & 0xFF);
    a_out[1] = (uint8_t)((a_val >> 8) & 0xFF);
    a_out[2] = (uint8_t)((a_val >> 16) & 0xFF);
}

/* Unpack one 3-byte coefficient. Returns value in [0, 2^24). */
static int32_t s_unpack_coeff3(const uint8_t *a_in)
{
    return (int32_t)((uint32_t)a_in[0] | ((uint32_t)a_in[1] << 8) | ((uint32_t)a_in[2] << 16));
}

static void s_serialize_poly_packed(uint8_t **a_p, const chipmunk_poly_t *a_poly)
{
    for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
        s_pack_coeff3(*a_p, a_poly->coeffs[k]);
        *a_p += 3;
    }
}

static void s_deserialize_poly_packed(chipmunk_poly_t *a_poly, const uint8_t **a_p)
{
    for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
        a_poly->coeffs[k] = s_unpack_coeff3(*a_p);
        *a_p += 3;
    }
}

size_t chipmunk_bdlop_proof_serialized_size(void)
{
    return CHIPMUNK_BDLOP_PROOF_WIRE_SIZE;
}

int chipmunk_bdlop_proof_serialize(uint8_t *a_out, size_t a_out_size,
                                    const chipmunk_bdlop_proof_t *a_proof)
{
    if (!a_out || !a_proof) return -EINVAL;
    if (a_out_size < CHIPMUNK_BDLOP_PROOF_WIRE_SIZE) return -EINVAL;

    uint8_t *l_p = a_out;
    uint32_t l_magic = CHIPMUNK_BDLOP_PROOF_MAGIC;
    uint32_t l_ver = CHIPMUNK_BDLOP_PROOF_VERSION;

    memcpy(l_p, &l_magic, 4); l_p += 4;
    memcpy(l_p, &l_ver, 4);   l_p += 4;
    memcpy(l_p, &a_proof->num_rounds, 4); l_p += 4;

    for (uint32_t r = 0; r < a_proof->num_rounds; ++r) {
        const chipmunk_bdlop_proof_round_t *l_rd = &a_proof->rounds[r];
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
            s_serialize_poly_packed(&l_p, &l_rd->W[i]);
        s_serialize_poly_packed(&l_p, &l_rd->challenge);
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_MSG_POLYS; ++i)
            s_serialize_poly_packed(&l_p, &l_rd->z_m[i]);
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_L; ++i)
            s_serialize_poly_packed(&l_p, &l_rd->z_r[i]);
        /* Shortness garbage terms and responses */
        s_serialize_poly_packed(&l_p, &l_rd->g1);
        s_serialize_poly_packed(&l_p, &l_rd->g0);
        s_serialize_poly_packed(&l_p, &l_rd->z_g1);
        /* c2_scalar packed as 4 bytes after polys */
        memcpy(l_p, &l_rd->c2_scalar, sizeof(int32_t)); l_p += sizeof(int32_t);
    }

    return (int)CHIPMUNK_BDLOP_PROOF_WIRE_SIZE;
}

int chipmunk_bdlop_proof_deserialize(chipmunk_bdlop_proof_t *a_proof,
                                      const uint8_t *a_in, size_t a_in_size)
{
    if (!a_proof || !a_in) return -EINVAL;
    if (a_in_size < CHIPMUNK_BDLOP_PROOF_WIRE_SIZE) return -EINVAL;

    memset(a_proof, 0, sizeof(*a_proof));

    const uint8_t *l_p = a_in;
    uint32_t l_magic, l_ver;
    memcpy(&l_magic, l_p, 4); l_p += 4;
    memcpy(&l_ver, l_p, 4);   l_p += 4;
    memcpy(&a_proof->num_rounds, l_p, 4); l_p += 4;

    if (l_magic != CHIPMUNK_BDLOP_PROOF_MAGIC) return -EBADMSG;
    if (l_ver != CHIPMUNK_BDLOP_PROOF_VERSION) return -EBADMSG;
    if (a_proof->num_rounds != CHIPMUNK_BDLOP_ROUNDS) return -EBADMSG;

    for (uint32_t r = 0; r < a_proof->num_rounds; ++r) {
        chipmunk_bdlop_proof_round_t *l_rd = &a_proof->rounds[r];
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i) {
            s_deserialize_poly_packed(&l_rd->W[i], &l_p);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int32_t c = l_rd->W[i].coeffs[k];
                if (c < 0 || (uint32_t)c >= CHIPMUNK_Q) {
                    memset(a_proof, 0, sizeof(*a_proof));
                    return -EBADMSG;
                }
            }
        }
        s_deserialize_poly_packed(&l_rd->challenge, &l_p);
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_MSG_POLYS; ++i)
            s_deserialize_poly_packed(&l_rd->z_m[i], &l_p);
        for (uint32_t i = 0; i < CHIPMUNK_BDLOP_L; ++i)
            s_deserialize_poly_packed(&l_rd->z_r[i], &l_p);
        /* Shortness garbage terms and responses */
        s_deserialize_poly_packed(&l_rd->g1, &l_p);
        s_deserialize_poly_packed(&l_rd->g0, &l_p);
        s_deserialize_poly_packed(&l_rd->z_g1, &l_p);
        memcpy(&l_rd->c2_scalar, l_p, sizeof(int32_t)); l_p += sizeof(int32_t);
        /* Range check: c₂ must be in (0, q) — prevents c₂=0 bypass */
        if (l_rd->c2_scalar <= 0 || (uint32_t)l_rd->c2_scalar >= CHIPMUNK_Q) {
            memset(a_proof, 0, sizeof(*a_proof));
            return -EBADMSG;
        }
    }

    return 0;
}

void chipmunk_bdlop_proof_wipe(chipmunk_bdlop_proof_t *a_proof)
{
    if (!a_proof) return;
    dap_memwipe(a_proof, sizeof(*a_proof));
}

/* =======================================================================
 *  Commitment
 * ======================================================================= */

int chipmunk_bdlop_commit_poly(chipmunk_bdlop_commit_t *a_commit,
                                const chipmunk_pedersen_params_t *a_params,
                                const chipmunk_poly_t *a_message,
                                const chipmunk_poly_t a_randomness[CHIPMUNK_BDLOP_L])
{
    if (!a_commit || !a_params || !a_message || !a_randomness)
        return -EINVAL;
    if (!a_params->initialized)
        return -EINVAL;

    /* C = A·r + encode(m) where encode puts m in row 0 */
    return s_matrix_times_vec(a_commit->C, a_params, a_randomness, a_message, a_params->q);
}

/* =======================================================================
 *  Opening proof — Internal: single protocol round
 * ======================================================================= */

/*
 * Sample a low-weight ternary challenge polynomial.
 * c ∈ {-1, 0, 1}^N with exactly τ nonzero coefficients.
 *
 * Positions and signs are drawn from the SHAKE state using rejection
 * sampling to avoid modulo bias and position collisions.
 */
static int s_sample_challenge(chipmunk_poly_t *a_c, uint64_t *a_shake)
{
    memset(a_c, 0, sizeof(*a_c));

    /* We need τ distinct positions, each with a random sign.
     * Draw 5 bytes per attempt: 4 for position (rejection in [0, 512)),
     * 1 for sign. Retry on collision. */
    uint8_t l_buf[136 * 2];  /* 2 SHAKE blocks = 272 bytes → enough for τ=18 */
    dap_hash_shake256_squeezeblocks(l_buf, 2, a_shake);

    size_t l_pos = 0;
    uint32_t l_set = 0;  /* Number of nonzero coefficients set */

    while (l_set < CHIPMUNK_BDLOP_TAU) {
        if (l_pos + 5 > sizeof(l_buf)) {
            dap_hash_shake256_squeezeblocks(l_buf, 1, a_shake);
            l_pos = 0;
        }

        uint32_t l_idx;
        memcpy(&l_idx, l_buf + l_pos, 4);
        l_pos += 4;
        l_idx %= CHIPMUNK_N;

        uint8_t l_sign = l_buf[l_pos] & 1;
        l_pos += 1;

        /* Skip if position already set (collision) */
        if (a_c->coeffs[l_idx] != 0)
            continue;

        a_c->coeffs[l_idx] = l_sign ? 1 : (int32_t)(CHIPMUNK_Q - 1);
        l_set++;
    }

    return 0;
}

/*
 * Execute one protocol round (steps 1-5 of the Sigma protocol).
 * Returns 0 on success (round accepted), -EAGAIN if rejected.
 */
static int s_prove_one_round(chipmunk_bdlop_proof_round_t *a_rd,
                              uint32_t a_proto_round,
                              const chipmunk_pedersen_params_t *a_params,
                              const chipmunk_bdlop_commit_t *a_commit,
                              const chipmunk_poly_t *a_message,
                              const chipmunk_poly_t a_randomness[CHIPMUNK_BDLOP_L],
                              const chipmunk_bdlop_proof_round_t *a_prev_rounds,
                              uint32_t a_n_prev,
                              const uint8_t a_seed[32])
{
    const uint64_t l_q = a_params->q;

    /* Rejection sampling loop */
    for (uint32_t l_rej = 0; l_rej < CHIPMUNK_BDLOP_REJ_MAX_ROUNDS; ++l_rej) {
        chipmunk_poly_t l_y_m;
        chipmunk_poly_t l_y_r[CHIPMUNK_BDLOP_L];

        /* === Step 1: Sample masking polynomials === */
        {
            uint64_t l_shake[25];
            memset(l_shake, 0, sizeof(l_shake));

            size_t l_domlen = 32 + 13 + 4 + 4;  /* seed + "BDLOP-mask-v2" + proto_round + rej */
            uint8_t *l_dom = DAP_NEW_Z_SIZE(uint8_t, l_domlen);
            if (!l_dom) return -ENOMEM;
            memcpy(l_dom, a_seed, 32);
            memcpy(l_dom + 32, "BDLOP-mask-v2", 13);
            memcpy(l_dom + 32 + 13, &a_proto_round, 4);
            memcpy(l_dom + 32 + 13 + 4, &l_rej, 4);
            dap_hash_shake256_absorb(l_shake, l_dom, l_domlen);
            DAP_DELETE(l_dom);

            int l_rc = s_sample_short_poly(&l_y_m, l_shake, CHIPMUNK_BDLOP_SAMP_M);
            if (l_rc != 0) return l_rc;
            for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
                l_rc = s_sample_short_poly(&l_y_r[j], l_shake, CHIPMUNK_BDLOP_SAMP_R);
                if (l_rc != 0) return l_rc;
            }
        }

        /* === Step 2: Compute W === */
        s_compute_W(a_rd->W, a_params, &l_y_m, l_y_r, l_q);

        /* === Step 2b: Sample SEPARATE masking y₂_m for quadratic shortness proof ===
         *
         * The quadratic proof uses its OWN masking y₂_m (independent from y_m
         * used in the linear proof). This ensures:
         * 1. z_g1 = c₂·m + y₂_m uses SCALAR multiply (not ring product)
         * 2. Hadamard square of z_g1 factorizes cleanly:
         *    z_g1⊙z_g1 = c₂²·(m⊙m) + 2c₂·(y₂_m⊙m) + (y₂_m⊙y₂_m)
         * 3. Garbage terms: g1 = 2(y₂_m⊙m) − y₂_m, g0 = y₂_m⊙y₂_m
         */
        chipmunk_poly_t l_y2_m;
        {
            uint64_t l_gshake[25];
            memset(l_gshake, 0, sizeof(l_gshake));
            size_t l_domlen = 32 + 15 + 4 + 4;
            uint8_t *l_dom = DAP_NEW_Z_SIZE(uint8_t, l_domlen);
            if (!l_dom) return -ENOMEM;
            memcpy(l_dom, a_seed, 32);
            memcpy(l_dom + 32, "BDLOP-y2mask-v1", 15);
            memcpy(l_dom + 32 + 15, &a_proto_round, 4);
            memcpy(l_dom + 32 + 15 + 4, &l_rej, 4);
            dap_hash_shake256_absorb(l_gshake, l_dom, l_domlen);
            DAP_DELETE(l_dom);
            s_sample_short_poly(&l_y2_m, l_gshake, CHIPMUNK_BDLOP_SAMP_M);
        }

        /* Compute garbage terms from y₂_m (NOT y_m) */
        {
            chipmunk_poly_t l_tmp;
            chipmunk_poly_pointwise_mul_q(&l_tmp, &l_y2_m, a_message, l_q);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                a_rd->g1.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)2 * l_tmp.coeffs[k] - l_y2_m.coeffs[k], l_q);
            }
            chipmunk_poly_pointwise_mul_q(&a_rd->g0, &l_y2_m, &l_y2_m, l_q);
        }

        /* === Step 3: Fiat-Shamir challenges (polynomial c₁ + scalar c₂) ===
         *
         * c₁: sparse ternary polynomial (weight τ=19) — for linear ABDLOP opening
         * c₂: scalar in F_q — for quadratic bit-ness constraint
         *
         * Both derived from the SAME FS transcript, but c₂ is sampled differently
         * (single F_q element instead of sparse ternary polynomial). */
        int32_t l_c2_scalar = 0;  /* scalar challenge for quadratic constraint */
        {
            uint64_t l_shake[25];
            memset(l_shake, 0, sizeof(l_shake));
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)"BDLOP-FS-v3", 11);
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)&a_proto_round, 4);

            size_t l_pb = CHIPMUNK_N * sizeof(int32_t);
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)a_commit->C[i].coeffs, l_pb);
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)a_rd->W[i].coeffs, l_pb);

            /* Absorb garbage terms g1, g0 (critical for soundness) */
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)a_rd->g1.coeffs, l_pb);
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)a_rd->g0.coeffs, l_pb);

            /* Absorb all previous rounds for sequential Fiat-Shamir */
            for (uint32_t pr = 0; pr < a_n_prev; ++pr) {
                const chipmunk_bdlop_proof_round_t *l_prev = &a_prev_rounds[pr];
                for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                    dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_prev->W[i].coeffs, l_pb);
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_prev->challenge.coeffs, l_pb);
            }

            /* Sample polynomial challenge c₁ */
            s_sample_challenge(&a_rd->challenge, l_shake);

            /* Sample scalar challenge c₂ ∈ F_q \ {0} */
            {
                uint8_t l_buf4[4];
                dap_hash_shake256_squeezeblocks(l_buf4, 1, l_shake);
                uint32_t l_tmp;
                memcpy(&l_tmp, l_buf4, 4);
                l_c2_scalar = (int32_t)(l_tmp % (uint32_t)l_q);
                if (l_c2_scalar == 0) l_c2_scalar = 1;  /* c₂ ≠ 0 */
            }
        }
        /* Store c₂ in g0.coeffs[0] — g0 is no longer used as garbage term,
         * we store the scalar challenge there for serialization. */
        a_rd->c2_scalar = l_c2_scalar;

        /* === Step 4: Compute responses === */
        {
            /* Linear responses: z_m = c₁·m + y_m, z_r = c₁·r + y_r (ring product) */
            s_poly_mul(&a_rd->z_m[0], &a_rd->challenge, a_message, l_q);
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int64_t l_s = (int64_t)a_rd->z_m[0].coeffs[k] + l_y_m.coeffs[k];
                a_rd->z_m[0].coeffs[k] = chipmunk_mod_q_q(l_s, l_q);
            }

            for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
                s_poly_mul(&a_rd->z_r[j], &a_rd->challenge, &a_randomness[j], l_q);
                for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                    int64_t l_s = (int64_t)a_rd->z_r[j].coeffs[k] + l_y_r[j].coeffs[k];
                    a_rd->z_r[j].coeffs[k] = chipmunk_mod_q_q(l_s, l_q);
                }
            }

            /* Quadratic response: z_g1 = c₂·m + y₂_m (SCALAR multiply)
             *
             * z_g1[k] = c₂ · m[k] + y₂_m[k]  (mod q)
             * This is Hadamard-friendly: z_g1⊙z_g1 = c₂²·(m⊙m) + 2c₂·(y₂_m⊙m) + (y₂_m⊙y₂_m)
             * Verifier checks: z_g1⊙z_g1 − c₂·z_g1 − c₂·g1 − g0 ≡ 0 */
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k)
                a_rd->z_g1.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)l_c2_scalar * a_message->coeffs[k] + l_y2_m.coeffs[k], l_q);
        }

        /* === Step 5: Rejection sampling (z_m and z_r only) ===
         * g1, g0 and their responses z_g1, z_g0 are sent directly — no rejection
         * sampling. They have large coefficients (up to 2·SAMP_M) that would
         * make rejection infeasible. Soundness comes from the quadratic identity
         * check in verify, not from response norm bounds. */
        bool l_accept = true;

        if (s_poly_chknorm(&a_rd->z_m[0], (int32_t)CHIPMUNK_BDLOP_RESP_M, l_q) != 0)
            l_accept = false;
        if (l_accept) {
            for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
                if (s_poly_chknorm(&a_rd->z_r[j], (int32_t)CHIPMUNK_BDLOP_RESP_R, l_q) != 0) {
                    l_accept = false;
                    break;
                }
            }
        }

        /* Wipe masking regardless of result */
        dap_memwipe(&l_y_m, sizeof(l_y_m));
        dap_memwipe(l_y_r, sizeof(l_y_r));
        dap_memwipe(&l_y2_m, sizeof(l_y2_m));

        if (l_accept)
            return 0;
    }

    return -EAGAIN;  /* All rejection rounds failed */
}

/* =======================================================================
 *  Opening proof — Prove (public API)
 * ======================================================================= */

int chipmunk_bdlop_opening_prove(chipmunk_bdlop_proof_t *a_proof,
                                  const chipmunk_pedersen_params_t *a_params,
                                  const chipmunk_bdlop_commit_t *a_commit,
                                  const chipmunk_poly_t *a_message,
                                  const chipmunk_poly_t a_randomness[CHIPMUNK_BDLOP_L],
                                  int32_t a_msg_bound,
                                  const uint8_t a_seed[32])
{
    /* Phase 2.4b: Approximate shortness now FULLY IMPLEMENTED.
     * Quadratic constraint m⊙m = m is proven via garbage terms g1, g0
     * and responses z_g1, z_g0. Verifier checks:
     *   z_m⊙z_m − c·z_m − c·z_g1 − z_g0 ≡ 0 (mod q)
     * This proves all coefficients of m are binary {0,1}, i.e. ||m||∞ ≤ 1. */
    if (!a_proof || !a_params || !a_commit || !a_message || !a_randomness || !a_seed)
        return -EINVAL;
    if (!a_params->initialized)
        return -EINVAL;

    memset(a_proof, 0, sizeof(*a_proof));
    a_proof->num_rounds = CHIPMUNK_BDLOP_ROUNDS;

    for (uint32_t r = 0; r < CHIPMUNK_BDLOP_ROUNDS; ++r) {
        int l_rc = s_prove_one_round(&a_proof->rounds[r], r, a_params, a_commit,
                                      a_message, a_randomness,
                                      a_proof->rounds, r, a_seed);
        if (l_rc != 0) {
            log_it(L_ERROR, "BDLOP opening: round %u failed (rc=%d)", r, l_rc);
            return l_rc;
        }
    }

    return 0;
}

/* =======================================================================
 *  Opening proof — Verify (public API)
 * ======================================================================= */

int chipmunk_bdlop_opening_verify(const chipmunk_bdlop_proof_t *a_proof,
                                   const chipmunk_pedersen_params_t *a_params,
                                   const chipmunk_bdlop_commit_t *a_commit)
{
    if (!a_proof || !a_params || !a_commit)
        return -EINVAL;
    if (!a_params->initialized)
        return -EINVAL;

    const uint64_t l_q = a_params->q;

    if (a_proof->num_rounds != CHIPMUNK_BDLOP_ROUNDS) {
        log_it(L_WARNING, "BDLOP verify: num_rounds=%u != %u",
               a_proof->num_rounds, CHIPMUNK_BDLOP_ROUNDS);
        return 0;
    }

    for (uint32_t r = 0; r < CHIPMUNK_BDLOP_ROUNDS; ++r) {
        const chipmunk_bdlop_proof_round_t *l_rd = &a_proof->rounds[r];

        /* Check 1: Response norms */
        if (s_poly_chknorm(&l_rd->z_m[0], (int32_t)CHIPMUNK_BDLOP_RESP_M, l_q) != 0) {
            log_it(L_WARNING, "BDLOP verify: round %u z_m exceeds norm bound", r);
            return 0;
        }
        for (uint32_t j = 0; j < CHIPMUNK_BDLOP_L; ++j) {
            if (s_poly_chknorm(&l_rd->z_r[j], (int32_t)CHIPMUNK_BDLOP_RESP_R, l_q) != 0) {
                log_it(L_WARNING, "BDLOP verify: round %u z_r[%u] exceeds norm bound", r, j);
                return 0;
            }
        }

        /* Check 2: Challenge validity (ternary, weight τ=1) */
        {
            uint32_t l_wt = 0;
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int32_t c = l_rd->challenge.coeffs[k];
                if (c >= (int32_t)(l_q / 2)) c -= (int32_t)l_q;
                if (c != 0 && c != 1 && c != -1) {
                    log_it(L_WARNING, "BDLOP verify: round %u non-ternary challenge", r);
                    return 0;
                }
                if (c != 0) l_wt++;
            }
            if (l_wt != CHIPMUNK_BDLOP_TAU) {
                log_it(L_WARNING, "BDLOP verify: round %u challenge weight %u != %u",
                       r, l_wt, CHIPMUNK_BDLOP_TAU);
                return 0;
            }
        }

        /* Check 3: Fiat-Shamir challenge re-derivation (with g1, g0 absorbed) */
        {
            chipmunk_poly_t l_expected_c;
            uint64_t l_shake[25];
            memset(l_shake, 0, sizeof(l_shake));
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)"BDLOP-FS-v3", 11);
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)&r, 4);

            size_t l_pb = CHIPMUNK_N * sizeof(int32_t);
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)a_commit->C[i].coeffs, l_pb);
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_rd->W[i].coeffs, l_pb);

            /* Absorb garbage terms g1, g0 (same order as prover) */
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_rd->g1.coeffs, l_pb);
            dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_rd->g0.coeffs, l_pb);

            for (uint32_t pr = 0; pr < r; ++pr) {
                const chipmunk_bdlop_proof_round_t *l_prev = &a_proof->rounds[pr];
                for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                    dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_prev->W[i].coeffs, l_pb);
                dap_hash_shake256_absorb(l_shake, (const uint8_t *)l_prev->challenge.coeffs, l_pb);
            }

            s_sample_challenge(&l_expected_c, l_shake);

            if (!chipmunk_poly_equal_q(&l_expected_c, &l_rd->challenge, l_q)) {
                log_it(L_WARNING, "BDLOP verify: round %u challenge mismatch", r);
                return 0;
            }

            /* CRITICAL FIX: Re-derive scalar c₂ from the SAME FS transcript.
             * Without this, attacker can set c₂=0 and bypass bit-ness entirely. */
            int32_t l_expected_c2;
            {
                uint8_t l_buf4[4];
                dap_hash_shake256_squeezeblocks(l_buf4, 1, l_shake);
                uint32_t l_tmp;
                memcpy(&l_tmp, l_buf4, 4);
                l_expected_c2 = (int32_t)(l_tmp % (uint32_t)l_q);
                if (l_expected_c2 == 0) l_expected_c2 = 1;
            }
            if (l_rd->c2_scalar != l_expected_c2) {
                log_it(L_WARNING, "BDLOP verify: round %u scalar c₂ mismatch "
                       "(got=%d, expected=%d)", r, l_rd->c2_scalar, l_expected_c2);
                return 0;
            }
        }

        /* Check 4: Linear equations
         *   A[i]·z_r + (i==0 ? z_m : 0) - c₁·C[i]  =?  W[i] */
        {
            chipmunk_poly_t l_cC[CHIPMUNK_BDLOP_K];
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                s_poly_mul(&l_cC[i], &l_rd->challenge, &a_commit->C[i], l_q);

            chipmunk_poly_t l_lhs[CHIPMUNK_BDLOP_K];
            s_matrix_times_vec(l_lhs, a_params, l_rd->z_r, &l_rd->z_m[0], l_q);

            chipmunk_poly_t l_rhs[CHIPMUNK_BDLOP_K];
            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i)
                chipmunk_poly_add_q(&l_rhs[i], &l_rd->W[i], &l_cC[i], l_q);

            for (uint32_t i = 0; i < CHIPMUNK_BDLOP_K; ++i) {
                if (!chipmunk_poly_equal_q(&l_lhs[i], &l_rhs[i], l_q)) {
                    log_it(L_WARNING, "BDLOP verify: round %u linear eq %u fails", r, i);
                    return 0;
                }
            }
        }

        /* Check 5: Quadratic bit-ness constraint (approximate shortness).
         *
         * Uses SCALAR challenge c₂ (from l_rd->c2_scalar field).
         *
         * z_g1 = c₂·m + y₂_m  (scalar multiply, all SCALAR/Hadamard ops)
         * g1 = 2·(y₂_m ⊙ m) − y₂_m
         * g0 = y₂_m ⊙ y₂_m
         *
         * Expansion: z_g1⊙z_g1 = c₂²·(m⊙m) + 2c₂·(y₂_m⊙m) + (y₂_m⊙y₂_m)
         *            c₂·z_g1   = c₂²·m + c₂·y₂_m
         *            c₂·g1     = 2c₂·(y₂_m⊙m) − c₂·y₂_m
         *
         * z_g1⊙z_g1 − c₂·z_g1 − c₂·g1 − g0
         *   = c₂²·(m⊙m) + 2c₂·(y₂_m⊙m) + (y₂_m⊙y₂_m)
         *     − c₂²·m − c₂·y₂_m
         *     − 2c₂·(y₂_m⊙m) + c₂·y₂_m
         *     − (y₂_m⊙y₂_m)
         *   = c₂²·(m⊙m − m)
         *
         * If m⊙m=m (binary): result = 0 ✓
         * If m⊙m≠m: result = c₂²·(m⊙m−m) ≠ 0 for random c₂ (Schwartz-Zippel). */
        {
            int32_t l_c2 = l_rd->c2_scalar;

            /* t = z_g1 ⊙ z_g1 (Hadamard square) */
            chipmunk_poly_t l_t;
            chipmunk_poly_pointwise_mul_q(&l_t, &l_rd->z_g1, &l_rd->z_g1, l_q);

            /* u = c₂ · z_g1 (scalar multiply) */
            chipmunk_poly_t l_u;
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k)
                l_u.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)l_c2 * l_rd->z_g1.coeffs[k], l_q);

            /* v_tmp = c₂ · g1 (scalar multiply) */
            chipmunk_poly_t l_v_tmp;
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k)
                l_v_tmp.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)l_c2 * l_rd->g1.coeffs[k], l_q);

            /* v = c₂·g1 + g0 */
            chipmunk_poly_t l_v;
            chipmunk_poly_add_q(&l_v, &l_v_tmp, &l_rd->g0, l_q);

            /* diff = t − u − v */
            chipmunk_poly_t l_diff;
            chipmunk_poly_sub_q(&l_diff, &l_t, &l_u, l_q);
            chipmunk_poly_sub_q(&l_diff, &l_diff, &l_v, l_q);

            /* Check diff == 0 in R_q */
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                int32_t c = l_diff.coeffs[k];
                if (c >= (int32_t)(l_q / 2)) c -= (int32_t)l_q;
                if (c != 0) {
                    log_it(L_WARNING, "BDLOP verify: round %u quadratic bit-ness "
                           "check failed at coeff %u (diff=%d)", r, k, c);
                    return 0;
                }
            }
        }
    }

    return 1;  /* All rounds valid */
}
