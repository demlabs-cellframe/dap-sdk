#if defined(__aarch64__) || defined(__arm__)
/**
 * @file dap_ntt32_neon.c
 * @brief NEON SIMD-optimized 32-bit Montgomery-domain NTT
 * @details Generated from dap_ntt32_simd.c.tpl by dap_tpl
 *
 * Targets: Dilithium/ML-DSA (q=8380417, R=2^32) and similar lattices with
 * mont_r_bits = 32.  Uses the "raw" Montgomery reduce that keeps coefficients
 * in approximately (-q, q) throughout all layers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <arm_neon.h>

#include "dap_ntt.h"
#include "dap_ntt_internal.h"

/* ============================================================================
 * NEON Architecture-Specific SIMD Primitives
 * ============================================================================ */

// ARM NEON primitives for 32-bit NTT (128-bit = 4 x int32_t)
// Builds on shared NEON primitive library.

// ============================================================================
// ARM NEON Shared SIMD Primitives (128-bit)
//
// NEON uses typed vectors (int16x8_t, uint32x4_t, etc.) unlike x86's generic
// __m128i. This library provides operations for each element width. Modules
// should typedef VEC_T to the appropriate type for their use case.
// ============================================================================

#include <arm_neon.h>

#define VEC_BITS     128
#define VEC_LANES_8  16
#define VEC_LANES_16 8
#define VEC_LANES_32 4
#define VEC_LANES_64 2

// === 8-bit (uint8x16_t) operations =========================================

#define VEC_LOAD_U8(p)        vld1q_u8((const uint8_t *)(p))
#define VEC_STORE_U8(p, v)    vst1q_u8((uint8_t *)(p), (v))
#define VEC_SET1_U8(x)        vmovq_n_u8(x)
#define VEC_CMPEQ_U8(a, b)    vceqq_u8(a, b)
#define VEC_OR_U8(a, b)       vorrq_u8(a, b)
#define VEC_AND_U8(a, b)      vandq_u8(a, b)
#define VEC_XOR_U8(a, b)      veorq_u8(a, b)

static inline uint16_t neon_movemask_u8(uint8x16_t a_vec) {
    uint8x16_t l_shifted = vshrq_n_u8(a_vec, 7);
    static const uint8_t l_weights[16] = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t l_weighted = vmulq_u8(l_shifted, vld1q_u8(l_weights));
#ifdef __aarch64__
    uint8_t l_lo = vaddv_u8(vget_low_u8(l_weighted));
    uint8_t l_hi = vaddv_u8(vget_high_u8(l_weighted));
#else
    uint8x8_t lo8 = vget_low_u8(l_weighted);
    uint8x8_t hi8 = vget_high_u8(l_weighted);
    lo8 = vpadd_u8(lo8, lo8); lo8 = vpadd_u8(lo8, lo8); lo8 = vpadd_u8(lo8, lo8);
    hi8 = vpadd_u8(hi8, hi8); hi8 = vpadd_u8(hi8, hi8); hi8 = vpadd_u8(hi8, hi8);
    uint8_t l_lo = vget_lane_u8(lo8, 0);
    uint8_t l_hi = vget_lane_u8(hi8, 0);
#endif
    return (uint16_t)l_lo | ((uint16_t)l_hi << 8);
}
#define VEC_MOVEMASK_U8(v)    neon_movemask_u8(v)

// === 16-bit signed (int16x8_t) operations ===================================

#define VEC_LOAD_S16(p)        vld1q_s16((const int16_t *)(p))
#define VEC_STORE_S16(p, v)    vst1q_s16((int16_t *)(p), (v))
#define VEC_SET1_16(x)         vdupq_n_s16(x)
#define VEC_ADD16(a, b)        vaddq_s16(a, b)
#define VEC_SUB16(a, b)        vsubq_s16(a, b)

static inline int16x8_t neon_mullo_s16(int16x8_t a, int16x8_t b) {
    return vmulq_s16(a, b);
}
static inline int16x8_t neon_mulhi_s16(int16x8_t a, int16x8_t b) {
    int16x4_t a_lo = vget_low_s16(a),  a_hi = vget_high_s16(a);
    int16x4_t b_lo = vget_low_s16(b),  b_hi = vget_high_s16(b);
    int32x4_t p_lo = vmull_s16(a_lo, b_lo);
    int32x4_t p_hi = vmull_s16(a_hi, b_hi);
    return vcombine_s16(vshrn_n_s32(p_lo, 16), vshrn_n_s32(p_hi, 16));
}

#define VEC_MULLO16(a, b)      neon_mullo_s16(a, b)
#define VEC_MULHI16(a, b)      neon_mulhi_s16(a, b)
#define VEC_SRAI16(a, n)       vshrq_n_s16(a, n)
#define VEC_SLLI16(a, n)       vshlq_n_s16(a, n)

// === 16-bit advanced ops ====================================================

#define VEC_MULHRS16(a, b)     vqrdmulhq_s16(a, b)
#define VEC_AND_S16(a, b)      vandq_s16(a, b)

// === 32-bit unsigned (uint32x4_t) operations ================================

#define VEC_LOAD_U32(p)        vld1q_u32((const uint32_t *)(p))
#define VEC_STORE_U32(p, v)    vst1q_u32((uint32_t *)(p), (v))
#define VEC_SET1_U32(x)        vdupq_n_u32(x)
#define VEC_ADD_U32(a, b)      vaddq_u32(a, b)
#define VEC_SUB_U32(a, b)      vsubq_u32(a, b)
#define VEC_XOR_U32(a, b)      veorq_u32(a, b)
#define VEC_OR_U32(a, b)       vorrq_u32(a, b)
#define VEC_SHL_U32(a, n)      vshlq_n_u32(a, n)
#define VEC_SHR_U32(a, n)      vshrq_n_u32(a, n)

// === 64-bit unsigned (uint64x2_t) operations ================================

#define VEC_LOAD_U64(p)        vld1q_u64((const uint64_t *)(p))
#define VEC_STORE_U64(p, v)    vst1q_u64((uint64_t *)(p), (v))
#define VEC_SET1_U64(x)        vdupq_n_u64(x)
#define VEC_XOR_U64(a, b)      veorq_u64(a, b)
#define VEC_ADD_U64(a, b)      vaddq_u64(a, b)

typedef int32x4_t VEC_T;
#define VEC_LANES 4

#define VEC_LOAD(p)        vld1q_s32((const int32_t *)(p))
#define VEC_STORE(p, v)    vst1q_s32((int32_t *)(p), (v))
#define VEC_SET1_32(x)     vdupq_n_s32(x)
#define VEC_ADD32(a, b)    vaddq_s32(a, b)
#define VEC_SUB32(a, b)    vsubq_s32(a, b)
#define VEC_MULLO32(a, b)  vmulq_s32(a, b)

#define VEC_AND(a, b)      vandq_s32(a, b)
#define VEC_OR(a, b)       vorrq_s32(a, b)
#define VEC_XOR(a, b)      veorq_s32(a, b)
#define VEC_ANDNOT(a, b)   vbicq_s32(b, a)
#define VEC_ZERO()         vdupq_n_s32(0)

/* vshlq_n_s32 requires a compile-time shift; Dilithium shiftl uses runtime k. */
#define VEC_SLLI32(a, n)   vshlq_s32((a), vdupq_n_s32((int32_t)(n)))
#define VEC_SRLI32(a, n)   vreinterpretq_s32_u32(vshrq_n_u32(vreinterpretq_u32_s32(a), n))
#define VEC_SRAI32(a, n)   vshrq_n_s32(a, n)

#define VEC_CMPEQ_32(a, b) vreinterpretq_s32_u32(vceqq_s32(a, b))
#define VEC_CMPGT_32(a, b) vreinterpretq_s32_u32(vcgtq_s32(a, b))
#define VEC_BLENDV_32(mask, t, f) \
    vbslq_s32(vreinterpretq_u32_s32(mask), t, f)
#define VEC_ANY_TRUE_32(v) (vmaxvq_u32(vreinterpretq_u32_s32(v)) != 0)

// Montgomery reduce multiply: (a * b) * R^{-1} mod q, R = 2^32.
// qinv = -q^{-1} mod R, formula: result = (a*b + u*q) >> 32
//
// NEON: process lower 2 and upper 2 elements via widening multiply.
// a*b uses SIGNED widening (vmull_s32).
// u*q uses UNSIGNED widening (vmull_u32) since u is an arbitrary 32-bit value.
#ifdef __aarch64__
#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({                               \
    int32x4_t _ab_lo = vmulq_s32((a), (b));                                  \
    int32x4_t _u = vmulq_s32(_ab_lo, (qinv));                                \
    int64x2_t _zb_lo = vmull_s32(vget_low_s32(a), vget_low_s32(b));          \
    uint64x2_t _uq_lo = vmull_u32(vreinterpret_u32_s32(vget_low_s32(_u)),    \
                                   vreinterpret_u32_s32(vget_low_s32(q)));    \
    int64x2_t _sum_lo = vaddq_s64(_zb_lo, vreinterpretq_s64_u64(_uq_lo));    \
    int32x2_t _t_lo = vshrn_n_s64(_sum_lo, 32);                              \
    int64x2_t _zb_hi = vmull_high_s32((a), (b));                             \
    uint64x2_t _uq_hi = vmull_high_u32(vreinterpretq_u32_s32(_u),            \
                                        vreinterpretq_u32_s32(q));            \
    int64x2_t _sum_hi = vaddq_s64(_zb_hi, vreinterpretq_s64_u64(_uq_hi));    \
    int32x2_t _t_hi = vshrn_n_s64(_sum_hi, 32);                              \
    vcombine_s32(_t_lo, _t_hi);                                               \
})
#else
#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({                               \
    int32x4_t _ab_lo = vmulq_s32((a), (b));                                  \
    int32x4_t _u = vmulq_s32(_ab_lo, (qinv));                                \
    int64x2_t _zb_lo = vmull_s32(vget_low_s32(a), vget_low_s32(b));          \
    uint64x2_t _uq_lo = vmull_u32(vreinterpret_u32_s32(vget_low_s32(_u)),    \
                                   vreinterpret_u32_s32(vget_low_s32(q)));    \
    int64x2_t _sum_lo = vaddq_s64(_zb_lo, vreinterpretq_s64_u64(_uq_lo));    \
    int32x2_t _t_lo = vshrn_n_s64(_sum_lo, 32);                              \
    int64x2_t _zb_hi = vmull_s32(vget_high_s32(a), vget_high_s32(b));        \
    uint64x2_t _uq_hi = vmull_u32(vreinterpret_u32_s32(vget_high_s32(_u)),   \
                                   vreinterpret_u32_s32(vget_high_s32(q)));   \
    int64x2_t _sum_hi = vaddq_s64(_zb_hi, vreinterpretq_s64_u64(_uq_hi));    \
    int32x2_t _t_hi = vshrn_n_s64(_sum_hi, 32);                              \
    vcombine_s32(_t_lo, _t_hi);                                               \
})
#endif

/* ============================================================================
 * Vectorized Montgomery reduce-multiply: (a * b) * R^{-1} mod q
 *
 * The VEC_MONT_REDUCE_MUL macro is provided by the primitives file.
 * It handles the 32×32→64 widening multiply and R^{-1} reduction using
 * arch-specific SIMD techniques:
 *   x86:  even/odd lane trick with blend
 *   NEON: lo/hi half widening with narrow
 * ============================================================================ */


static inline VEC_T
s_mont_reduce_mul_vec(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    return VEC_MONT_REDUCE_MUL(a_a, a_b, a_qinv, a_q);
}

#ifdef HVEC_LANES


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


void dap_ntt_forward_mont_neon(int32_t *a_coeffs,
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
                int32_t l_aj = a_coeffs[l_j];
                a_coeffs[l_j + l_len] = dap_ntt_i32_sub_wrap(l_aj, l_t);
                a_coeffs[l_j]         = dap_ntt_i32_add_wrap(l_aj, l_t);
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


void dap_ntt_inverse_mont_neon(int32_t *a_coeffs,
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
                a_coeffs[l_j]          = dap_ntt_i32_add_wrap(l_t, l_b);
                int64_t l_diff = (int64_t)l_zeta * (int64_t)dap_ntt_i32_sub_wrap(l_t, l_b);
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


void dap_ntt_pointwise_montgomery_neon(int32_t *a_c,
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
#endif
