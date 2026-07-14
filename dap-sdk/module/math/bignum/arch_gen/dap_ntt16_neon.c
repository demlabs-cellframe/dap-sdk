#if defined(__aarch64__) || defined(__arm__)
/**
 * @file dap_ntt16_neon.c
 * @brief NEON SIMD-optimized 16-bit NTT (Kyber-style, R = 2^16)
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
#include <arm_neon.h>

#include "dap_ntt.h"

/* ============================================================================
 * NEON Architecture-Specific SIMD Primitives
 * ============================================================================ */

// ARM NEON primitives for 16-bit polynomial/NTT ops (128-bit = 8 x int16_t)
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

typedef int16x8_t VEC_T;
#define VEC_LANES 8

#define VEC_ZERO()         vdupq_n_s16(0)

#define VEC_LOAD(p)        VEC_LOAD_S16(p)
#define VEC_STORE(p, v)    VEC_STORE_S16(p, v)
#define VEC_AND(a, b)      VEC_AND_S16(a, b)

#define VEC_SWAP_ADJACENT16(v)  vrev32q_s16(v)

static inline int16x8_t s_vec_blend_odd_s16(int16x8_t a, int16x8_t b) {
    static const uint16_t l_mask[8] = {0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF};
    return vbslq_s16(vld1q_u16(l_mask), b, a);
}
#define VEC_BLEND_ODD(a, b) s_vec_blend_odd_s16(a, b)

/* ============================================================================
 * Vectorized Montgomery field multiply: a * b * R^{-1} mod q
 * ============================================================================ */


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


static inline HVEC_T
s_fqmul_hvec(HVEC_T a_a, HVEC_T a_b, HVEC_T a_qinv, HVEC_T a_q)
{
    HVEC_T l_lo = HVEC_MULLO16(a_a, a_b);
    HVEC_T l_hi = HVEC_MULHI16(a_a, a_b);
    HVEC_T l_u  = HVEC_MULLO16(l_lo, a_qinv);
    HVEC_T l_uq = HVEC_MULHI16(l_u, a_q);
    return HVEC_SUB16(l_hi, l_uq);
}


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


void dap_ntt16_forward_neon(int16_t *a_coeffs,
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


void dap_ntt16_inverse_neon(int16_t *a_coeffs,
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


void dap_ntt16_basemul_neon(int16_t a_r[2],
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
