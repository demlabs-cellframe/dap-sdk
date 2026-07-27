#if defined(__aarch64__) || defined(__arm__)
/**
 * @file dap_mlkem_ntt_neon.c
 * @brief NEON specialized NTT16 for ML-KEM (Kyber)
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

/*
 * ML-KEM NTT scalar inner-layer stub.
 *
 * Does NOT define MLKEM_HAS_NTT_INNER — the universal template falls back
 * to the generic scalar loop for sub-VEC_LANES butterfly layers.
 * Does NOT define MLKEM_HAS_NTTPACK — the universal template uses the
 * portable scalar deinterleave.
 *
 * Included by dap_mlkem_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* No SIMD inner-layer or nttpack optimizations on this architecture. */

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

 __attribute__((noinline))
void dap_mlkem_ntt_forward_neon(int16_t a_coeffs[MLKEM_N])
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

 __attribute__((noinline))
void dap_mlkem_ntt_inverse_neon(int16_t a_coeffs[MLKEM_N])
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

 __attribute__((noinline))
void dap_mlkem_ntt_nttpack_neon(int16_t a_coeffs[MLKEM_N])
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

 __attribute__((noinline))
void dap_mlkem_ntt_nttunpack_neon(int16_t a_coeffs[MLKEM_N])
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
