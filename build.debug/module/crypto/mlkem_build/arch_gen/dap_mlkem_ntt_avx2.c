#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_mlkem_ntt_avx2.c
 * @brief AVX2 specialized NTT16 for ML-KEM (Kyber)
 * @details Compile-time constants: Q=3329, QINV=-3327, N=256.
 *          Pure algorithmic template: ALL architecture-specific code lives in
 *          NTT_INNER_FILE (following the Keccak pattern). This file contains
 *          ZERO intrinsics and ZERO #if VEC_LANES branches.
 *
 *          Primitives contract — PRIMITIVES_FILE must provide:
 *            Types:  VEC_T, HVEC_T (optional)
 *            Macros: VEC_LANES, VEC_LOAD, VEC_STORE, VEC_SET1_16,
 *                    VEC_ADD16, VEC_SUB16
 *
 *          NTT_INNER_FILE may provide (Keccak-pattern opt-in):
 *            MLKEM_HAS_NTT_INNER  — enables per-block SIMD inner layers
 *            MLKEM_NTT_FWD_INNER(v, zetas, blk)
 *            MLKEM_NTT_INV_INNER(v, zetas_inv, blk)
 *            MLKEM_NTT_INV_OUTER_K — starting zeta index for outer inverse
 *            MLKEM_HAS_NTTPACK    — enables SIMD nttpack/nttunpack
 *            MLKEM_NTTPACK(coeffs), MLKEM_NTTUNPACK(coeffs)
 *
 *          Generated from dap_mlkem_ntt_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
#include <immintrin.h>

// AVX2 primitives for 16-bit polynomial/NTT ops (256-bit = 16 x int16_t)
// Builds on shared AVX2 primitive library.

// ============================================================================
// AVX2 Shared SIMD Primitives (256-bit)
// Provides unified macro names for all modules using AVX2 arch optimizations.
// ============================================================================

#include <immintrin.h>

typedef __m256i VEC_T;
#define VEC_BITS     256
#define VEC_LANES_8  32
#define VEC_LANES_16 16
#define VEC_LANES_32 8
#define VEC_LANES_64 4

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm256_loadu_si256((const __m256i *)(p))
#define VEC_STORE(p, v)   _mm256_storeu_si256((__m256i *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm256_xor_si256(a, b)
#define VEC_AND(a, b)     _mm256_and_si256(a, b)
#define VEC_OR(a, b)      _mm256_or_si256(a, b)
#define VEC_ANDNOT(a, b)  _mm256_andnot_si256(a, b)

// === Zero / Blend ===========================================================

#define VEC_ZERO()        _mm256_setzero_si256()

// === 8-bit element ops ======================================================

#define VEC_SET1_8(x)      _mm256_set1_epi8(x)
#define VEC_CMPEQ_8(a, b)  _mm256_cmpeq_epi8(a, b)
#define VEC_ADD8(a, b)     _mm256_add_epi8(a, b)
#define VEC_SUB8(a, b)     _mm256_sub_epi8(a, b)
#define VEC_MOVEMASK_8(v)  _mm256_movemask_epi8(v)

// === 16-bit element ops =====================================================

#define VEC_SET1_16(x)      _mm256_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm256_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm256_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm256_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm256_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm256_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm256_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm256_srli_epi16(a, n)

// === 16-bit advanced ops ====================================================

#define VEC_MULHRS16(a, b)      _mm256_mulhrs_epi16(a, b)
#define VEC_BLEND16(a, b, imm)  _mm256_blend_epi16(a, b, imm)
#define VEC_SHUFFLELO16(a, imm) _mm256_shufflelo_epi16(a, imm)
#define VEC_SHUFFLEHI16(a, imm) _mm256_shufflehi_epi16(a, imm)
#define VEC_SETR_16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15) \
    _mm256_setr_epi16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)          _mm256_set1_epi32((int)(x))
#define VEC_ADD32(a, b)         _mm256_add_epi32(a, b)
#define VEC_SUB32(a, b)         _mm256_sub_epi32(a, b)
#define VEC_MULLO32(a, b)       _mm256_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)        _mm256_slli_epi32(a, n)
#define VEC_SRLI32(a, n)        _mm256_srli_epi32(a, n)
#define VEC_SRAI32(a, n)        _mm256_srai_epi32(a, n)
#define VEC_SET_32(h,g,f,e,d,c,b,a)  _mm256_set_epi32(h,g,f,e,d,c,b,a)
#define VEC_CMPEQ_32(a, b)         _mm256_cmpeq_epi32(a, b)
#define VEC_CMPGT_32(a, b)         _mm256_cmpgt_epi32(a, b)
#define VEC_BLENDV_32(mask, t, f)  _mm256_blendv_epi8(f, t, mask)
#define VEC_ANY_TRUE_32(v)         (_mm256_movemask_epi8(v) != 0)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)          _mm256_set1_epi64x(x)
#define VEC_ADD64(a, b)         _mm256_add_epi64(a, b)
#define VEC_SET_64(d, c, b, a)  _mm256_set_epi64x(d, c, b, a)

// === Half-width (128-bit) operations ========================================

typedef __m128i HVEC_T;
#define HVEC_BITS    128
#define HVEC_LANES_8  16
#define HVEC_LANES_16 8
#define HVEC_LANES_32 4
#define HVEC_LANES_64 2

#define HVEC_LOAD(p)       _mm_loadu_si128((const __m128i *)(p))
#define HVEC_STORE(p, v)   _mm_storeu_si128((__m128i *)(p), (v))

#define HVEC_XOR(a, b)     _mm_xor_si128(a, b)
#define HVEC_AND(a, b)     _mm_and_si128(a, b)
#define HVEC_OR(a, b)      _mm_or_si128(a, b)
#define HVEC_ANDNOT(a, b)  _mm_andnot_si128(a, b)

#define HVEC_SET1_16(x)     _mm_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm_srai_epi16(a, n)

#define HVEC_SET1_32(x)     _mm_set1_epi32((int)(x))
#define HVEC_ADD32(a, b)    _mm_add_epi32(a, b)
#define HVEC_SUB32(a, b)    _mm_sub_epi32(a, b)
#define HVEC_SLLI32(a, n)   _mm_slli_epi32(a, n)
#define HVEC_SRLI32(a, n)   _mm_srli_epi32(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm256_castsi256_si128(v)
#define VEC_HI_HALF(v)            _mm256_extracti128_si256(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm256_setr_m128i(lo, hi)

#define VEC_LANES 16
#define HVEC_LANES 8

static inline __m256i s_vec_swap_adjacent16(__m256i v) {
    v = _mm256_shufflelo_epi16(v, 0xB1);
    return _mm256_shufflehi_epi16(v, 0xB1);
}
#define VEC_SWAP_ADJACENT16(v) s_vec_swap_adjacent16(v)
#define VEC_BLEND_ODD(a, b) VEC_BLEND16(a, b, 0xAA)

#ifndef MLKEM_Q
#define MLKEM_Q     3329
#endif
#ifndef MLKEM_QINV
#define MLKEM_QINV  ((int16_t)-3327)
#endif
#ifndef MLKEM_N
#define MLKEM_N     256
#endif
#ifndef MLKEM_BARRETT_V
#define MLKEM_BARRETT_V 20159
#endif

__attribute__((target("avx2")))
static inline VEC_T s_fqmul(VEC_T a_a, VEC_T a_b)
{
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);
    VEC_T l_lo  = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi  = VEC_MULHI16(a_a, a_b);
    VEC_T l_u   = VEC_MULLO16(l_lo, l_qinv);
    VEC_T l_uq  = VEC_MULHI16(l_u, l_q);
    return VEC_SUB16(l_hi, l_uq);
}

__attribute__((target("avx2")))
static inline VEC_T s_fqmul_ext(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    VEC_T l_lo = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi = VEC_MULHI16(a_a, a_b);
    VEC_T l_u  = VEC_MULLO16(l_lo, a_qinv);
    VEC_T l_uq = VEC_MULHI16(l_u, a_q);
    return VEC_SUB16(l_hi, l_uq);
}

__attribute__((target("avx2")))
static inline VEC_T s_barrett_reduce(VEC_T a_val)
{
    const VEC_T l_v = VEC_SET1_16(MLKEM_BARRETT_V);
    const VEC_T l_q = VEC_SET1_16(MLKEM_Q);
    VEC_T l_bt = VEC_MULHI16(l_v, a_val);
    l_bt = VEC_SRAI16(l_bt, 10);
    l_bt = VEC_MULLO16(l_bt, l_q);
    return VEC_SUB16(a_val, l_bt);
}

static inline int16_t s_fqmul_scalar(int16_t a, int16_t b)
{
    int32_t t = (int32_t)a * b;
    /* Match dap_mlkem_montgomery_reduce — unsigned low-16 product; signed t*QINV overflows int32. */
    int16_t u = (int16_t)((uint32_t)t * (uint32_t)(uint16_t)MLKEM_QINV);
    return (int16_t)((t - (int32_t)u * MLKEM_Q) >> 16);
}

static inline int16_t s_barrett_reduce_scalar(int16_t a)
{
    int16_t t = (int16_t)((int32_t)MLKEM_BARRETT_V * a >> 26);
    return a - t * MLKEM_Q;
}

#ifdef HVEC_LANES
__attribute__((target("avx2")))
static inline HVEC_T s_fqmul_hvec(HVEC_T a_a, HVEC_T a_b)
{
    const HVEC_T l_qinv = HVEC_SET1_16((int16_t)MLKEM_QINV);
    const HVEC_T l_q    = HVEC_SET1_16(MLKEM_Q);
    HVEC_T l_lo = HVEC_MULLO16(a_a, a_b);
    HVEC_T l_hi = HVEC_MULHI16(a_a, a_b);
    HVEC_T l_u  = HVEC_MULLO16(l_lo, l_qinv);
    HVEC_T l_uq = HVEC_MULHI16(l_u, l_q);
    return HVEC_SUB16(l_hi, l_uq);
}

__attribute__((target("avx2")))
static inline HVEC_T s_barrett_reduce_hvec(HVEC_T a_val)
{
    const HVEC_T l_v = HVEC_SET1_16(MLKEM_BARRETT_V);
    const HVEC_T l_q = HVEC_SET1_16(MLKEM_Q);
    HVEC_T l_bt = HVEC_MULHI16(l_v, a_val);
    l_bt = HVEC_SRAI16(l_bt, 10);
    l_bt = HVEC_MULLO16(l_bt, l_q);
    return HVEC_SUB16(a_val, l_bt);
}
#endif

/*
 * ML-KEM NTT AVX2 inner-layer primitives (VEC_LANES=16, HVEC_LANES=8).
 *
 * Provides MLKEM_NTT_FWD_INNER / MLKEM_NTT_INV_INNER — per-block shuffle-based
 * butterflies for layers 8/4/2 (forward) and 2/4/8 (inverse).
 * Also provides MLKEM_NTTPACK / MLKEM_NTTUNPACK for even/odd deinterleave.
 *
 * All macros use AVX2 intrinsics (_mm256_shuffle_epi32, _mm256_blend_epi32,
 * _mm256_setr_m128i) for sub-register data movement.
 * The enclosing template supplies: s_fqmul, s_fqmul_hvec, s_barrett_reduce,
 * s_barrett_reduce_hvec, and the VEC_* / HVEC_* abstract primitives.
 *
 * Included by dap_mlkem_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define MLKEM_HAS_NTT_INNER 1
#define MLKEM_NTT_INV_OUTER_K 112

#define MLKEM_NTT_FWD_INNER(v, zetas, blk) do {                               \
    /* Layer 8: CT butterfly across 128-bit halves */                          \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _t = s_fqmul_hvec(HVEC_SET1_16((zetas)[16 + (blk)]), _hi);    \
        v = VEC_FROM_HALVES(HVEC_ADD16(_lo, _t), HVEC_SUB16(_lo, _t));        \
    }                                                                          \
    /* Layer 4: CT butterfly across 64-bit groups within 128-bit lanes */      \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi16((zetas)[32 + 2 * (blk)]),                           \
            _mm_set1_epi16((zetas)[33 + 2 * (blk)]));                          \
        VEC_T _t = s_fqmul(_zv, _hi);                                         \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                             \
                               VEC_SUB16(_lo, _t), 0xCC);                      \
    }                                                                          \
    /* Layer 2: CT butterfly across 32-bit groups */                           \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        unsigned _z2 = 64 + 4 * (blk);                                        \
        __m128i _z4  = _mm_loadl_epi64((const __m128i *)((zetas) + _z2));      \
        __m128i _zd  = _mm_unpacklo_epi16(_z4, _z4);                          \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_unpacklo_epi32(_zd, _zd),                                      \
            _mm_unpackhi_epi32(_zd, _zd));                                     \
        VEC_T _t = s_fqmul(_zv, _hi);                                         \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                             \
                               VEC_SUB16(_lo, _t), 0xAA);                      \
    }                                                                          \
    v = s_barrett_reduce(v);                                                   \
} while (0)

#define MLKEM_NTT_INV_INNER(v, zetas_inv, blk) do {                           \
    /* Layer 2 (GS): merge 32-bit groups */                                    \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                      \
        unsigned _z2 = 4 * (blk);                                             \
        __m128i _z4  = _mm_loadl_epi64((const __m128i *)((zetas_inv) + _z2)); \
        __m128i _zd  = _mm_unpacklo_epi16(_z4, _z4);                          \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_unpacklo_epi32(_zd, _zd),                                      \
            _mm_unpackhi_epi32(_zd, _zd));                                     \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                         \
                               s_fqmul(_zv, _dif), 0xAA);                     \
    }                                                                          \
    /* Layer 4 (GS): merge 64-bit groups within 128-bit lanes */               \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                      \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi16((zetas_inv)[64 + 2 * (blk)]),                       \
            _mm_set1_epi16((zetas_inv)[65 + 2 * (blk)]));                      \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                         \
                               s_fqmul(_zv, _dif), 0xCC);                     \
    }                                                                          \
    /* Layer 8 (GS): merge 128-bit halves */                                   \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _sum = HVEC_ADD16(_lo, _hi);                                    \
        HVEC_T _dif = HVEC_SUB16(_lo, _hi);                                    \
        v = VEC_FROM_HALVES(                                                   \
            s_barrett_reduce_hvec(_sum),                                        \
            s_fqmul_hvec(HVEC_SET1_16((zetas_inv)[96 + (blk)]), _dif));        \
    }                                                                          \
} while (0)

/* nttpack/nttunpack: even/odd deinterleave using AVX2 pack/permute */

#define MLKEM_HAS_NTTPACK 1

#define MLKEM_NTTPACK(coeffs) do {                                             \
    const __m256i _mask = _mm256_set1_epi32(0x0000FFFF);                       \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _a = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _b = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        __m256i _ea = _mm256_and_si256(_a, _mask);                             \
        __m256i _oa = _mm256_srli_epi32(_a, 16);                               \
        __m256i _eb = _mm256_and_si256(_b, _mask);                             \
        __m256i _ob = _mm256_srli_epi32(_b, 16);                               \
        __m256i _ep = _mm256_packus_epi32(_ea, _eb);                           \
        __m256i _op = _mm256_packus_epi32(_oa, _ob);                           \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permute4x64_epi64(_ep, _MM_SHUFFLE(3,1,2,0)));\
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permute4x64_epi64(_op, _MM_SHUFFLE(3,1,2,0)));\
    }                                                                          \
} while (0)

#define MLKEM_NTTUNPACK(coeffs) do {                                           \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _evens = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _odds  = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        __m256i _lo = _mm256_unpacklo_epi16(_evens, _odds);                    \
        __m256i _hi = _mm256_unpackhi_epi16(_evens, _odds);                    \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permute2x128_si256(_lo, _hi, 0x20));        \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permute2x128_si256(_lo, _hi, 0x31));        \
    }                                                                          \
} while (0)

static const int16_t s_zetas[128] = {
  2285, 2571, 2970, 1812, 1493, 1422, 287, 202, 3158, 622, 1577, 182, 962,
  2127, 1855, 1468, 573, 2004, 264, 383, 2500, 1458, 1727, 3199, 2648, 1017,
  732, 608, 1787, 411, 3124, 1758, 1223, 652, 2777, 1015, 2036, 1491, 3047,
  1785, 516, 3321, 3009, 2663, 1711, 2167, 126, 1469, 2476, 3239, 3058, 830,
  107, 1908, 3082, 2378, 2931, 961, 1821, 2604, 448, 2264, 677, 2054, 2226,
  430, 555, 843, 2078, 871, 1550, 105, 422, 587, 177, 3094, 3038, 2869, 1574,
  1653, 3083, 778, 1159, 3182, 2552, 1483, 2727, 1119, 1739, 644, 2457, 349,
  418, 329, 3173, 3254, 817, 1097, 603, 610, 1322, 2044, 1864, 384, 2114, 3193,
  1218, 1994, 2455, 220, 2142, 1670, 2144, 1799, 2051, 794, 1819, 2475, 2459,
  478, 3221, 3021, 996, 991, 958, 1869, 1522, 1628
};

static const int16_t s_zetas_inv[128] = {
  1701, 1807, 1460, 2371, 2338, 2333, 308, 108, 2851, 870, 854, 1510, 2535,
  1278, 1530, 1185, 1659, 1187, 3109, 874, 1335, 2111, 136, 1215, 2945, 1465,
  1285, 2007, 2719, 2726, 2232, 2512, 75, 156, 3000, 2911, 2980, 872, 2685,
  1590, 2210, 602, 1846, 777, 147, 2170, 2551, 246, 1676, 1755, 460, 291, 235,
  3152, 2742, 2907, 3224, 1779, 2458, 1251, 2486, 2774, 2899, 1103, 1275, 2652,
  1065, 2881, 725, 1508, 2368, 398, 951, 247, 1421, 3222, 2499, 271, 90, 853,
  1860, 3203, 1162, 1618, 666, 320, 8, 2813, 1544, 282, 1838, 1293, 2314, 552,
  2677, 2106, 1571, 205, 2918, 1542, 2721, 2597, 2312, 681, 130, 1602, 1871,
  829, 2946, 3065, 1325, 2756, 1861, 1474, 1202, 2367, 3147, 1752, 2707, 171,
  3127, 3042, 1907, 1836, 1517, 359, 758, 1441
};

/* ======== Forward NTT (Cooley-Tukey) ======== */

__attribute__((target("avx2"))) __attribute__((noinline))
void dap_mlkem_ntt_forward_avx2(int16_t a_coeffs[MLKEM_N])
{
    unsigned l_k = 1;
    for (unsigned l_len = 128; l_len >= VEC_LANES; l_len >>= 1) {
        for (unsigned l_s = 0; l_s < MLKEM_N; l_s += 2 * l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas[l_k++]);
            for (unsigned l_j = l_s; l_j < l_s + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_fqmul(l_zv, l_b);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD16(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB16(l_a, l_t));
            }
        }
    }

#ifdef MLKEM_HAS_NTT_INNER
    for (unsigned l_blk = 0; l_blk < MLKEM_N / VEC_LANES; l_blk++) {
        VEC_T v = VEC_LOAD(a_coeffs + l_blk * VEC_LANES);
        MLKEM_NTT_FWD_INNER(v, s_zetas, l_blk);
        VEC_STORE(a_coeffs + l_blk * VEC_LANES, v);
    }
#else
    /* Generic scalar inner layers for sub-VEC_LANES butterflies */
#ifdef HVEC_LANES
    {
        unsigned l_len = HVEC_LANES;
        for (unsigned l_start = 0; l_start < MLKEM_N; l_start += 2 * l_len) {
            HVEC_T l_zv = HVEC_SET1_16(s_zetas[l_k++]);
            for (unsigned l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_t = s_fqmul_hvec(l_zv, l_b);
                HVEC_STORE(a_coeffs + l_j,         HVEC_ADD16(l_a, l_t));
                HVEC_STORE(a_coeffs + l_j + l_len, HVEC_SUB16(l_a, l_t));
            }
        }
    }
    for (unsigned l_len = HVEC_LANES >> 1; l_len >= 2; l_len >>= 1) {
#else
    for (unsigned l_len = VEC_LANES >> 1; l_len >= 2; l_len >>= 1) {
#endif
        for (unsigned l_start = 0; l_start < MLKEM_N; l_start += 2 * l_len) {
            int16_t l_zeta = s_zetas[l_k++];
            for (unsigned l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = s_fqmul_scalar(l_zeta, a_coeffs[l_j + l_len]);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
    for (unsigned l_i = 0; l_i < MLKEM_N; l_i++)
        a_coeffs[l_i] = s_barrett_reduce_scalar(a_coeffs[l_i]);
#endif
}

/* ======== Inverse NTT (Gentleman-Sande) ======== */

__attribute__((target("avx2"))) __attribute__((noinline))
void dap_mlkem_ntt_inverse_avx2(int16_t a_coeffs[MLKEM_N])
{
#ifdef MLKEM_HAS_NTT_INNER
    for (unsigned l_blk = 0; l_blk < MLKEM_N / VEC_LANES; l_blk++) {
        VEC_T v = VEC_LOAD(a_coeffs + l_blk * VEC_LANES);
        MLKEM_NTT_INV_INNER(v, s_zetas_inv, l_blk);
        VEC_STORE(a_coeffs + l_blk * VEC_LANES, v);
    }
    unsigned l_k = MLKEM_NTT_INV_OUTER_K;
#else
    unsigned l_k = 0;
    unsigned l_simd_start = VEC_LANES;
#ifdef HVEC_LANES
    l_simd_start = HVEC_LANES;
#endif
    for (unsigned l_len = 2; l_len < l_simd_start; l_len <<= 1) {
        for (unsigned l_start = 0; l_start < MLKEM_N; l_start += 2 * l_len) {
            int16_t l_zeta = s_zetas_inv[l_k++];
            for (unsigned l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = s_barrett_reduce_scalar(l_t + a_coeffs[l_j + l_len]);
                a_coeffs[l_j + l_len] = s_fqmul_scalar(l_zeta, l_t - a_coeffs[l_j + l_len]);
            }
        }
    }
#ifdef HVEC_LANES
    {
        unsigned l_len = HVEC_LANES;
        for (unsigned l_start = 0; l_start < MLKEM_N; l_start += 2 * l_len) {
            HVEC_T l_zv = HVEC_SET1_16(s_zetas_inv[l_k++]);
            for (unsigned l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a   = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b   = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_sum = HVEC_ADD16(l_a, l_b);
                HVEC_T l_dif = HVEC_SUB16(l_a, l_b);
                HVEC_STORE(a_coeffs + l_j,         s_barrett_reduce_hvec(l_sum));
                HVEC_STORE(a_coeffs + l_j + l_len, s_fqmul_hvec(l_zv, l_dif));
            }
        }
    }
#endif
#endif

    for (unsigned l_len = VEC_LANES; l_len <= 128; l_len <<= 1) {
        for (unsigned l_s = 0; l_s < MLKEM_N; l_s += 2 * l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas_inv[l_k++]);
            for (unsigned l_j = l_s; l_j < l_s + l_len; l_j += VEC_LANES) {
                VEC_T l_a   = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b   = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_sum = VEC_ADD16(l_a, l_b);
                VEC_T l_dif = VEC_SUB16(l_a, l_b);
                VEC_STORE(a_coeffs + l_j,         s_barrett_reduce(l_sum));
                VEC_STORE(a_coeffs + l_j + l_len, s_fqmul(l_zv, l_dif));
            }
        }
    }

    {
        VEC_T l_sv = VEC_SET1_16(s_zetas_inv[127]);
        for (unsigned l_j = 0; l_j < MLKEM_N; l_j += VEC_LANES) {
            VEC_T l_c = VEC_LOAD(a_coeffs + l_j);
            VEC_STORE(a_coeffs + l_j, s_fqmul(l_c, l_sv));
        }
    }
}

/* ======== nttpack (even/odd deinterleave) ======== */

__attribute__((target("avx2"))) __attribute__((noinline))
void dap_mlkem_ntt_nttpack_avx2(int16_t a_coeffs[MLKEM_N])
{
#ifdef MLKEM_HAS_NTTPACK
    MLKEM_NTTPACK(a_coeffs);
#else
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[l_j]      = l_blk[2 * l_j];
            l_tmp[16 + l_j] = l_blk[2 * l_j + 1];
        }
        memcpy(l_blk, l_tmp, 64);
    }
#endif
}

/* ======== nttunpack (even/odd interleave) ======== */

__attribute__((target("avx2"))) __attribute__((noinline))
void dap_mlkem_ntt_nttunpack_avx2(int16_t a_coeffs[MLKEM_N])
{
#ifdef MLKEM_HAS_NTTPACK
    MLKEM_NTTUNPACK(a_coeffs);
#else
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[2 * l_j]     = l_blk[l_j];
            l_tmp[2 * l_j + 1] = l_blk[16 + l_j];
        }
        memcpy(l_blk, l_tmp, 64);
    }
#endif
}
#endif
