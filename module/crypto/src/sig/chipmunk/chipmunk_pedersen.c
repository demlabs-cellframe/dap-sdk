/*
 * chipmunk_pedersen.c — Lattice-based Pedersen commitment implementation.
 *
 * C = A * r + encode(m) mod q
 * where A ∈ R_q^{K×L}, r ∈ R_q^L short, m ∈ Z encoded as constant polynomial.
 *
 * Phase 2: Encoding changed from binary bits to base-256 digit decomposition.
 * encode(v)[i] = byte i of the LE uint256 value (v >> (8*i)) & 0xFF.
 * This is homomorphic: encode(v1) + encode(v2) = encode(v1+v2) in R_q
 * because each digit ∈ [0,255] and 2*255 = 510 < Q = 3168257.
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

/* Encode uint256 (LE bytes) as base-256 digit decomposition.
 * Coefficient i = byte i of the LE value.
 * 32 digits (bytes) × 8 bits = 256 bits, using coeffs[0..31].
 *
 * Homomorphic: encode(v1) + encode(v2) = encode(v1 + v2) in R_q
 * because each digit ∈ [0, 255] and digit-wise sum ∈ [0, 510] < Q. */
static void s_encode_message_bytes(chipmunk_poly_t *a_out,
                                   const uint8_t a_message[CHIPMUNK_PEDERSEN_VALUE_BYTES])
{
    memset(a_out, 0, sizeof(chipmunk_poly_t));
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_DIGITS; ++i) {
        a_out->coeffs[i] = (int32_t)a_message[i];
    }
}

/* Encode a single digit [0, 255] at position digit_pos */
static void s_encode_digit_at(chipmunk_poly_t *a_out, uint8_t a_digit, uint32_t a_pos)
{
    memset(a_out, 0, sizeof(chipmunk_poly_t));
    if (a_pos < CHIPMUNK_N)
        a_out->coeffs[a_pos] = (int32_t)a_digit;
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

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        chipmunk_poly_t l_sum;
        memset(&l_sum, 0, sizeof(l_sum));

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_a_ntt = a_params->A[i][j];
            chipmunk_poly_t l_r_ntt = a_randomness[j];
            chipmunk_ntt(l_a_ntt.coeffs);
            chipmunk_ntt(l_r_ntt.coeffs);

            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt(&l_prod, &l_a_ntt, &l_r_ntt);
            chipmunk_invntt(l_prod.coeffs);

            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_prod.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        if (i == 0) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + a_message_poly->coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
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
                        int32_t l_sample = chipmunk_sample_reject4(l_buf + l_sq_pos, (uint32_t)CHIPMUNK_Q);
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
    s_encode_message_bytes(&l_m, a_message);
    return s_pedersen_commit_with_message_poly(a_commit, a_params, &l_m, a_randomness);
}

void chipmunk_pedersen_blinding_sub(chipmunk_poly_t a_result[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t a[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t b[CHIPMUNK_LRS_K])
{
    if (!a_result || !a || !b) return;
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j)
        chipmunk_poly_sub(&a_result[j], &a[j], &b[j]);
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
    s_encode_message_bytes(&l_m, a_message);
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
                                             uint8_t a_digit,
                                             uint32_t a_digit_pos,
                                             const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_randomness) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;
    if (a_digit_pos >= CHIPMUNK_PEDERSEN_DIGITS) return -EINVAL;
    if (a_digit > CHIPMUNK_PEDERSEN_DIGIT_MAX) return -EINVAL;

    chipmunk_poly_t l_m;
    s_encode_digit_at(&l_m, a_digit, a_digit_pos);
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
    s_encode_message_bytes(&l_m, a_opening->message);

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
    if (!a_sum || !a_c1 || !a_c2) return;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            a_sum->C[i].coeffs[k] = (int32_t)(((int64_t)a_c1->C[i].coeffs[k]
                                                + a_c2->C[i].coeffs[k])
                                               % CHIPMUNK_Q);
            if (a_sum->C[i].coeffs[k] < 0) a_sum->C[i].coeffs[k] += CHIPMUNK_Q;
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
    if (!a_commit || !a_in) return -EINVAL;
    size_t l_needed = (size_t)CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    if (a_in_size < l_needed) return -ENOMEM;

    size_t l_off = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        memcpy(a_commit->C[i].coeffs, a_in + l_off, CHIPMUNK_N * sizeof(int32_t));
        l_off += CHIPMUNK_N * sizeof(int32_t);
    }
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
            if (a_commit->C[i].coeffs[j] < 0 || a_commit->C[i].coeffs[j] >= CHIPMUNK_Q) {
                return -EINVAL;
            }
        }
    }
    return 0;
}
