/**
 * @file dap_mlkem_ntt_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} specialized NTT16 for ML-KEM (Kyber)
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
{{ARCH_INCLUDES}}

{{#include PRIMITIVES_FILE}}

{{#include REDUCE_FILE}}

{{#include NTT_INNER_FILE}}

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

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_ntt_forward_{{ARCH_LOWER}}(int16_t a_coeffs[MLKEM_N])
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

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_ntt_inverse_{{ARCH_LOWER}}(int16_t a_coeffs[MLKEM_N])
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

{{TARGET_ATTR}} __attribute__((noinline))
void dap_mlkem_ntt_nttpack_{{ARCH_LOWER}}(int16_t a_coeffs[MLKEM_N])
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

{{TARGET_ATTR}} __attribute__((noinline))
void dap_mlkem_ntt_nttunpack_{{ARCH_LOWER}}(int16_t a_coeffs[MLKEM_N])
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
