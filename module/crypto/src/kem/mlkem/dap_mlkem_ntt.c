/**
 * @file dap_mlkem_ntt.c
 * @brief ML-KEM NTT — specialized dispatch to arch-specific implementations.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_mlkem_ntt.h"
#include "dap_ntt.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"

#include <string.h>

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

const int16_t *MLKEM_NAMESPACE(_get_zetas)(void) { return s_zetas; }

/* Specialized NTT — generated from dap_mlkem_ntt_simd.c.tpl */
#if defined(__x86_64__) || defined(_M_X64)
void dap_mlkem_ntt_forward_avx2(int16_t a_coeffs[256]);
void dap_mlkem_ntt_inverse_avx2(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttpack_avx2(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttunpack_avx2(int16_t a_coeffs[256]);
void dap_mlkem_ntt_forward_avx2_512vl(int16_t a_coeffs[256]);
void dap_mlkem_ntt_inverse_avx2_512vl(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttpack_avx2_512vl(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttunpack_avx2_512vl(int16_t a_coeffs[256]);
/* Hand-written AVX2+AVX-512VL assembly NTT (32 YMM registers, zero spills) */
void dap_mlkem_ntt_forward_asm(int16_t a_coeffs[256]);
void dap_mlkem_ntt_inverse_asm(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttpack_asm(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttunpack_asm(int16_t a_coeffs[256]);
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
void dap_mlkem_ntt_forward_neon(int16_t a_coeffs[256]);
void dap_mlkem_ntt_inverse_neon(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttpack_neon(int16_t a_coeffs[256]);
void dap_mlkem_ntt_nttunpack_neon(int16_t a_coeffs[256]);
#endif

typedef void (*mlkem_ntt_fn_t)(int16_t *);
static mlkem_ntt_fn_t s_ntt_forward_fn = NULL;
static mlkem_ntt_fn_t s_ntt_inverse_fn = NULL;
static mlkem_ntt_fn_t s_ntt_nttpack_fn = NULL;
static mlkem_ntt_fn_t s_ntt_nttunpack_fn = NULL;

/* Scalar fallback using generic NTT */
static dap_ntt_params16_t s_ntt_params;

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

static void s_nttpack_scalar(int16_t *a_coeffs)
{
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[l_j]      = l_blk[2 * l_j];
            l_tmp[16 + l_j] = l_blk[2 * l_j + 1];
        }
        memcpy(l_blk, l_tmp, 64);
    }
}

static void s_nttunpack_scalar(int16_t *a_coeffs)
{
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[2 * l_j]     = l_blk[l_j];
            l_tmp[2 * l_j + 1] = l_blk[16 + l_j];
        }
        memcpy(l_blk, l_tmp, 64);
    }
}

static void s_ntt_forward_generic(int16_t *a_coeffs)
{
    dap_ntt16_forward(a_coeffs, &s_ntt_params);
    s_nttpack_scalar(a_coeffs);
}
static void s_ntt_inverse_generic(int16_t *a_coeffs)
{
    s_nttunpack_scalar(a_coeffs);
    dap_ntt16_inverse(a_coeffs, &s_ntt_params);
}

/*
 * SIMD wrappers: _ntt() = forward + nttpack, _invntt() = nttunpack + inverse.
 * Callers (indcpa) rely on _ntt() returning nttpacked data.
 */
#if DAP_CPU_DETECT_X86
static void s_ntt_forward_avx2_512vl_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_forward_avx2_512vl(a_coeffs);
    dap_mlkem_ntt_nttpack_avx2_512vl(a_coeffs);
}
static void s_ntt_inverse_avx2_512vl_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_nttunpack_avx2_512vl(a_coeffs);
    dap_mlkem_ntt_inverse_avx2_512vl(a_coeffs);
}
static void s_ntt_forward_avx2_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_forward_avx2(a_coeffs);
    dap_mlkem_ntt_nttpack_avx2(a_coeffs);
}
static void s_ntt_inverse_avx2_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_nttunpack_avx2(a_coeffs);
    dap_mlkem_ntt_inverse_avx2(a_coeffs);
}
/* ASM NTT already includes nttpack/nttunpack internally (SHUFFLE1 stage) */
#endif
#if DAP_CPU_DETECT_ARM
static void s_ntt_forward_neon_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_forward_neon(a_coeffs);
    dap_mlkem_ntt_nttpack_neon(a_coeffs);
}
static void s_ntt_inverse_neon_packed(int16_t *a_coeffs)
{
    dap_mlkem_ntt_nttunpack_neon(a_coeffs);
    dap_mlkem_ntt_inverse_neon(a_coeffs);
}
#endif

static void s_resolve_ntt(void)
{
    s_ntt_params.n         = MLKEM_N;
    s_ntt_params.q         = MLKEM_Q;
    s_ntt_params.qinv      = MLKEM_QINV;
    s_ntt_params.zetas     = s_zetas;
    s_ntt_params.zetas_inv = s_zetas_inv;
    s_ntt_params.zetas_len = 128;

    s_ntt_forward_fn  = s_ntt_forward_generic;
    s_ntt_inverse_fn  = s_ntt_inverse_generic;
    s_ntt_nttpack_fn  = s_nttpack_scalar;
    s_ntt_nttunpack_fn = s_nttunpack_scalar;

#if DAP_CPU_DETECT_X86
    if (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512) {
        s_ntt_forward_fn  = dap_mlkem_ntt_forward_asm;
        s_ntt_inverse_fn  = dap_mlkem_ntt_inverse_asm;
        s_ntt_nttpack_fn  = dap_mlkem_ntt_nttpack_asm;
        s_ntt_nttunpack_fn = dap_mlkem_ntt_nttunpack_asm;
    } else if (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2) {
        s_ntt_forward_fn  = s_ntt_forward_avx2_packed;
        s_ntt_inverse_fn  = s_ntt_inverse_avx2_packed;
        s_ntt_nttpack_fn  = dap_mlkem_ntt_nttpack_avx2;
        s_ntt_nttunpack_fn = dap_mlkem_ntt_nttunpack_avx2;
    }
#endif
#if DAP_CPU_DETECT_ARM
    if (dap_cpu_arch_get() >= DAP_CPU_ARCH_NEON) {
        s_ntt_forward_fn  = s_ntt_forward_neon_packed;
        s_ntt_inverse_fn  = s_ntt_inverse_neon_packed;
        s_ntt_nttpack_fn  = dap_mlkem_ntt_nttpack_neon;
        s_ntt_nttunpack_fn = dap_mlkem_ntt_nttunpack_neon;
    }
#endif
}

static inline void s_ensure_init(void)
{
    if (__builtin_expect(!s_ntt_forward_fn, 0))
        s_resolve_ntt();
}

void MLKEM_NAMESPACE(_ntt)(int16_t a_coeffs[MLKEM_N])
{
    s_ensure_init();
    s_ntt_forward_fn(a_coeffs);
}

void MLKEM_NAMESPACE(_invntt)(int16_t a_coeffs[MLKEM_N])
{
    s_ensure_init();
    s_ntt_inverse_fn(a_coeffs);
}

void MLKEM_NAMESPACE(_nttpack)(int16_t a_coeffs[MLKEM_N])
{
    s_ensure_init();
    s_ntt_nttpack_fn(a_coeffs);
}

void MLKEM_NAMESPACE(_nttunpack)(int16_t a_coeffs[MLKEM_N])
{
    s_ensure_init();
    s_ntt_nttunpack_fn(a_coeffs);
}

void MLKEM_NAMESPACE(_basemul)(int16_t a_r[2], const int16_t a_a[2],
                                const int16_t a_b[2], int16_t a_zeta)
{
    int16_t l_qinv = MLKEM_QINV;
    int32_t t;

    t = (int32_t)a_a[1] * a_b[1];
    a_r[0] = (int16_t)((t - (int32_t)((int16_t)t * l_qinv) * MLKEM_Q) >> 16);
    t = (int32_t)a_r[0] * a_zeta;
    a_r[0] = (int16_t)((t - (int32_t)((int16_t)t * l_qinv) * MLKEM_Q) >> 16);
    t = (int32_t)a_a[0] * a_b[0];
    a_r[0] += (int16_t)((t - (int32_t)((int16_t)t * l_qinv) * MLKEM_Q) >> 16);

    t = (int32_t)a_a[0] * a_b[1];
    a_r[1] = (int16_t)((t - (int32_t)((int16_t)t * l_qinv) * MLKEM_Q) >> 16);
    t = (int32_t)a_a[1] * a_b[0];
    a_r[1] += (int16_t)((t - (int32_t)((int16_t)t * l_qinv) * MLKEM_Q) >> 16);
}
