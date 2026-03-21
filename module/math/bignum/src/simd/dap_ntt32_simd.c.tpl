/**
 * @file dap_ntt32_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} SIMD-optimized 32-bit Montgomery-domain NTT
 * @details Generated from dap_ntt32_simd.c.tpl by dap_tpl
 *
 * Targets: Dilithium/ML-DSA (q=8380417, R=2^32) and similar lattices with
 * mont_r_bits = 32.  Uses the "raw" Montgomery reduce that keeps coefficients
 * in approximately (-q, q) throughout all layers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

#include "dap_ntt.h"
#include "dap_ntt_internal.h"

/* ============================================================================
 * {{ARCH_NAME}} Architecture-Specific SIMD Primitives
 * ============================================================================ */

{{#include PRIMITIVES_FILE}}

/* ============================================================================
 * Vectorized Montgomery reduce-multiply: (a * b) * R^{-1} mod q
 *
 * The VEC_MONT_REDUCE_MUL macro is provided by the primitives file.
 * It handles the 32×32→64 widening multiply and R^{-1} reduction using
 * arch-specific SIMD techniques:
 *   x86:  even/odd lane trick with blend
 *   NEON: lo/hi half widening with narrow
 * ============================================================================ */

{{TARGET_ATTR}}
static inline VEC_T
s_mont_reduce_mul_vec(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    return VEC_MONT_REDUCE_MUL(a_a, a_b, a_qinv, a_q);
}

#ifdef HVEC_LANES

{{TARGET_ATTR}}
static inline HVEC_T
s_mont_reduce_mul_hvec(HVEC_T a_a, HVEC_T a_b, HVEC_T a_qinv, HVEC_T a_q)
{
    return HVEC_MONT_REDUCE_MUL(a_a, a_b, a_qinv, a_q);
}

#endif /* HVEC_LANES */

/* ============================================================================
 * Forward NTT — Cooley–Tukey, sequential zeta walk, Montgomery butterfly
 *
 * Input in standard order, output in bit-reversed order.
 * Loop: len = n/2 down to 1.
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_ntt_forward_mont_{{ARCH_LOWER}}(int32_t *a_coeffs,
                                          const dap_ntt_params_t *a_params)
{
    if (a_params->mont_r_bits != 32) {
        dap_ntt_forward_mont_ref(a_coeffs, a_params);
        return;
    }

    const int32_t *l_z = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;
    const unsigned int l_n = a_params->n;

    VEC_T l_qinv_vec = VEC_SET1_32((int32_t)a_params->qinv);
    VEC_T l_q_vec    = VEC_SET1_32(a_params->q);

    /* --- Full-vector SIMD layers (len >= VEC_LANES) --- */
    for (l_len = l_n / 2; l_len >= VEC_LANES; l_len >>= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            VEC_T l_zv = VEC_SET1_32(l_z[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_mont_reduce_mul_vec(l_zv, l_b, l_qinv_vec, l_q_vec);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD32(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB32(l_a, l_t));
            }
        }
    }

#if VEC_LANES == 8 && defined(HVEC_LANES) && HVEC_LANES == 4
    /* Fused inner 3 layers (len=4,2,1) in a single load/store pass.
     * Eliminates 2 scalar layers and reduces memory traffic 3x. */
    {
        unsigned l_k4 = l_k;
        unsigned l_k2 = l_k + l_n / 8;
        unsigned l_k1 = l_k + l_n / 8 + l_n / 4;
        HVEC_T l_hqinv = HVEC_SET1_32((int32_t)a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_32(a_params->q);
        for (l_start = 0; l_start < l_n; l_start += VEC_LANES) {
            VEC_T v = VEC_LOAD(a_coeffs + l_start);
            {
                HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);
                HVEC_T _t = s_mont_reduce_mul_hvec(HVEC_SET1_32(l_z[l_k4++]),
                                                    _hi, l_hqinv, l_hq);
                v = VEC_FROM_HALVES(HVEC_ADD32(_lo, _t), HVEC_SUB32(_lo, _t));
            }
            {
                VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));
                VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));
                VEC_T _zv = _mm256_setr_m128i(
                    _mm_set1_epi32(l_z[l_k2]),
                    _mm_set1_epi32(l_z[l_k2 + 1]));
                l_k2 += 2;
                VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, l_qinv_vec, l_q_vec);
                v = _mm256_blend_epi32(VEC_ADD32(_lo, _t),
                                       VEC_SUB32(_lo, _t), 0xCC);
            }
            {
                VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));
                VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));
                VEC_T _zv = _mm256_setr_epi32(
                    l_z[l_k1], l_z[l_k1], l_z[l_k1+1], l_z[l_k1+1],
                    l_z[l_k1+2], l_z[l_k1+2], l_z[l_k1+3], l_z[l_k1+3]);
                l_k1 += 4;
                VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, l_qinv_vec, l_q_vec);
                v = _mm256_blend_epi32(VEC_ADD32(_lo, _t),
                                       VEC_SUB32(_lo, _t), 0xAA);
            }
            VEC_STORE(a_coeffs + l_start, v);
        }
    }
#else
#ifdef HVEC_LANES
    if (l_len == HVEC_LANES && l_len >= 1) {
        HVEC_T l_hqinv = HVEC_SET1_32((int32_t)a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_32(a_params->q);
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_32(l_z[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_t = s_mont_reduce_mul_hvec(l_zv, l_b, l_hqinv, l_hq);
                HVEC_STORE(a_coeffs + l_j,         HVEC_ADD32(l_a, l_t));
                HVEC_STORE(a_coeffs + l_j + l_len, HVEC_SUB32(l_a, l_t));
            }
        }
        l_len >>= 1;
    }
#endif
    for (; l_len >= 1; l_len >>= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            int32_t l_zeta = l_z[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_u = (uint32_t)((int64_t)l_zeta * a_coeffs[l_j + l_len])
                               * a_params->qinv;
                int32_t l_t = (int32_t)(((int64_t)l_zeta * a_coeffs[l_j + l_len]
                               + (int64_t)l_u * a_params->q) >> 32);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
#endif
}

/* ============================================================================
 * Inverse NTT — Gentleman–Sande, sequential zeta walk, Montgomery butterfly
 *
 * Input in bit-reversed order, output in standard order.
 * Final scaling by one_over_n is NOT applied — the caller handles it.
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_ntt_inverse_mont_{{ARCH_LOWER}}(int32_t *a_coeffs,
                                          const dap_ntt_params_t *a_params)
{
    if (a_params->mont_r_bits != 32) {
        dap_ntt_inverse_mont_ref(a_coeffs, a_params);
        return;
    }

    const int32_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;
    const unsigned int l_n = a_params->n;
    const unsigned int l_half_n = l_n / 2;

#if VEC_LANES == 8 && defined(HVEC_LANES) && HVEC_LANES == 4
    /* Fused inner 3 layers (len=1,2,4) in a single load/store pass. */
    {
        unsigned l_k1 = 0;
        unsigned l_k2 = l_n / 2;
        unsigned l_k4 = l_n / 2 + l_n / 4;
        HVEC_T l_hqinv = HVEC_SET1_32((int32_t)a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_32(a_params->q);
        VEC_T l_qinv_vec = VEC_SET1_32((int32_t)a_params->qinv);
        VEC_T l_q_vec    = VEC_SET1_32(a_params->q);
        for (l_start = 0; l_start < l_n; l_start += VEC_LANES) {
            VEC_T v = VEC_LOAD(a_coeffs + l_start);
            {
                VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));
                VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));
                VEC_T _sum = VEC_ADD32(_lo, _hi);
                VEC_T _dif = VEC_SUB32(_lo, _hi);
                VEC_T _zv = _mm256_setr_epi32(
                    l_zinv[l_k1], l_zinv[l_k1], l_zinv[l_k1+1], l_zinv[l_k1+1],
                    l_zinv[l_k1+2], l_zinv[l_k1+2], l_zinv[l_k1+3], l_zinv[l_k1+3]);
                l_k1 += 4;
                v = _mm256_blend_epi32(_sum,
                    s_mont_reduce_mul_vec(_zv, _dif, l_qinv_vec, l_q_vec), 0xAA);
            }
            {
                VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));
                VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));
                VEC_T _sum = VEC_ADD32(_lo, _hi);
                VEC_T _dif = VEC_SUB32(_lo, _hi);
                VEC_T _zv = _mm256_setr_m128i(
                    _mm_set1_epi32(l_zinv[l_k2]),
                    _mm_set1_epi32(l_zinv[l_k2 + 1]));
                l_k2 += 2;
                v = _mm256_blend_epi32(_sum,
                    s_mont_reduce_mul_vec(_zv, _dif, l_qinv_vec, l_q_vec), 0xCC);
            }
            {
                HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);
                HVEC_T _sum = HVEC_ADD32(_lo, _hi);
                HVEC_T _dif = HVEC_SUB32(_lo, _hi);
                v = VEC_FROM_HALVES(_sum,
                    s_mont_reduce_mul_hvec(HVEC_SET1_32(l_zinv[l_k4++]),
                                           _dif, l_hqinv, l_hq));
            }
            VEC_STORE(a_coeffs + l_start, v);
        }
        l_k = l_k4;
        l_len = VEC_LANES;
    }
#else
    unsigned int l_simd_start = VEC_LANES;
#ifdef HVEC_LANES
    l_simd_start = HVEC_LANES;
#endif
    for (l_len = 1; l_len < l_simd_start && l_len < l_n; l_len <<= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t    = a_coeffs[l_j];
                int32_t l_b    = a_coeffs[l_j + l_len];
                a_coeffs[l_j]          = l_t + l_b;
                int64_t l_diff = (int64_t)l_zeta * (l_t - l_b);
                uint32_t l_u = (uint32_t)l_diff * a_params->qinv;
                a_coeffs[l_j + l_len]  = (int32_t)((l_diff + (int64_t)l_u * a_params->q) >> 32);
            }
        }
    }
#ifdef HVEC_LANES
    if (l_len == HVEC_LANES && l_len <= l_half_n) {
        HVEC_T l_hqinv = HVEC_SET1_32((int32_t)a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_32(a_params->q);
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_32(l_zinv[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a   = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b   = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_sum = HVEC_ADD32(l_a, l_b);
                HVEC_T l_dif = HVEC_SUB32(l_a, l_b);
                HVEC_STORE(a_coeffs + l_j,         l_sum);
                HVEC_STORE(a_coeffs + l_j + l_len,
                           s_mont_reduce_mul_hvec(l_zv, l_dif, l_hqinv, l_hq));
            }
        }
        l_len <<= 1;
    }
#endif
#endif

    /* --- Full-vector SIMD layers --- */
    {
        VEC_T l_qinv_vec = VEC_SET1_32((int32_t)a_params->qinv);
        VEC_T l_q_vec    = VEC_SET1_32(a_params->q);

        for (; l_len <= l_half_n; l_len <<= 1) {
            for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
                VEC_T l_zv = VEC_SET1_32(l_zinv[l_k++]);
                for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                    VEC_T l_a   = VEC_LOAD(a_coeffs + l_j);
                    VEC_T l_b   = VEC_LOAD(a_coeffs + l_j + l_len);
                    VEC_T l_sum = VEC_ADD32(l_a, l_b);
                    VEC_T l_dif = VEC_SUB32(l_a, l_b);
                    VEC_STORE(a_coeffs + l_j,         l_sum);
                    VEC_STORE(a_coeffs + l_j + l_len,
                              s_mont_reduce_mul_vec(l_zv, l_dif, l_qinv_vec, l_q_vec));
                }
            }
        }
    }
}

/* ============================================================================
 * Pointwise Montgomery multiplication: c[i] = (a[i] * b[i]) * R^{-1} mod q
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_ntt_pointwise_montgomery_{{ARCH_LOWER}}(int32_t *a_c,
                                                   const int32_t *a_a,
                                                   const int32_t *a_b,
                                                   const dap_ntt_params_t *a_params)
{
    if (a_params->mont_r_bits != 32) {
        dap_ntt_pointwise_montgomery_ref(a_c, a_a, a_b, a_params);
        return;
    }

    const unsigned int l_n = a_params->n;
    VEC_T l_qinv_vec = VEC_SET1_32((int32_t)a_params->qinv);
    VEC_T l_q_vec    = VEC_SET1_32(a_params->q);
    unsigned int l_i;

    for (l_i = 0; l_i + VEC_LANES <= l_n; l_i += VEC_LANES) {
        VEC_T l_a = VEC_LOAD(a_a + l_i);
        VEC_T l_b = VEC_LOAD(a_b + l_i);
        VEC_STORE(a_c + l_i,
                  s_mont_reduce_mul_vec(l_a, l_b, l_qinv_vec, l_q_vec));
    }

    for (; l_i < l_n; l_i++) {
        int64_t l_product = (int64_t)a_a[l_i] * a_b[l_i];
        a_c[l_i] = dap_ntt_montgomery_reduce(l_product, a_params);
    }
}
