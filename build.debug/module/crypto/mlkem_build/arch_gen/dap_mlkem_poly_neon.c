#if defined(__aarch64__) || defined(__arm__)
/**
 * @file dap_mlkem_poly_neon.c
 * @brief NEON SIMD-optimized ML-KEM polynomial helpers (heavy ops).
 * @details Generated from dap_mlkem_poly_simd.c.tpl by dap_tpl.
 *          Light ops (csubq, reduce, tomont, add, sub) are in the
 *          generated header dap_mlkem_poly_fast_neon.h.
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

#include <arm_neon.h>

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


static inline VEC_T s_fqmul_ext(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    VEC_T l_lo = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi = VEC_MULHI16(a_a, a_b);
    VEC_T l_u  = VEC_MULLO16(l_lo, a_qinv);
    VEC_T l_uq = VEC_MULHI16(l_u, a_q);
    return VEC_SUB16(l_hi, l_uq);
}


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

 DAP_MLKEM_POLY_FN_OPT
void dap_mlkem_poly_basemul_montgomery_neon(
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

 DAP_MLKEM_POLY_FN_OPT
void dap_mlkem_poly_basemul_acc_montgomery_neon(
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


void dap_mlkem_poly_compress_coeffs_neon(int16_t *a_coeffs,
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
