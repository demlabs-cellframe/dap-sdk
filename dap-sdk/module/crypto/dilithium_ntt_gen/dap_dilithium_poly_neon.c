#if defined(__aarch64__) || defined(__arm__)
/**
 * @file dap_dilithium_poly_neon.c
 * @brief NEON SIMD-optimized Dilithium/ML-DSA polynomial helpers.
 * @details Vectorized reduce, csubq, freeze, add, sub, neg, shiftl,
 *          decompose, power2round, make_hint, use_hint, chknorm
 *          for int32 Q=8380417.
 *          Generated from dap_dilithium_poly_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <arm_neon.h>

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

#define DIL_N    256
#define DIL_Q    8380417
#define DIL_QINV 4236238847U
#define DIL_MONT 4193792U


static inline void s_poly_reduce_vec(int32_t *a_coeffs)
{
    const VEC_T l_mask23 = VEC_SET1_32(0x7FFFFF);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T lo = VEC_AND(v, l_mask23);
        VEC_T hi = VEC_SRLI32(v, 23);
        VEC_STORE(a_coeffs + i,
            VEC_ADD32(lo, VEC_SUB32(VEC_SLLI32(hi, 13), hi)));
    }
}


static inline void s_poly_csubq_vec(int32_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_32(DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T t = VEC_SUB32(v, l_q);
        VEC_T m = VEC_SRAI32(t, 31);
        VEC_STORE(a_coeffs + i, VEC_ADD32(t, VEC_AND(m, l_q)));
    }
}


static inline void s_poly_freeze_vec(int32_t *a_coeffs)
{
    s_poly_reduce_vec(a_coeffs);
    s_poly_csubq_vec(a_coeffs);
}


static inline void s_poly_add_vec(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_ADD32(va, vb));
    }
}


static inline void s_poly_sub_vec(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    const VEC_T l_2q = VEC_SET1_32(2 * DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_SUB32(VEC_ADD32(va, l_2q), vb));
    }
}

#ifndef DIL_Q
#define DIL_N       256
#define DIL_Q       8380417
#endif

#define DIL_D       14
#define DIL_GAMMA1  ((DIL_Q - 1U) / 16U)
#define DIL_GAMMA2  (DIL_GAMMA1 / 2U)
#define DIL_ALPHA   (2U * DIL_GAMMA2)


static inline void s_poly_neg_vec(int32_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_32(DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SUB32(l_q, v));
    }
}


static inline void s_poly_shiftl_vec(int32_t *a_coeffs, unsigned a_k)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SLLI32(v, a_k));
    }
}


static inline void s_poly_decompose_vec(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    const VEC_T l_mask19   = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha    = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1  = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1  = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q        = VEC_SET1_32(DIL_Q);
    const VEC_T l_one      = VEC_SET1_32(1);
    const VEC_T l_0xf      = VEC_SET1_32(0xF);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T t  = VEC_AND(va, l_mask19);
        t = VEC_ADD32(t, VEC_SLLI32(VEC_SRLI32(va, 19), 9));
        t = VEC_SUB32(t, l_half_p1);
        VEC_T tm = VEC_SRAI32(t, 31);
        t = VEC_ADD32(t, VEC_AND(tm, l_alpha));
        t = VEC_SUB32(t, l_half_m1);

        VEC_T a_val = VEC_SUB32(va, t);
        VEC_T u = VEC_SRAI32(VEC_SUB32(a_val, l_one), 31);
        a_val = VEC_SUB32(VEC_ADD32(VEC_SRLI32(a_val, 19), l_one),
                          VEC_AND(u, l_one));

        VEC_STORE(a_a0 + i,
            VEC_SUB32(VEC_ADD32(l_q, t), VEC_SRLI32(a_val, 4)));
        VEC_STORE(a_a1 + i, VEC_AND(a_val, l_0xf));
    }
}


static inline void s_poly_power2round_vec(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    const VEC_T l_d_mask    = VEC_SET1_32((1 << DIL_D) - 1);
    const VEC_T l_d_half_p1 = VEC_SET1_32((1 << (DIL_D - 1)) + 1);
    const VEC_T l_d_full    = VEC_SET1_32(1 << DIL_D);
    const VEC_T l_d_half_m1 = VEC_SET1_32((1 << (DIL_D - 1)) - 1);
    const VEC_T l_q         = VEC_SET1_32(DIL_Q);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T t  = VEC_AND(va, l_d_mask);
        t = VEC_SUB32(t, l_d_half_p1);
        VEC_T tm = VEC_SRAI32(t, 31);
        t = VEC_ADD32(t, VEC_AND(tm, l_d_full));
        t = VEC_SUB32(t, l_d_half_m1);

        VEC_STORE(a_a0 + i, VEC_ADD32(l_q, t));
        VEC_STORE(a_a1 + i, VEC_SRLI32(VEC_SUB32(va, t), DIL_D));
    }
}

/* Inline decompose helper for make_hint/use_hint: returns (a1, a0_for_hint) */
#define S_DECOMPOSE_HINT(va, l_mask19, l_alpha, l_half_p1, l_half_m1, \
                         l_q, l_one, l_0xf, out_a1, out_a0) do {      \
    VEC_T _t = VEC_AND(va, l_mask19);                                  \
    _t = VEC_ADD32(_t, VEC_SLLI32(VEC_SRLI32(va, 19), 9));            \
    _t = VEC_SUB32(_t, l_half_p1);                                     \
    _t = VEC_ADD32(_t, VEC_AND(VEC_SRAI32(_t, 31), l_alpha));         \
    _t = VEC_SUB32(_t, l_half_m1);                                     \
    VEC_T _av = VEC_SUB32(va, _t);                                     \
    VEC_T _u = VEC_SRAI32(VEC_SUB32(_av, l_one), 31);                 \
    _av = VEC_SUB32(VEC_ADD32(VEC_SRLI32(_av, 19), l_one),            \
                    VEC_AND(_u, l_one));                               \
    (out_a1) = VEC_AND(_av, l_0xf);                                    \
    (out_a0) = VEC_SUB32(VEC_ADD32(l_q, _t), VEC_SRLI32(_av, 4));     \
} while (0)


static inline unsigned s_poly_make_hint_vec(int32_t * restrict a_h,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    unsigned s = 0;
    const VEC_T l_mask19  = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha   = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1 = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1 = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q       = VEC_SET1_32(DIL_Q);
    const VEC_T l_one     = VEC_SET1_32(1);
    const VEC_T l_0xf     = VEC_SET1_32(0xF);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);

        VEC_T a1a, a0a, a1b, a0b;
        S_DECOMPOSE_HINT(va, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1a, a0a);
        S_DECOMPOSE_HINT(vb, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1b, a0b);

        VEC_T cmp  = VEC_CMPEQ_32(a1a, a1b);
        VEC_T hint = VEC_ANDNOT(cmp, l_one);
        VEC_STORE(a_h + i, hint);

        int32_t buf[VEC_LANES];
        VEC_STORE(buf, hint);
        for (int j = 0; j < VEC_LANES; j++) s += (unsigned)buf[j];
    }
    return s;
}


static inline void s_poly_use_hint_vec(int32_t * restrict a_r,
    const int32_t * restrict a_b, const int32_t * restrict a_h)
{
    const VEC_T l_mask19  = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha   = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1 = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1 = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q       = VEC_SET1_32(DIL_Q);
    const VEC_T l_one     = VEC_SET1_32(1);
    const VEC_T l_0xf     = VEC_SET1_32(0xF);
    const VEC_T l_zero    = VEC_ZERO();

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_T vh = VEC_LOAD(a_h + i);

        VEC_T a1, a0;
        S_DECOMPOSE_HINT(vb, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1, a0);

        VEC_T hint_is_zero = VEC_CMPEQ_32(vh, l_zero);
        VEC_T a0_gt_q      = VEC_CMPGT_32(a0, l_q);
        VEC_T plus1  = VEC_AND(VEC_ADD32(a1, l_one), l_0xf);
        VEC_T minus1 = VEC_AND(VEC_SUB32(a1, l_one), l_0xf);
        VEC_T hint_result = VEC_BLENDV_32(a0_gt_q, plus1, minus1);
        VEC_T result = VEC_BLENDV_32(hint_is_zero, a1, hint_result);
        VEC_STORE(a_r + i, result);
    }
}


static inline int s_poly_chknorm_vec(const int32_t *a_coeffs, int32_t a_bound)
{
    const VEC_T l_half  = VEC_SET1_32((DIL_Q - 1) / 2);
    const VEC_T l_bm1   = VEC_SET1_32(a_bound - 1);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T t = VEC_SUB32(l_half, v);
        VEC_T m = VEC_SRAI32(t, 31);
        t = VEC_XOR(t, m);
        t = VEC_SUB32(l_half, t);
        if (VEC_ANY_TRUE_32(VEC_CMPGT_32(t, l_bm1)))
            return 1;
    }
    return 0;
}


void dap_dilithium_poly_reduce_neon(int32_t a_coeffs[DIL_N])
{
    s_poly_reduce_vec(a_coeffs);
}


void dap_dilithium_poly_csubq_neon(int32_t a_coeffs[DIL_N])
{
    s_poly_csubq_vec(a_coeffs);
}


void dap_dilithium_poly_freeze_neon(int32_t a_coeffs[DIL_N])
{
    s_poly_freeze_vec(a_coeffs);
}


void dap_dilithium_poly_add_neon(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_add_vec(a_r, a_a, a_b);
}


void dap_dilithium_poly_sub_neon(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_sub_vec(a_r, a_a, a_b);
}


void dap_dilithium_poly_neg_neon(int32_t a_coeffs[DIL_N])
{
    s_poly_neg_vec(a_coeffs);
}


void dap_dilithium_poly_shiftl_neon(int32_t a_coeffs[DIL_N],
    unsigned a_k)
{
    s_poly_shiftl_vec(a_coeffs, a_k);
}


void dap_dilithium_poly_decompose_neon(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_decompose_vec(a_a1, a_a0, a_a);
}


void dap_dilithium_poly_power2round_neon(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_power2round_vec(a_a1, a_a0, a_a);
}


unsigned dap_dilithium_poly_make_hint_neon(int32_t * restrict a_h,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    return s_poly_make_hint_vec(a_h, a_a, a_b);
}


void dap_dilithium_poly_use_hint_neon(int32_t * restrict a_r,
    const int32_t * restrict a_b, const int32_t * restrict a_h)
{
    s_poly_use_hint_vec(a_r, a_b, a_h);
}


int dap_dilithium_poly_chknorm_neon(const int32_t *a_coeffs,
    int32_t a_bound)
{
    return s_poly_chknorm_vec(a_coeffs, a_bound);
}
#endif
