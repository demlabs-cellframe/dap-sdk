#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_mlkem_poly_avx2.c
 * @brief AVX2 SIMD-optimized ML-KEM polynomial helpers (heavy ops).
 * @details Generated from dap_mlkem_poly_simd.c.tpl by dap_tpl.
 *          Light ops (csubq, reduce, tomont, add, sub) are in the
 *          generated header dap_mlkem_poly_fast_avx2.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>

/* Clang treats GCC's optimize() as unknown attribute (-Werror on Android NDK). */
#if defined(__clang__)
#define DAP_MLKEM_POLY_FN_OPT
#else
#define DAP_MLKEM_POLY_FN_OPT __attribute__((optimize("O3")))
#endif

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

#include "dap_mlkem_poly_simd.h"

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

/* ============================================================================
 * basemul_montgomery: NTT-domain polynomial multiply (nttpack layout)
 *
 * In nttpack layout, 256 coefficients are stored as 8 blocks of 32:
 *   block[p] = [even0..even15, odd0..odd15]
 * Each even/odd pair corresponds to an NTT pair (a[2i], a[2i+1]).
 * Zeta table: [+z0, -z0, +z1, -z1, ...] for each of 8 zetas per block.
 * ============================================================================ */

static const int16_t s_basemul_zetas_nttpack[128] = {
     2226, -2226,   430,  -430,   555,  -555,   843,  -843,
     2078, -2078,   871,  -871,  1550, -1550,   105,  -105,
      422,  -422,   587,  -587,   177,  -177,  3094, -3094,
     3038, -3038,  2869, -2869,  1574, -1574,  1653, -1653,
     3083, -3083,   778,  -778,  1159, -1159,  3182, -3182,
     2552, -2552,  1483, -1483,  2727, -2727,  1119, -1119,
     1739, -1739,   644,  -644,  2457, -2457,   349,  -349,
      418,  -418,   329,  -329,  3173, -3173,  3254, -3254,
      817,  -817,  1097, -1097,   603,  -603,   610,  -610,
     1322, -1322,  2044, -2044,  1864, -1864,   384,  -384,
     2114, -2114,  3193, -3193,  1218, -1218,  1994, -1994,
     2455, -2455,   220,  -220,  2142, -2142,  1670, -1670,
     2144, -2144,  1799, -1799,  2051, -2051,   794,  -794,
     1819, -1819,  2475, -2475,  2459, -2459,   478,  -478,
     3221, -3221,  3021, -3021,   996,  -996,   991,  -991,
      958,  -958,  1869, -1869,  1522, -1522,  1628, -1628,
};

__attribute__((target("avx2"))) DAP_MLKEM_POLY_FN_OPT
void dap_mlkem_poly_basemul_montgomery_avx2(
    int16_t *a_r, const int16_t *a_a, const int16_t *a_b, const int16_t *a_zetas)
{
    (void)a_zetas;
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);

    /* Odd half of each 32-coeff nttpack block starts at +16 (not +VEC_LANES: NEON VEC_LANES=8
     * would wrongly use +8 and read ae[8..15] as "odd"). Cover 16/VEC_LANES sub-blocks. */
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const unsigned l_base = 32 * l_p;
        for (unsigned l_sub = 0; l_sub < 16 / VEC_LANES; l_sub++) {
            const unsigned l_off = VEC_LANES * l_sub;
            VEC_T l_ae = VEC_LOAD(a_a + l_base + l_off);
            VEC_T l_ao = VEC_LOAD(a_a + l_base + 16 + l_off);
            VEC_T l_be = VEC_LOAD(a_b + l_base + l_off);
            VEC_T l_bo = VEC_LOAD(a_b + l_base + 16 + l_off);
            VEC_T l_z  = VEC_LOAD(s_basemul_zetas_nttpack + 16 * l_p + l_off);

            VEC_T l_re = VEC_ADD16(
                s_fqmul_ext(l_ae, l_be, l_qinv, l_q),
                s_fqmul_ext(s_fqmul_ext(l_ao, l_bo, l_qinv, l_q), l_z, l_qinv, l_q));
            VEC_T l_ro = VEC_ADD16(
                s_fqmul_ext(l_ae, l_bo, l_qinv, l_q),
                s_fqmul_ext(l_ao, l_be, l_qinv, l_q));

            VEC_STORE(a_r + l_base + l_off, l_re);
            VEC_STORE(a_r + l_base + 16 + l_off, l_ro);
        }
    }
}

/* ============================================================================
 * basemul_acc_montgomery: fused K basemul + accumulate + Barrett reduce
 * ============================================================================ */

__attribute__((target("avx2"))) DAP_MLKEM_POLY_FN_OPT
void dap_mlkem_poly_basemul_acc_montgomery_avx2(
    int16_t *a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    unsigned a_count)
{
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);
    const VEC_T l_bv   = VEC_SET1_16(20159);

    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const unsigned l_base = 32 * l_p;
        for (unsigned l_sub = 0; l_sub < 16 / VEC_LANES; l_sub++) {
            const unsigned l_off = VEC_LANES * l_sub;
            VEC_T l_acc_e = VEC_ZERO();
            VEC_T l_acc_o = VEC_ZERO();
            VEC_T l_z = VEC_LOAD(s_basemul_zetas_nttpack + 16 * l_p + l_off);

            for (unsigned k = 0; k < a_count; k++) {
                VEC_T l_ae = VEC_LOAD(a_polys_a[k] + l_base + l_off);
                VEC_T l_ao = VEC_LOAD(a_polys_a[k] + l_base + 16 + l_off);
                VEC_T l_be = VEC_LOAD(a_polys_b[k] + l_base + l_off);
                VEC_T l_bo = VEC_LOAD(a_polys_b[k] + l_base + 16 + l_off);

                VEC_T l_boz = s_fqmul_ext(l_bo, l_z, l_qinv, l_q);
                l_acc_e = VEC_ADD16(l_acc_e, VEC_ADD16(
                    s_fqmul_ext(l_ae, l_be, l_qinv, l_q),
                    s_fqmul_ext(l_ao, l_boz, l_qinv, l_q)));
                l_acc_o = VEC_ADD16(l_acc_o, VEC_ADD16(
                    s_fqmul_ext(l_ae, l_bo, l_qinv, l_q),
                    s_fqmul_ext(l_ao, l_be, l_qinv, l_q)));
            }

            VEC_T l_bt_e = VEC_SRAI16(VEC_MULHI16(l_acc_e, l_bv), 10);
            VEC_STORE(a_r + l_base + l_off,
                      VEC_SUB16(l_acc_e, VEC_MULLO16(l_bt_e, l_q)));
            VEC_T l_bt_o = VEC_SRAI16(VEC_MULHI16(l_acc_o, l_bv), 10);
            VEC_STORE(a_r + l_base + 16 + l_off,
                      VEC_SUB16(l_acc_o, VEC_MULLO16(l_bt_o, l_q)));
        }
    }
}

/* ============================================================================
 * compress_coeffs: round(x * 2^d / q) via mulhrs approximation
 * ============================================================================ */

__attribute__((target("avx2")))
void dap_mlkem_poly_compress_coeffs_avx2(int16_t *a_coeffs,
                                                     int16_t a_magic,
                                                     int16_t a_mask)
{
    const VEC_T l_c = VEC_SET1_16(a_magic);
    const VEC_T l_m = VEC_SET1_16(a_mask);
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        v = VEC_AND(VEC_MULHRS16(v, l_c), l_m);
        VEC_STORE(a_coeffs + i, v);
    }
}
#endif
