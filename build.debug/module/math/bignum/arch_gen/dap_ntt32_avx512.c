#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/**
 * @file dap_ntt32_avx512.c
 * @brief AVX-512 SIMD-optimized 32-bit Montgomery-domain NTT
 * @details Generated from dap_ntt32_simd.c.tpl by dap_tpl
 *
 * Targets: Dilithium/ML-DSA (q=8380417, R=2^32) and similar lattices with
 * mont_r_bits = 32.  Uses the "raw" Montgomery reduce that keeps coefficients
 * in approximately (-q, q) throughout all layers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <immintrin.h>

#include "dap_ntt.h"
#include "dap_ntt_internal.h"

/* ============================================================================
 * AVX-512 Architecture-Specific SIMD Primitives
 * ============================================================================ */

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

/* ============================================================================
 * Vectorized Montgomery reduce-multiply: (a * b) * R^{-1} mod q
 *
 * The VEC_MONT_REDUCE_MUL macro is provided by the primitives file.
 * It handles the 32×32→64 widening multiply and R^{-1} reduction using
 * arch-specific SIMD techniques:
 *   x86:  even/odd lane trick with blend
 *   NEON: lo/hi half widening with narrow
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

#endif /* HVEC_LANES */

/* ============================================================================
 * Forward NTT — Cooley–Tukey, sequential zeta walk, Montgomery butterfly
 *
 * Input in standard order, output in bit-reversed order.
 * Loop: len = n/2 down to 1.
 * ============================================================================ */

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_ntt_forward_mont_avx512(int32_t *a_coeffs,
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_ntt_inverse_mont_avx512(int32_t *a_coeffs,
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

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_ntt_pointwise_montgomery_avx512(int32_t *a_c,
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
