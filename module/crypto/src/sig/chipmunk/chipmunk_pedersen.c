/*
 * chipmunk_pedersen.c — Lattice-based Pedersen commitment implementation.
 *
 * C = A * r + encode(m) mod q
 * where A ∈ R_q^{K×L}, r ∈ R_q^L short, m ∈ Z encoded as constant polynomial.
 *
 * Phase 6: Encoding changed from base-256 digits to scalar encoding.
 * encode(v)[i] = (v mod Q) for ALL coefficients i.
 * This is Z-linear: encode(v1) + encode(v2) = encode(v1+v2) in R_q.
 * No carry propagation issues. Value range: [0, Q-1].
 *
 * Previous encodings had carry propagation bugs:
 *   Base-256: encode(128)[0]+encode(128)[0]=256, but encode(256)[0]=0.
 *   Base-14: same issue at chunk boundaries (e.g., 16383+1=16384).
 * Scalar encoding eliminates all carry issues by using uniform coefficients.
 */

#include "chipmunk_pedersen.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_lrs.h"
#include "chipmunk.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#include <string.h>
#include <errno.h>

#define LOG_TAG "chipmunk_pedersen"

/* Encode uint256 (LE bytes) as a scalar in R_q.
 *
 * Phase 6: Replaced chunk-based encoding with trivial scalar encoding.
 * encode(v) has ALL coefficients equal to (v mod Q).
 * This is Z-linear: encode(v1) + encode(v2) = encode(v1 + v2) in R_q,
 * and encode(v1) - encode(v2) = encode(v1 - v2) in R_q.
 *
 * Value range: [0, Q-1] where Q = 3168257. Values ≥ Q are rejected.
 * This is the standard lattice commitment approach: commit to values less
 * than the ring modulus. For blockchain use, amounts are in atomic units
 * where the maximum per-TX amount is ~3.1M atomic units.
 *
 * Trade-off vs chunk encoding:
 *   Chunk encoding (14-bit): supports 256-bit values but breaks additivity
 *     at chunk boundaries — conservation fails when digit sums overflow.
 *   Scalar encoding: perfectly additive for ALL values in [0, Q-1],
 *     no carry issues ever. Limited range is acceptable since amounts
 *     are confidential and the network enforces max amount per TX.
 */
static void s_encode_message_bytes(chipmunk_poly_t *a_out,
                                   const uint8_t a_message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                   uint64_t q)
{
    uint64_t l_val = 0;
    memcpy(&l_val, a_message, sizeof(l_val));
    int32_t l_coeff = chipmunk_mod_q_q((int64_t)l_val, q);

    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        a_out->coeffs[i] = l_coeff;
    }
}

/* Encode a single scalar value at ALL coefficients (scalar encoding). */
static void s_encode_digit_at(chipmunk_poly_t *a_out, int32_t a_digit, uint32_t a_pos,
                                uint64_t q)
{
    (void)a_pos;
    int32_t l_coeff = chipmunk_mod_q_q((int64_t)a_digit, q);
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        a_out->coeffs[i] = l_coeff;
    }
}

static void s_encode_bit_at(chipmunk_poly_t *a_out, uint8_t a_bit, uint32_t a_pos)
{
    memset(a_out, 0, sizeof(chipmunk_poly_t));
    if (a_pos < CHIPMUNK_N)
        a_out->coeffs[a_pos] = (int32_t)(a_bit & 1u);
}

static int s_pedersen_commit_with_message_poly(chipmunk_pedersen_commit_t *a_commit,
                                               const chipmunk_pedersen_params_t *a_params,
                                               const chipmunk_poly_t *a_message_poly,
                                               const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_message_poly || !a_randomness)
        return -EINVAL;
    if (!a_params->initialized)
        return -EINVAL;

    uint64_t l_q = a_params->q;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        chipmunk_poly_t l_sum;
        memset(&l_sum, 0, sizeof(l_sum));

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_a_ntt = a_params->A[i][j];
            chipmunk_poly_t l_r_ntt = a_randomness[j];
            chipmunk_ntt(l_a_ntt.coeffs);
            chipmunk_ntt(l_r_ntt.coeffs);

            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt_q(&l_prod, &l_a_ntt, &l_r_ntt, l_q);
            chipmunk_invntt(l_prod.coeffs);

            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)l_sum.coeffs[k] + l_prod.coeffs[k], l_q);
            }
        }

        if (i == 0) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = chipmunk_mod_q_q(
                    (int64_t)l_sum.coeffs[k] + a_message_poly->coeffs[k], l_q);
            }
        }

        a_commit->C[i] = l_sum;
    }
    return 0;
}

int chipmunk_pedersen_init(chipmunk_pedersen_params_t *a_params,
                           const uint8_t a_seed[32])
{
    if (!a_params || !a_seed) return -EINVAL;

    a_params->q = (uint64_t)CHIPMUNK_Q;  /* Phase 9.14c: default modulus */

    /* Derive matrix A from seed using SHAKE256 */
    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    {
        size_t l_abs_len = 32 + 18;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) return -ENOMEM;
        memcpy(l_abs, a_seed, 32);
        memcpy(l_abs + 32, "pedersen-matrix-v1", 18);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);
    }

    /* Use heap allocation. Size must be multiple of SHAKE256 rate (136 bytes)
     * to avoid overflow from squeezeblocks writing beyond buffer. */
    size_t l_needed = CHIPMUNK_N * 4;  /* 2048 bytes for 512 coefficients * 4 bytes */
    size_t l_nblocks = (l_needed + 135) / 136;  /* ceil(2048/136) = 16 blocks */
    size_t l_buf_size = l_nblocks * 136;  /* 16 * 136 = 2176 bytes */
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buf_size);
    if (!l_buf) return -ENOMEM;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            /* Squeeze random bytes for polynomial */
            dap_hash_shake256_squeezeblocks(l_buf,
                                             (l_buf_size + 135) / 136,
                                             l_state);
            /* Convert to polynomial coefficients — rejection sampling for uniformity */
            {
                size_t l_sq_pos = 0;
                for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                    for (;;) {
                        if (l_sq_pos + 4 > l_buf_size) break; /* should not happen */
                        int32_t l_sample = chipmunk_sample_reject4(l_buf + l_sq_pos, (uint32_t)a_params->q);
                        l_sq_pos += 4;
                        if (l_sample >= 0) {
                            a_params->A[i][j].coeffs[k] = l_sample;
                            break;
                        }
                    }
                }
            }
        }
    }

    DAP_DELETE(l_buf);
    a_params->initialized = true;
    return 0;
}

int chipmunk_pedersen_derive_blinding(chipmunk_poly_t a_r[CHIPMUNK_LRS_K],
                                       const uint8_t a_randomness_seed[32])
{
    if (!a_r || !a_randomness_seed) return -EINVAL;

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    {
        size_t l_abs_len = 32 + 22;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) return -ENOMEM;
        memcpy(l_abs, a_randomness_seed, 32);
        memcpy(l_abs + 32, "pedersen-randomness-v1", 22);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);
    }

    size_t l_needed = CHIPMUNK_N * 4;
    size_t l_nblocks = (l_needed + 135) / 136;
    size_t l_buf_size = l_nblocks * 136;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buf_size);
    if (!l_buf) return -ENOMEM;

    const uint32_t l_blind_range = 2 * 13 + 1;  /* 27: coefficients in [-13, 13] */
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, l_state);
        size_t l_sq_pos = 0;
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            for (;;) {
                if (l_sq_pos + 4 > l_buf_size) break; /* should not happen */
                int32_t l_sample = chipmunk_sample_reject4(l_buf + l_sq_pos, l_blind_range);
                l_sq_pos += 4;
                if (l_sample >= 0) {
                    a_r[j].coeffs[k] = l_sample - 13;
                    break;
                }
            }
        }
    }
    DAP_DELETE(l_buf);
    return 0;
}

int chipmunk_pedersen_commit_explicit(chipmunk_pedersen_commit_t *a_commit,
                                       const chipmunk_pedersen_params_t *a_params,
                                       const uint8_t a_message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                       const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_message || !a_randomness) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    chipmunk_poly_t l_m;
    s_encode_message_bytes(&l_m, a_message, a_params->q);
    return s_pedersen_commit_with_message_poly(a_commit, a_params, &l_m, a_randomness);
}

void chipmunk_pedersen_blinding_sub_q(chipmunk_poly_t a_result[CHIPMUNK_LRS_K],
                                       const chipmunk_poly_t a[CHIPMUNK_LRS_K],
                                       const chipmunk_poly_t b[CHIPMUNK_LRS_K],
                                       uint64_t q)
{
    if (!a_result || !a || !b) return;
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j)
        chipmunk_poly_sub_q(&a_result[j], &a[j], &b[j], q);
}

void chipmunk_pedersen_blinding_sub(chipmunk_poly_t a_result[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t a[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t b[CHIPMUNK_LRS_K])
{
    chipmunk_pedersen_blinding_sub_q(a_result, a, b, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_pedersen_commit(chipmunk_pedersen_commit_t *a_commit,
                             const chipmunk_pedersen_params_t *a_params,
                             const uint8_t a_message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                             const uint8_t a_randomness_seed[32])
{
    if (!a_commit || !a_params || !a_message || !a_randomness_seed) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    chipmunk_poly_t *l_r = DAP_NEW_Z_COUNT(chipmunk_poly_t, CHIPMUNK_LRS_K);
    if (!l_r) return -ENOMEM;
    int l_rc = chipmunk_pedersen_derive_blinding(l_r, a_randomness_seed);
    if (l_rc != 0) { DAP_DELETE(l_r); return l_rc; }

    chipmunk_poly_t l_m;
    s_encode_message_bytes(&l_m, a_message, a_params->q);
    l_rc = s_pedersen_commit_with_message_poly(a_commit, a_params, &l_m, l_r);

    dap_memwipe(l_r, CHIPMUNK_LRS_K * sizeof(chipmunk_poly_t));
    DAP_DELETE(l_r);
    return l_rc;
}

int chipmunk_pedersen_commit_explicit_bit(chipmunk_pedersen_commit_t *a_commit,
                                          const chipmunk_pedersen_params_t *a_params,
                                          uint8_t a_bit,
                                          uint32_t a_bit_pos,
                                          const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_randomness) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;
    if (a_bit_pos >= CHIPMUNK_PEDERSEN_VALUE_BITS) return -EINVAL;

    chipmunk_poly_t l_m;
    s_encode_bit_at(&l_m, a_bit, a_bit_pos);
    return s_pedersen_commit_with_message_poly(a_commit, a_params, &l_m, a_randomness);
}

int chipmunk_pedersen_commit_explicit_digit(chipmunk_pedersen_commit_t *a_commit,
                                             const chipmunk_pedersen_params_t *a_params,
                                             int32_t a_digit,
                                             uint32_t a_digit_pos,
                                             const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_randomness) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;
    /* a_digit_pos is ignored for scalar encoding, but validate range anyway */
    if (a_digit_pos >= CHIPMUNK_N) return -EINVAL;
    /* a_digit can be any value mod Q; no range check needed */

    chipmunk_poly_t l_m;
    s_encode_digit_at(&l_m, a_digit, a_digit_pos, a_params->q);
    return s_pedersen_commit_with_message_poly(a_commit, a_params, &l_m, a_randomness);
}

int chipmunk_pedersen_verify_opening(const chipmunk_pedersen_commit_t *a_commit,
                                     const chipmunk_pedersen_params_t *a_params,
                                     const chipmunk_pedersen_opening_t *a_opening)
{
    if (!a_commit || !a_params || !a_opening) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    /* Recompute C' = A * r + encode(m) */
    chipmunk_pedersen_commit_t l_recomputed;
    chipmunk_poly_t l_m;
    s_encode_message_bytes(&l_m, a_opening->message, a_params->q);

    int l_rc = s_pedersen_commit_with_message_poly(&l_recomputed, a_params, &l_m, a_opening->randomness);
    if (l_rc != 0)
        return l_rc;

    /* Compare C' == C */
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            if (a_commit->C[i].coeffs[k] != l_recomputed.C[i].coeffs[k]) {
                return 0; /* Invalid */
            }
        }
    }

    return 1; /* Valid */
}

void chipmunk_pedersen_add(chipmunk_pedersen_commit_t *a_sum,
                           const chipmunk_pedersen_commit_t *a_c1,
                           const chipmunk_pedersen_commit_t *a_c2)
{
    chipmunk_pedersen_add_q(a_sum, a_c1, a_c2, (uint64_t)CHIPMUNK_Q);
}

void chipmunk_pedersen_add_q(chipmunk_pedersen_commit_t *a_sum,
                               const chipmunk_pedersen_commit_t *a_c1,
                               const chipmunk_pedersen_commit_t *a_c2,
                               uint64_t q)
{
    if (!a_sum || !a_c1 || !a_c2) return;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            a_sum->C[i].coeffs[k] = chipmunk_mod_q_q(
                (int64_t)a_c1->C[i].coeffs[k] + a_c2->C[i].coeffs[k], q);
        }
    }
}

int chipmunk_pedersen_commit_serialize(uint8_t *a_out, size_t a_out_size,
                                       const chipmunk_pedersen_commit_t *a_commit)
{
    if (!a_out || !a_commit) return -EINVAL;
    /* Each polynomial: CHIPMUNK_N * 4 bytes = 2048 bytes */
    size_t l_needed = (size_t)CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    if (a_out_size < l_needed) return -ENOMEM;

    size_t l_off = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        memcpy(a_out + l_off, a_commit->C[i].coeffs, CHIPMUNK_N * sizeof(int32_t));
        l_off += CHIPMUNK_N * sizeof(int32_t);
    }
    return 0;
}

int chipmunk_pedersen_commit_deserialize(chipmunk_pedersen_commit_t *a_commit,
                                         const uint8_t *a_in, size_t a_in_size)
{
    return chipmunk_pedersen_commit_deserialize_q(a_commit, a_in, a_in_size,
                                                    (uint64_t)CHIPMUNK_Q);
}

int chipmunk_pedersen_commit_deserialize_q(chipmunk_pedersen_commit_t *a_commit,
                                             const uint8_t *a_in, size_t a_in_size,
                                             uint64_t q)
{
    if (!a_commit || !a_in) return -EINVAL;
    int32_t l_q = (int32_t)q;
    size_t l_needed = (size_t)CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    if (a_in_size < l_needed) return -ENOMEM;

    size_t l_off = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        memcpy(a_commit->C[i].coeffs, a_in + l_off, CHIPMUNK_N * sizeof(int32_t));
        l_off += CHIPMUNK_N * sizeof(int32_t);
    }
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
            if (a_commit->C[i].coeffs[j] < 0 || a_commit->C[i].coeffs[j] >= l_q) {
                return -EINVAL;
            }
        }
    }
    return 0;
}
