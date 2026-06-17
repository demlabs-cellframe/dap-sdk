#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_dilithium_poly_avx512.c
 * @brief AVX-512 SIMD-optimized Dilithium/ML-DSA polynomial helpers.
 * @details Vectorized reduce, csubq, freeze, add, sub, neg, shiftl,
 *          decompose, power2round, make_hint, use_hint, chknorm
 *          for int32 Q=8380417.
 *          Generated from dap_dilithium_poly_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <immintrin.h>

// AVX-512 primitives for 32-bit NTT (512-bit = 16 x int32_t)
// Builds on shared AVX-512 primitive library.

// ============================================================================
// AVX-512 Shared SIMD Primitives (512-bit)
// Requires AVX-512F + AVX-512BW for full 8/16-bit element support.
// ============================================================================

#include <immintrin.h>

typedef __m512i VEC_T;
#define VEC_BITS     512
#define VEC_LANES_8  64
#define VEC_LANES_16 32
#define VEC_LANES_32 16
#define VEC_LANES_64 8

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm512_loadu_si512((const void *)(p))
#define VEC_STORE(p, v)   _mm512_storeu_si512((void *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm512_xor_si512(a, b)
#define VEC_AND(a, b)     _mm512_and_si512(a, b)
#define VEC_OR(a, b)      _mm512_or_si512(a, b)
#define VEC_ANDNOT(a, b)  _mm512_andnot_si512(a, b)

// === Ternary logic (single-instruction boolean combinations) ================

#define VEC_XOR3(a, b, c)     _mm512_ternarylogic_epi64(a, b, c, 0x96)
#define VEC_CHI(a, b, c)      _mm512_ternarylogic_epi64(a, b, c, 0xD2)

// === Zero ===================================================================

#define VEC_ZERO()        _mm512_setzero_si512()

// === 8-bit element ops (requires AVX-512BW) =================================

#define VEC_SET1_8(x)              _mm512_set1_epi8(x)
#define VEC_ADD8(a, b)             _mm512_add_epi8(a, b)
#define VEC_SUB8(a, b)             _mm512_sub_epi8(a, b)
#define VEC_CMPEQ_8_MASK(a, b)     _mm512_cmpeq_epi8_mask(a, b)
#define VEC_MOVEMASK_8_TYPE        __mmask64

// === 16-bit element ops (requires AVX-512BW) ================================

#define VEC_SET1_16(x)      _mm512_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm512_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm512_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm512_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm512_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm512_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm512_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm512_srli_epi16(a, n)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)      _mm512_set1_epi32((int)(x))
#define VEC_ADD32(a, b)     _mm512_add_epi32(a, b)
#define VEC_SUB32(a, b)     _mm512_sub_epi32(a, b)
#define VEC_MULLO32(a, b)   _mm512_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)    _mm512_slli_epi32(a, n)
#define VEC_SRLI32(a, n)    _mm512_srli_epi32(a, n)
#define VEC_SRAI32(a, n)    _mm512_srai_epi32(a, n)
#define VEC_CMPEQ_32(a, b) _mm512_maskz_set1_epi32(_mm512_cmpeq_epi32_mask(a, b), -1)
#define VEC_CMPGT_32(a, b) _mm512_maskz_set1_epi32(_mm512_cmpgt_epi32_mask(a, b), -1)
#define VEC_BLENDV_32(mask, t, f) \
    _mm512_mask_blend_epi32(_mm512_movepi32_mask(mask), f, t)
#define VEC_ANY_TRUE_32(v) (_mm512_test_epi32_mask(v, v) != 0)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)      _mm512_set1_epi64(x)
#define VEC_ADD64(a, b)     _mm512_add_epi64(a, b)
#define VEC_ROL64(a, n)     _mm512_rol_epi64(a, n)
#define VEC_ROLV64(a, v)    _mm512_rolv_epi64(a, v)

// === Mask load / store (5-lane for Keccak plane layout etc.) ================

#define VEC_MASKZ_LOAD_64(mask, p)     _mm512_maskz_loadu_epi64(mask, p)
#define VEC_MASK_STORE_64(p, mask, v)  _mm512_mask_storeu_epi64(p, mask, v)
#define VEC_MASK_BLEND_64(mask, a, b)  _mm512_mask_blend_epi64(mask, a, b)

// === Permutation ============================================================

#define VEC_PERMUTEXVAR_64(idx, v)          _mm512_permutexvar_epi64(idx, v)
#define VEC_PERMUTEX2VAR_64(a, idx, b)      _mm512_permutex2var_epi64(a, idx, b)
#define VEC_UNPACKLO_64(a, b)               _mm512_unpacklo_epi64(a, b)
#define VEC_UNPACKHI_64(a, b)               _mm512_unpackhi_epi64(a, b)

// === Half-width (256-bit) operations ========================================

typedef __m256i HVEC_T;
#define HVEC_BITS    256
#define HVEC_LANES_16 16

#define HVEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define HVEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))

#define HVEC_SET1_16(x)     _mm256_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm256_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm256_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm256_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm256_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm256_srai_epi16(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm512_castsi512_si256(v)
#define VEC_HI_HALF(v)            _mm512_extracti64x4_epi64(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm512_inserti64x4(_mm512_castsi256_si512(lo), (hi), 1)

#define VEC_LANES 16
#define HVEC_LANES 8

// Signed / unsigned 32x32->64 widening multiply (even-indexed elements only)
#define VEC_MUL_EVEN_S32(a, b)   _mm512_mul_epi32(a, b)
#define VEC_MUL_EVEN_U32(a, b)   _mm512_mul_epu32(a, b)

#define VEC_SHIFT_ODD32(a)       _mm512_srli_epi64(a, 32)
#define VEC_ADD64(a, b)          _mm512_add_epi64(a, b)
#define VEC_SRLI64(a, n)         _mm512_srli_epi64(a, n)
#define VEC_BLEND_EVEN_ODD32(even, odd) \
    _mm512_mask_blend_epi32((__mmask16)0xAAAA, even, odd)

#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({               \
    VEC_T _ab_lo = VEC_MULLO32((a), (b));                    \
    VEC_T _u = VEC_MULLO32(_ab_lo, (qinv));                  \
    VEC_T _ab_ev = VEC_MUL_EVEN_S32((a), (b));               \
    VEC_T _uq_ev = VEC_MUL_EVEN_U32(_u, (q));                \
    VEC_T _s_ev  = VEC_SRLI64(VEC_ADD64(_ab_ev, _uq_ev), 32);\
    VEC_T _a_od = VEC_SHIFT_ODD32((a));                      \
    VEC_T _b_od = VEC_SHIFT_ODD32((b));                      \
    VEC_T _u_od = VEC_SHIFT_ODD32(_u);                       \
    VEC_T _ab_od = VEC_MUL_EVEN_S32(_a_od, _b_od);           \
    VEC_T _uq_od = VEC_MUL_EVEN_U32(_u_od, (q));             \
    VEC_T _s_od  = VEC_ADD64(_ab_od, _uq_od);                \
    VEC_BLEND_EVEN_ODD32(_s_ev, _s_od);                      \
})

// Half-width (256-bit = 8 x int32)
#define HVEC_MUL_EVEN_S32(a, b)  _mm256_mul_epi32(a, b)
#define HVEC_MUL_EVEN_U32(a, b)  _mm256_mul_epu32(a, b)
#define HVEC_SHIFT_ODD32(a)      _mm256_srli_epi64(a, 32)
#define HVEC_ADD64(a, b)         _mm256_add_epi64(a, b)
#define HVEC_SRLI64(a, n)        _mm256_srli_epi64(a, n)
#define HVEC_BLEND_EVEN_ODD32(even, odd) _mm256_blend_epi32(even, odd, 0xAA)

#define HVEC_MONT_REDUCE_MUL(a, b, qinv, q) ({               \
    HVEC_T _ab_lo = HVEC_MULLO32((a), (b));                   \
    HVEC_T _u = HVEC_MULLO32(_ab_lo, (qinv));                 \
    HVEC_T _ab_ev = HVEC_MUL_EVEN_S32((a), (b));              \
    HVEC_T _uq_ev = HVEC_MUL_EVEN_U32(_u, (q));               \
    HVEC_T _s_ev  = HVEC_SRLI64(HVEC_ADD64(_ab_ev, _uq_ev), 32);\
    HVEC_T _a_od = HVEC_SHIFT_ODD32((a));                     \
    HVEC_T _b_od = HVEC_SHIFT_ODD32((b));                     \
    HVEC_T _u_od = HVEC_SHIFT_ODD32(_u);                      \
    HVEC_T _ab_od = HVEC_MUL_EVEN_S32(_a_od, _b_od);          \
    HVEC_T _uq_od = HVEC_MUL_EVEN_U32(_u_od, (q));            \
    HVEC_T _s_od  = HVEC_ADD64(_ab_od, _uq_od);               \
    HVEC_BLEND_EVEN_ODD32(_s_ev, _s_od);                      \
})

// Half-width additional ops
#define HVEC_SET1_32(x)     _mm256_set1_epi32((int)(x))
#define HVEC_ADD32(a, b)    _mm256_add_epi32(a, b)
#define HVEC_SUB32(a, b)    _mm256_sub_epi32(a, b)
#define HVEC_MULLO32(a, b)  _mm256_mullo_epi32(a, b)
#define HVEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define HVEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))

#define DIL_N    256
#define DIL_Q    8380417
#define DIL_QINV 4236238847U
#define DIL_MONT 4193792U

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline void s_poly_freeze_vec(int32_t *a_coeffs)
{
    s_poly_reduce_vec(a_coeffs);
    s_poly_csubq_vec(a_coeffs);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline void s_poly_add_vec(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_ADD32(va, vb));
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline void s_poly_neg_vec(int32_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_32(DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SUB32(l_q, v));
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline void s_poly_shiftl_vec(int32_t *a_coeffs, unsigned a_k)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SLLI32(v, a_k));
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_reduce_avx512(int32_t a_coeffs[DIL_N])
{
    s_poly_reduce_vec(a_coeffs);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_csubq_avx512(int32_t a_coeffs[DIL_N])
{
    s_poly_csubq_vec(a_coeffs);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_freeze_avx512(int32_t a_coeffs[DIL_N])
{
    s_poly_freeze_vec(a_coeffs);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_add_avx512(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_add_vec(a_r, a_a, a_b);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_sub_avx512(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_sub_vec(a_r, a_a, a_b);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_neg_avx512(int32_t a_coeffs[DIL_N])
{
    s_poly_neg_vec(a_coeffs);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_shiftl_avx512(int32_t a_coeffs[DIL_N],
    unsigned a_k)
{
    s_poly_shiftl_vec(a_coeffs, a_k);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_decompose_avx512(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_decompose_vec(a_a1, a_a0, a_a);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_power2round_avx512(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_power2round_vec(a_a1, a_a0, a_a);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
unsigned dap_dilithium_poly_make_hint_avx512(int32_t * restrict a_h,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    return s_poly_make_hint_vec(a_h, a_a, a_b);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_poly_use_hint_avx512(int32_t * restrict a_r,
    const int32_t * restrict a_b, const int32_t * restrict a_h)
{
    s_poly_use_hint_vec(a_r, a_b, a_h);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
int dap_dilithium_poly_chknorm_avx512(const int32_t *a_coeffs,
    int32_t a_bound)
{
    return s_poly_chknorm_vec(a_coeffs, a_bound);
}
#endif
