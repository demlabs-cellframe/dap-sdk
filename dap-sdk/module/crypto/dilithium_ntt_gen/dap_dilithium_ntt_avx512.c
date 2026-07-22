#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_dilithium_ntt_avx512.c
 * @brief AVX-512 specialized NTT32 for Dilithium/ML-DSA
 * @details Compile-time constants: Q=8380417, QINV=4236238847, N=256.
 *          Pure algorithmic template: ALL architecture-specific code lives in
 *          NTT_INNER_FILE (following the Keccak pattern). This file contains
 *          ZERO intrinsics and ZERO #if VEC_LANES branches.
 *
 *          Primitives contract — PRIMITIVES_FILE must provide:
 *            Types:  VEC_T, HVEC_T (optional)
 *            Macros: VEC_LANES, VEC_LOAD, VEC_STORE, VEC_SET1_32,
 *                    VEC_ADD32, VEC_SUB32, VEC_MULLO32
 *                    VEC_MONT_REDUCE_MUL(a, b, qinv, q)
 *
 *          NTT_INNER_FILE may provide (Keccak-pattern opt-in):
 *            DIL_HAS_NTT_INNER  — enables per-block SIMD inner layers
 *            DIL_NTT_FWD_INNER(v, zetas, k_base, blk, qinv_v, q_v)
 *            DIL_NTT_INV_INNER(v, zetas_inv, k_base, blk, qinv_v, q_v)
 *
 *          Generated from dap_dilithium_ntt_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
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

/*
 * Dilithium NTT AVX-512 inner-layer primitives (VEC_LANES=16, HVEC_LANES=8).
 *
 * Provides DIL_NTT_FWD_INNER / DIL_NTT_INV_INNER — per-block shuffle-based
 * butterflies for inner layers (len=8/4/2/1 forward, len=1/2/4/8 inverse).
 *
 * Uses AVX-512 intrinsics: _mm512_shuffle_epi32, _mm512_shuffle_i32x4,
 * _mm512_mask_blend_epi32, _mm512_setr_epi32, __mmask16.
 * The enclosing template supplies: s_mont_reduce_mul_vec, s_mont_reduce_mul_hvec,
 * and the VEC_* / HVEC_* abstract primitives.
 *
 * Included by dap_dilithium_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define DIL_HAS_NTT_INNER 1

#define DIL_NTT_FWD_INNER(v, zetas, k_base, blk, qinv_v, q_v) do {           \
    unsigned _k8  = (k_base);                                                  \
    unsigned _k4  = (k_base) + DIL_N / 16;                                     \
    unsigned _k2  = (k_base) + DIL_N / 16 + DIL_N / 8;                         \
    unsigned _k1  = (k_base) + DIL_N / 16 + DIL_N / 8 + DIL_N / 4;             \
    _k8 += (blk); _k4 += 2 * (blk); _k2 += 4 * (blk); _k1 += 8 * (blk);     \
    HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                         \
    HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                      \
    /* len=8: butterfly [0..7] <-> [8..15] via 256-bit halves */               \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _t = s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas)[_k8]),        \
                                            _hi, _hqinv, _hq);                \
        v = VEC_FROM_HALVES(HVEC_ADD32(_lo, _t), HVEC_SUB32(_lo, _t));        \
    }                                                                          \
    /* len=4: butterfly lanes 0<->1, 2<->3 via 128-bit shuffle */             \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(2,2,0,0));         \
        VEC_T _hi = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(3,3,1,1));         \
        VEC_T _zv = VEC_FROM_HALVES(                                           \
            HVEC_SET1_32((zetas)[_k4]),                                        \
            HVEC_SET1_32((zetas)[_k4 + 1]));                                    \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xF0F0,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
    /* len=2: butterfly within each 128-bit lane, stride=2 */                  \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas)[_k2],   (zetas)[_k2],   (zetas)[_k2],   (zetas)[_k2],     \
            (zetas)[_k2+1], (zetas)[_k2+1], (zetas)[_k2+1], (zetas)[_k2+1],   \
            (zetas)[_k2+2], (zetas)[_k2+2], (zetas)[_k2+2], (zetas)[_k2+2],   \
            (zetas)[_k2+3], (zetas)[_k2+3], (zetas)[_k2+3], (zetas)[_k2+3]);  \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xCCCC,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
    /* len=1: butterfly within each pair */                                    \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas)[_k1],   (zetas)[_k1],   (zetas)[_k1+1], (zetas)[_k1+1],   \
            (zetas)[_k1+2], (zetas)[_k1+2], (zetas)[_k1+3], (zetas)[_k1+3],   \
            (zetas)[_k1+4], (zetas)[_k1+4], (zetas)[_k1+5], (zetas)[_k1+5],   \
            (zetas)[_k1+6], (zetas)[_k1+6], (zetas)[_k1+7], (zetas)[_k1+7]);  \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xAAAA,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
} while (0)

#define DIL_NTT_INV_INNER(v, zetas_inv, k_base, blk, qinv_v, q_v) do {        \
    unsigned _k1  = (k_base);                                                  \
    unsigned _k2  = (k_base) + DIL_N / 2;                                      \
    unsigned _k4  = (k_base) + DIL_N / 2 + DIL_N / 4;                          \
    unsigned _k8  = (k_base) + DIL_N / 2 + DIL_N / 4 + DIL_N / 8;              \
    _k1 += 8 * (blk); _k2 += 4 * (blk); _k4 += 2 * (blk); _k8 += (blk);     \
    HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                         \
    HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                      \
    /* len=1: GS butterfly within each pair */                                 \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas_inv)[_k1],   (zetas_inv)[_k1],   (zetas_inv)[_k1+1], (zetas_inv)[_k1+1],\
            (zetas_inv)[_k1+2], (zetas_inv)[_k1+2], (zetas_inv)[_k1+3], (zetas_inv)[_k1+3],\
            (zetas_inv)[_k1+4], (zetas_inv)[_k1+4], (zetas_inv)[_k1+5], (zetas_inv)[_k1+5],\
            (zetas_inv)[_k1+6], (zetas_inv)[_k1+6], (zetas_inv)[_k1+7], (zetas_inv)[_k1+7]);\
        v = _mm512_mask_blend_epi32((__mmask16)0xAAAA, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=2: GS butterfly across 64-bit groups */                             \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas_inv)[_k2],   (zetas_inv)[_k2],   (zetas_inv)[_k2],   (zetas_inv)[_k2],\
            (zetas_inv)[_k2+1], (zetas_inv)[_k2+1], (zetas_inv)[_k2+1], (zetas_inv)[_k2+1],\
            (zetas_inv)[_k2+2], (zetas_inv)[_k2+2], (zetas_inv)[_k2+2], (zetas_inv)[_k2+2],\
            (zetas_inv)[_k2+3], (zetas_inv)[_k2+3], (zetas_inv)[_k2+3], (zetas_inv)[_k2+3]);\
        v = _mm512_mask_blend_epi32((__mmask16)0xCCCC, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=4: GS butterfly lanes 0<->1, 2<->3 */                              \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(2,2,0,0));         \
        VEC_T _hi = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(3,3,1,1));         \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = VEC_FROM_HALVES(                                           \
            HVEC_SET1_32((zetas_inv)[_k4]),                                    \
            HVEC_SET1_32((zetas_inv)[_k4 + 1]));                                \
        v = _mm512_mask_blend_epi32((__mmask16)0xF0F0, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=8: GS butterfly [0..7] <-> [8..15] */                               \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _sum = HVEC_ADD32(_lo, _hi);                                    \
        HVEC_T _dif = HVEC_SUB32(_lo, _hi);                                    \
        v = VEC_FROM_HALVES(_sum,                                              \
            s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas_inv)[_k8]),             \
                                   _dif, _hqinv, _hq));                        \
    }                                                                          \
} while (0)

static const int32_t s_zetas[DIL_N] = {
    0, 25847, 5771523, 7861508, 237124, 7602457, 7504169, 466468,
    1826347, 2353451, 8021166, 6288512, 3119733, 5495562, 3111497, 2680103,
    2725464, 1024112, 7300517, 3585928, 7830929, 7260833, 2619752, 6271868,
    6262231, 4520680, 6980856, 5102745, 1757237, 8360995, 4010497, 280005,
    2706023, 95776, 3077325, 3530437, 6718724, 4788269, 5842901, 3915439,
    4519302, 5336701, 3574422, 5512770, 3539968, 8079950, 2348700, 7841118,
    6681150, 6736599, 3505694, 4558682, 3507263, 6239768, 6779997, 3699596,
    811944, 531354, 954230, 3881043, 3900724, 5823537, 2071892, 5582638,
    4450022, 6851714, 4702672, 5339162, 6927966, 3475950, 2176455, 6795196,
    7122806, 1939314, 4296819, 7380215, 5190273, 5223087, 4747489, 126922,
    3412210, 7396998, 2147896, 2715295, 5412772, 4686924, 7969390, 5903370,
    7709315, 7151892, 8357436, 7072248, 7998430, 1349076, 1852771, 6949987,
    5037034, 264944, 508951, 3097992, 44288, 7280319, 904516, 3958618,
    4656075, 8371839, 1653064, 5130689, 2389356, 8169440, 759969, 7063561,
    189548, 4827145, 3159746, 6529015, 5971092, 8202977, 1315589, 1341330,
    1285669, 6795489, 7567685, 6940675, 5361315, 4499357, 4751448, 3839961,
    2091667, 3407706, 2316500, 3817976, 5037939, 2244091, 5933984, 4817955,
    266997, 2434439, 7144689, 3513181, 4860065, 4621053, 7183191, 5187039,
    900702, 1859098, 909542, 819034, 495491, 6767243, 8337157, 7857917,
    7725090, 5257975, 2031748, 3207046, 4823422, 7855319, 7611795, 4784579,
    342297, 286988, 5942594, 4108315, 3437287, 5038140, 1735879, 203044,
    2842341, 2691481, 5790267, 1265009, 4055324, 1247620, 2486353, 1595974,
    4613401, 1250494, 2635921, 4832145, 5386378, 1869119, 1903435, 7329447,
    7047359, 1237275, 5062207, 6950192, 7929317, 1312455, 3306115, 6417775,
    7100756, 1917081, 5834105, 7005614, 1500165, 777191, 2235880, 3406031,
    7838005, 5548557, 6709241, 6533464, 5796124, 4656147, 594136, 4603424,
    6366809, 2432395, 2454455, 8215696, 1957272, 3369112, 185531, 7173032,
    5196991, 162844, 1616392, 3014001, 810149, 1652634, 4686184, 6581310,
    5341501, 3523897, 3866901, 269760, 2213111, 7404533, 1717735, 472078,
    7953734, 1723600, 6577327, 1910376, 6712985, 7276084, 8119771, 4546524,
    5441381, 6144432, 7959518, 6094090, 183443, 7403526, 1612842, 4834730,
    7826001, 3919660, 8332111, 7018208, 3937738, 1400424, 7534263, 1976782
};

static const int32_t s_zetas_inv[DIL_N] = {
    6403635, 846154, 6979993, 4442679, 1362209, 48306, 4460757, 554416,
    3545687, 6767575, 976891, 8196974, 2286327, 420899, 2235985, 2939036,
    3833893, 260646, 1104333, 1667432, 6470041, 1803090, 6656817, 426683,
    7908339, 6662682, 975884, 6167306, 8110657, 4513516, 4856520, 3038916,
    1799107, 3694233, 6727783, 7570268, 5366416, 6764025, 8217573, 3183426,
    1207385, 8194886, 5011305, 6423145, 164721, 5925962, 5948022, 2013608,
    3776993, 7786281, 3724270, 2584293, 1846953, 1671176, 2831860, 542412,
    4974386, 6144537, 7603226, 6880252, 1374803, 2546312, 6463336, 1279661,
    1962642, 5074302, 7067962, 451100, 1430225, 3318210, 7143142, 1333058,
    1050970, 6476982, 6511298, 2994039, 3548272, 5744496, 7129923, 3767016,
    6784443, 5894064, 7132797, 4325093, 7115408, 2590150, 5688936, 5538076,
    8177373, 6644538, 3342277, 4943130, 4272102, 2437823, 8093429, 8038120,
    3595838, 768622, 525098, 3556995, 5173371, 6348669, 3122442, 655327,
    522500, 43260, 1613174, 7884926, 7561383, 7470875, 6521319, 7479715,
    3193378, 1197226, 3759364, 3520352, 4867236, 1235728, 5945978, 8113420,
    3562462, 2446433, 6136326, 3342478, 4562441, 6063917, 4972711, 6288750,
    4540456, 3628969, 3881060, 3019102, 1439742, 812732, 1584928, 7094748,
    7039087, 7064828, 177440, 2409325, 1851402, 5220671, 3553272, 8190869,
    1316856, 7620448, 210977, 5991061, 3249728, 6727353, 8578, 3724342,
    4421799, 7475901, 1100098, 8336129, 5282425, 7871466, 8115473, 3343383,
    1430430, 6527646, 7031341, 381987, 1308169, 22981, 1228525, 671102,
    2477047, 411027, 3693493, 2967645, 5665122, 6232521, 983419, 4968207,
    8253495, 3632928, 3157330, 3190144, 1000202, 4083598, 6441103, 1257611,
    1585221, 6203962, 4904467, 1452451, 3041255, 3677745, 1528703, 3930395,
    2797779, 6308525, 2556880, 4479693, 4499374, 7426187, 7849063, 7568473,
    4680821, 1600420, 2140649, 4873154, 3821735, 4874723, 1643818, 1699267,
    539299, 6031717, 300467, 4840449, 2867647, 4805995, 3043716, 3861115,
    4464978, 2537516, 3592148, 1661693, 4849980, 5303092, 8284641, 5674394,
    8100412, 4369920, 19422, 6623180, 3277672, 1399561, 3859737, 2118186,
    2108549, 5760665, 1119584, 549488, 4794489, 1079900, 7356305, 5654953,
    5700314, 5268920, 2884855, 5260684, 2091905, 359251, 6026966, 6554070,
    7913949, 876248, 777960, 8143293, 518909, 2608894, 8354570
};

static const int32_t s_intt_scale =
    (int32_t)((((uint64_t)DIL_MONT * DIL_MONT % DIL_Q)
               * (DIL_Q - 1) % DIL_Q)
               * ((DIL_Q - 1) >> 8) % DIL_Q);

/* ============================================================================
 * Vectorized Montgomery reduce-multiply: (a * b) * R^{-1} mod q
 * ============================================================================ */

__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline VEC_T
s_mont_reduce_mul_vec(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    return VEC_MONT_REDUCE_MUL(a_a, a_b, a_qinv, a_q);
}

#ifdef HVEC_LANES
__attribute__((target("avx512f,avx512bw,avx512vl")))
static inline HVEC_T
s_mont_reduce_mul_hvec(HVEC_T a_a, HVEC_T a_b, HVEC_T a_qinv, HVEC_T a_q)
{
    return HVEC_MONT_REDUCE_MUL(a_a, a_b, a_qinv, a_q);
}
#endif

/* ============================================================================
 * Forward NTT — Cooley–Tukey, specialized for Dilithium (Q=8380417, N=256)
 * ============================================================================ */

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_ntt_forward_avx512(int32_t a_coeffs[DIL_N])
{
    unsigned int l_len, l_start, l_j, l_k = 1;

    const VEC_T l_qinv_vec = VEC_SET1_32((int32_t)DIL_QINV);
    const VEC_T l_q_vec    = VEC_SET1_32(DIL_Q);

    for (l_len = DIL_N / 2; l_len >= VEC_LANES; l_len >>= 1) {
        for (l_start = 0; l_start < DIL_N; l_start = l_j + l_len) {
            VEC_T l_zv = VEC_SET1_32(s_zetas[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_mont_reduce_mul_vec(l_zv, l_b, l_qinv_vec, l_q_vec);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD32(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB32(l_a, l_t));
            }
        }
    }

#ifdef DIL_HAS_NTT_INNER
    for (l_start = 0; l_start < DIL_N; l_start += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + l_start);
        DIL_NTT_FWD_INNER(v, s_zetas, l_k, l_start / VEC_LANES,
                           l_qinv_vec, l_q_vec);
        VEC_STORE(a_coeffs + l_start, v);
    }
#else
    for (; l_len >= 1; l_len >>= 1) {
        for (l_start = 0; l_start < DIL_N; l_start = l_j + l_len) {
            int32_t l_zeta = s_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_u = (uint32_t)((int64_t)l_zeta * a_coeffs[l_j + l_len])
                               * DIL_QINV;
                int32_t l_t = (int32_t)(((int64_t)l_zeta * a_coeffs[l_j + l_len]
                               + (int64_t)l_u * DIL_Q) >> 32);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
#endif
}

/* ============================================================================
 * Inverse NTT with fused Montgomery scaling — Gentleman–Sande
 * ============================================================================ */

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_ntt_inverse_avx512(int32_t a_coeffs[DIL_N])
{
    unsigned int l_start, l_len, l_j, l_k = 0;

#ifdef DIL_HAS_NTT_INNER
    {
        const VEC_T l_qinv_vec = VEC_SET1_32((int32_t)DIL_QINV);
        const VEC_T l_q_vec    = VEC_SET1_32(DIL_Q);
        for (l_start = 0; l_start < DIL_N; l_start += VEC_LANES) {
            VEC_T v = VEC_LOAD(a_coeffs + l_start);
            DIL_NTT_INV_INNER(v, s_zetas_inv, l_k, l_start / VEC_LANES,
                               l_qinv_vec, l_q_vec);
            VEC_STORE(a_coeffs + l_start, v);
        }
    }
    {
        unsigned _nblk = DIL_N / VEC_LANES;
        l_k = _nblk * (VEC_LANES / 2)
            + _nblk * (VEC_LANES / 4)
            + _nblk * (VEC_LANES / 8);
        if (VEC_LANES >= 16)
            l_k += _nblk * (VEC_LANES / 16);
        l_len = VEC_LANES;
    }
#else
    for (l_len = 1; l_len < VEC_LANES && l_len < DIL_N; l_len <<= 1) {
        for (l_start = 0; l_start < DIL_N; l_start = l_j + l_len) {
            int32_t l_zeta = s_zetas_inv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t    = a_coeffs[l_j];
                int32_t l_b    = a_coeffs[l_j + l_len];
                a_coeffs[l_j]          = l_t + l_b;
                int64_t l_diff = (int64_t)l_zeta * (l_t - l_b);
                uint32_t l_u = (uint32_t)l_diff * DIL_QINV;
                a_coeffs[l_j + l_len]  = (int32_t)((l_diff + (int64_t)l_u * DIL_Q) >> 32);
            }
        }
    }
#endif

    {
        const VEC_T l_qinv_vec = VEC_SET1_32((int32_t)DIL_QINV);
        const VEC_T l_q_vec    = VEC_SET1_32(DIL_Q);

        for (; l_len <= DIL_N / 2; l_len <<= 1) {
            for (l_start = 0; l_start < DIL_N; l_start = l_j + l_len) {
                VEC_T l_zv = VEC_SET1_32(s_zetas_inv[l_k++]);
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

        VEC_T l_fv = VEC_SET1_32(s_intt_scale);
        for (unsigned l_i = 0; l_i < DIL_N; l_i += VEC_LANES) {
            VEC_T v = VEC_LOAD(a_coeffs + l_i);
            VEC_STORE(a_coeffs + l_i,
                      s_mont_reduce_mul_vec(l_fv, v, l_qinv_vec, l_q_vec));
        }
    }
}

/* ============================================================================
 * Pointwise Montgomery multiply: c[i] = (a[i] * b[i]) * R^{-1} mod q
 * ============================================================================ */

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_dilithium_pointwise_mont_avx512(
    int32_t *a_c, const int32_t *a_a, const int32_t *a_b)
{
    const VEC_T l_qinv_vec = VEC_SET1_32((int32_t)DIL_QINV);
    const VEC_T l_q_vec    = VEC_SET1_32(DIL_Q);

    for (unsigned l_i = 0; l_i < DIL_N; l_i += VEC_LANES) {
        VEC_T l_a = VEC_LOAD(a_a + l_i);
        VEC_T l_b = VEC_LOAD(a_b + l_i);
        VEC_STORE(a_c + l_i,
                  s_mont_reduce_mul_vec(l_a, l_b, l_qinv_vec, l_q_vec));
    }
}
#endif
