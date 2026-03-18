/**
 * @file dap_ntt_dispatch.c
 * @brief Runtime dispatch layer for NTT: selects reference or SIMD backend.
 *
 * Uses dap_cpu_arch_get() for architecture detection.
 * Function pointers are initialized lazily on first call.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_ntt_internal.h"
#include "dap_cpu_arch.h"

/* ===== Function pointer types ===== */

typedef void (*ntt32_fn)(int32_t *, const dap_ntt_params_t *);
typedef void (*ntt32_pw_fn)(int32_t *, const int32_t *, const int32_t *, const dap_ntt_params_t *);

typedef void (*ntt16_fn)(int16_t *, const dap_ntt_params16_t *);
typedef void (*ntt16_bm_fn)(int16_t [2], const int16_t [2], const int16_t [2],
                            int16_t, const dap_ntt_params16_t *);

/* ===== Global function pointers ===== */

static ntt32_fn    s_ntt_forward          = NULL;
static ntt32_fn    s_ntt_inverse          = NULL;
static ntt32_fn    s_ntt_forward_mont     = NULL;
static ntt32_fn    s_ntt_inverse_mont     = NULL;
static ntt32_pw_fn s_ntt_pw_montgomery    = NULL;

static ntt16_fn    s_ntt16_forward        = NULL;
static ntt16_fn    s_ntt16_inverse        = NULL;
static ntt16_bm_fn s_ntt16_basemul        = NULL;

/* ===== Lazy initialization ===== */

static void s_dispatch_init(void)
{
    s_ntt_forward       = dap_ntt_forward_ref;
    s_ntt_inverse       = dap_ntt_inverse_ref;
    s_ntt_forward_mont  = dap_ntt_forward_mont_ref;
    s_ntt_inverse_mont  = dap_ntt_inverse_mont_ref;
    s_ntt_pw_montgomery = dap_ntt_pointwise_montgomery_ref;

    s_ntt16_forward     = dap_ntt16_forward_ref;
    s_ntt16_inverse     = dap_ntt16_inverse_ref;
    s_ntt16_basemul     = dap_ntt16_basemul_ref;

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    dap_cpu_arch_t l_arch = dap_cpu_arch_get_best();
    if (l_arch >= DAP_CPU_ARCH_AVX512) {
        s_ntt16_forward = dap_ntt16_forward_avx512;
        s_ntt16_inverse = dap_ntt16_inverse_avx512;
        s_ntt16_basemul = dap_ntt16_basemul_avx512;
    } else if (l_arch >= DAP_CPU_ARCH_AVX2) {
        s_ntt16_forward = dap_ntt16_forward_avx2;
        s_ntt16_inverse = dap_ntt16_inverse_avx2;
        s_ntt16_basemul = dap_ntt16_basemul_avx2;
    }
#elif defined(__aarch64__) || defined(__arm__)
    dap_cpu_arch_t l_arch = dap_cpu_arch_get_best();
    if (l_arch >= DAP_CPU_ARCH_NEON) {
        s_ntt16_forward = dap_ntt16_forward_neon;
        s_ntt16_inverse = dap_ntt16_inverse_neon;
        s_ntt16_basemul = dap_ntt16_basemul_neon;
    }
#endif
}

/* ===== 32-bit public API ===== */

void dap_ntt_forward(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    if (__builtin_expect(!s_ntt_forward, 0))
        s_dispatch_init();
    s_ntt_forward(a_coeffs, a_params);
}

void dap_ntt_inverse(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    if (__builtin_expect(!s_ntt_inverse, 0))
        s_dispatch_init();
    s_ntt_inverse(a_coeffs, a_params);
}

void dap_ntt_forward_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    if (__builtin_expect(!s_ntt_forward_mont, 0))
        s_dispatch_init();
    s_ntt_forward_mont(a_coeffs, a_params);
}

void dap_ntt_inverse_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    if (__builtin_expect(!s_ntt_inverse_mont, 0))
        s_dispatch_init();
    s_ntt_inverse_mont(a_coeffs, a_params);
}

void dap_ntt_pointwise_montgomery(int32_t *a_c, const int32_t *a_a,
                                  const int32_t *a_b, const dap_ntt_params_t *a_params)
{
    if (__builtin_expect(!s_ntt_pw_montgomery, 0))
        s_dispatch_init();
    s_ntt_pw_montgomery(a_c, a_a, a_b, a_params);
}

/* ===== 16-bit public API ===== */

void dap_ntt16_forward(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    if (__builtin_expect(!s_ntt16_forward, 0))
        s_dispatch_init();
    s_ntt16_forward(a_coeffs, a_params);
}

void dap_ntt16_inverse(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    if (__builtin_expect(!s_ntt16_inverse, 0))
        s_dispatch_init();
    s_ntt16_inverse(a_coeffs, a_params);
}

void dap_ntt16_basemul(int16_t a_r[2], const int16_t a_a[2], const int16_t a_b[2],
                       int16_t a_zeta, const dap_ntt_params16_t *a_params)
{
    if (__builtin_expect(!s_ntt16_basemul, 0))
        s_dispatch_init();
    s_ntt16_basemul(a_r, a_a, a_b, a_zeta, a_params);
}
