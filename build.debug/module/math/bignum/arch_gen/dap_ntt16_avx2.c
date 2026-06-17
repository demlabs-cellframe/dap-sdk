#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_ntt16_avx2.c
 * @brief AVX2 SIMD-optimized 16-bit NTT (Kyber-style, R = 2^16)
 * @details Generated from dap_ntt16_simd.c.tpl by dap_tpl
 *
 * Optimization notes:
 *   
 *
 * Performance target: 
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <immintrin.h>

#include "dap_ntt.h"

/* ============================================================================
 * AVX2 Architecture-Specific SIMD Primitives
 * ============================================================================ */

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

/* ============================================================================
 * Vectorized Montgomery field multiply: a * b * R^{-1} mod q
 * ============================================================================ */

__attribute__((target("avx2")))
static inline VEC_T
s_fqmul_vec(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    VEC_T l_lo = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi = VEC_MULHI16(a_a, a_b);
    VEC_T l_u  = VEC_MULLO16(l_lo, a_qinv);
    VEC_T l_uq = VEC_MULHI16(l_u, a_q);
    return VEC_SUB16(l_hi, l_uq);
}

/* ============================================================================
 * Vectorized Barrett reduction: a mod q
 * v = ((1 << 26) + q/2) / q, result = a − ((a·v) >> 26) · q
 * mulhi gives >> 16, shift right 10 more for >> 26 total
 * ============================================================================ */

__attribute__((target("avx2")))
static inline VEC_T
s_barrett_reduce_vec(VEC_T a_val, VEC_T a_v, VEC_T a_q)
{
    VEC_T l_bt = VEC_MULHI16(a_v, a_val);
    l_bt = VEC_SRAI16(l_bt, 10);
    l_bt = VEC_MULLO16(l_bt, a_q);
    return VEC_SUB16(a_val, l_bt);
}

/* ============================================================================
 * Half-width SIMD helpers (for the layer where len == VEC_LANES/2)
 * Absent on architectures that have no wider parent (e.g. NEON-only)
 * ============================================================================ */

#ifdef HVEC_LANES

__attribute__((target("avx2")))
static inline HVEC_T
s_fqmul_hvec(HVEC_T a_a, HVEC_T a_b, HVEC_T a_qinv, HVEC_T a_q)
{
    HVEC_T l_lo = HVEC_MULLO16(a_a, a_b);
    HVEC_T l_hi = HVEC_MULHI16(a_a, a_b);
    HVEC_T l_u  = HVEC_MULLO16(l_lo, a_qinv);
    HVEC_T l_uq = HVEC_MULHI16(l_u, a_q);
    return HVEC_SUB16(l_hi, l_uq);
}

__attribute__((target("avx2")))
static inline HVEC_T
s_barrett_reduce_hvec(HVEC_T a_val, HVEC_T a_v, HVEC_T a_q)
{
    HVEC_T l_bt = HVEC_MULHI16(a_v, a_val);
    l_bt = HVEC_SRAI16(l_bt, 10);
    l_bt = HVEC_MULLO16(l_bt, a_q);
    return HVEC_SUB16(a_val, l_bt);
}

#endif /* HVEC_LANES */

/* ============================================================================
 * Forward NTT — Cooley–Tukey, sequential zeta walk, Montgomery butterfly
 * ============================================================================ */

__attribute__((target("avx2")))
void dap_ntt16_forward_avx2(int16_t *a_coeffs,
                                       const dap_ntt_params16_t *a_params)
{
    const int16_t *l_z = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;
    const unsigned int l_n = a_params->n;

    VEC_T l_qinv_vec = VEC_SET1_16(a_params->qinv);
    VEC_T l_q_vec    = VEC_SET1_16(a_params->q);

    /* --- Full-vector SIMD layers (len >= VEC_LANES) --- */
    for (l_len = l_n / 2; l_len >= VEC_LANES; l_len >>= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            VEC_T l_zv = VEC_SET1_16(l_z[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_fqmul_vec(l_zv, l_b, l_qinv_vec, l_q_vec);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD16(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB16(l_a, l_t));
            }
        }
    }

#ifdef HVEC_LANES
    /* --- Half-vector layer (len == HVEC_LANES) --- */
    if (l_len == HVEC_LANES && l_len >= 2) {
        HVEC_T l_hqinv = HVEC_SET1_16(a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_16(a_params->q);
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_16(l_z[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_t = s_fqmul_hvec(l_zv, l_b, l_hqinv, l_hq);
                HVEC_STORE(a_coeffs + l_j,         HVEC_ADD16(l_a, l_t));
                HVEC_STORE(a_coeffs + l_j + l_len, HVEC_SUB16(l_a, l_t));
            }
        }
        l_len >>= 1;
    }
#endif

    /* --- Scalar fallback for remaining small layers --- */
    for (; l_len >= 2; l_len >>= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            int16_t l_zeta = l_z[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = dap_ntt16_fqmul(l_zeta, a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
}

/* ============================================================================
 * Inverse NTT — Gentleman–Sande, sequential zeta walk, Montgomery butterfly
 * ============================================================================ */

__attribute__((target("avx2")))
void dap_ntt16_inverse_avx2(int16_t *a_coeffs,
                                       const dap_ntt_params16_t *a_params)
{
    const int16_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;
    const unsigned int l_n = a_params->n;
    const unsigned int l_half_n = l_n / 2;

    /* Threshold where we switch from scalar to SIMD */
    unsigned int l_simd_start = VEC_LANES;
#ifdef HVEC_LANES
    l_simd_start = HVEC_LANES;
#endif

    /* --- Scalar layers: len from 2 up to (but not including) l_simd_start --- */
    for (l_len = 2; l_len < l_simd_start && l_len <= l_half_n; l_len <<= 1) {
        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            int16_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t   = a_coeffs[l_j];
                a_coeffs[l_j]         = dap_ntt16_barrett_reduce(
                    l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = dap_ntt16_fqmul(
                    l_zeta, l_t - a_coeffs[l_j + l_len], a_params);
            }
        }
    }

#ifdef HVEC_LANES
    /* --- Half-vector layer --- */
    if (l_len == HVEC_LANES && l_len <= l_half_n) {
        HVEC_T l_hqinv = HVEC_SET1_16(a_params->qinv);
        HVEC_T l_hq    = HVEC_SET1_16(a_params->q);
        int16_t l_v_sc = (int16_t)(((1U << 26) + a_params->q / 2) / a_params->q);
        HVEC_T l_hv    = HVEC_SET1_16(l_v_sc);

        for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_16(l_zinv[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a   = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b   = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_sum = HVEC_ADD16(l_a, l_b);
                HVEC_T l_dif = HVEC_SUB16(l_a, l_b);
                HVEC_STORE(a_coeffs + l_j,
                           s_barrett_reduce_hvec(l_sum, l_hv, l_hq));
                HVEC_STORE(a_coeffs + l_j + l_len,
                           s_fqmul_hvec(l_zv, l_dif, l_hqinv, l_hq));
            }
        }
        l_len <<= 1;
    }
#endif

    /* --- Full-vector SIMD layers --- */
    {
        VEC_T l_qinv_vec = VEC_SET1_16(a_params->qinv);
        VEC_T l_q_vec    = VEC_SET1_16(a_params->q);
        int16_t l_v_sc   = (int16_t)(((1U << 26) + a_params->q / 2) / a_params->q);
        VEC_T l_v_vec    = VEC_SET1_16(l_v_sc);

        for (; l_len <= l_half_n; l_len <<= 1) {
            for (l_start = 0; l_start < l_n; l_start = l_j + l_len) {
                VEC_T l_zv = VEC_SET1_16(l_zinv[l_k++]);
                for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                    VEC_T l_a   = VEC_LOAD(a_coeffs + l_j);
                    VEC_T l_b   = VEC_LOAD(a_coeffs + l_j + l_len);
                    VEC_T l_sum = VEC_ADD16(l_a, l_b);
                    VEC_T l_dif = VEC_SUB16(l_a, l_b);
                    VEC_STORE(a_coeffs + l_j,
                              s_barrett_reduce_vec(l_sum, l_v_vec, l_q_vec));
                    VEC_STORE(a_coeffs + l_j + l_len,
                              s_fqmul_vec(l_zv, l_dif, l_qinv_vec, l_q_vec));
                }
            }
        }
    }

    /* --- Final scaling by zetas_inv[zetas_len - 1] --- */
    {
        int16_t  l_scale = l_zinv[a_params->zetas_len - 1];
        VEC_T l_sv    = VEC_SET1_16(l_scale);
        VEC_T l_qinv  = VEC_SET1_16(a_params->qinv);
        VEC_T l_q     = VEC_SET1_16(a_params->q);
        unsigned int l_i;

        for (l_i = 0; l_i + VEC_LANES <= l_n; l_i += VEC_LANES) {
            VEC_T l_c = VEC_LOAD(a_coeffs + l_i);
            VEC_STORE(a_coeffs + l_i,
                      s_fqmul_vec(l_c, l_sv, l_qinv, l_q));
        }
        for (; l_i < l_n; l_i++)
            a_coeffs[l_i] = dap_ntt16_fqmul(a_coeffs[l_i], l_scale, a_params);
    }
}

/* ============================================================================
 * Basemul — polynomial multiply in Zq[X]/(X²−ζ), 2-element pairs
 * Scalar only: SIMD has no benefit on 2-element pairs
 * ============================================================================ */

__attribute__((target("avx2")))
void dap_ntt16_basemul_avx2(int16_t a_r[2],
                                       const int16_t a_a[2],
                                       const int16_t a_b[2],
                                       int16_t a_zeta,
                                       const dap_ntt_params16_t *a_params)
{
    a_r[0]  = dap_ntt16_fqmul(a_a[1], a_b[1], a_params);
    a_r[0]  = dap_ntt16_fqmul(a_r[0], a_zeta, a_params);
    a_r[0] += dap_ntt16_fqmul(a_a[0], a_b[0], a_params);

    a_r[1]  = dap_ntt16_fqmul(a_a[0], a_b[1], a_params);
    a_r[1] += dap_ntt16_fqmul(a_a[1], a_b[0], a_params);
}
#endif
