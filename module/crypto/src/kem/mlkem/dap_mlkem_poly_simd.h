/**
 * @file dap_mlkem_poly_simd.h
 * @brief SIMD-accelerated ML-KEM polynomial helpers — declarations & dispatch.
 *
 * Dispatch uses dap_cpu_arch_get() from DAP infrastructure for consistency
 * with NTT dispatch (DAP_DISPATCH). ARM NEON is always-on.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_cpu_arch.h"

#if defined(__x86_64__) || defined(_M_X64)
void dap_mlkem_poly_csubq_avx2(int16_t *a_coeffs);
void dap_mlkem_poly_reduce_avx2(int16_t *a_coeffs);
void dap_mlkem_poly_tomont_avx2(int16_t *a_coeffs);
void dap_mlkem_poly_basemul_montgomery_avx2(int16_t *a_r, const int16_t *a_a, const int16_t *a_b, const int16_t *a_zetas);
void dap_mlkem_poly_compress_coeffs_avx2(int16_t *a_coeffs, int16_t a_magic, int16_t a_mask);
void dap_mlkem_poly_add_avx2(int16_t *a_r, const int16_t *a_a, const int16_t *a_b);
void dap_mlkem_poly_sub_avx2(int16_t *a_r, const int16_t *a_a, const int16_t *a_b);
void dap_mlkem_poly_basemul_acc_montgomery_avx2(int16_t *a_r, const int16_t * const *a_polys_a, const int16_t * const *a_polys_b, unsigned a_count);
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
void dap_mlkem_poly_csubq_neon(int16_t *a_coeffs);
void dap_mlkem_poly_reduce_neon(int16_t *a_coeffs);
void dap_mlkem_poly_tomont_neon(int16_t *a_coeffs);
void dap_mlkem_poly_basemul_montgomery_neon(int16_t *a_r, const int16_t *a_a, const int16_t *a_b, const int16_t *a_zetas);
void dap_mlkem_poly_compress_coeffs_neon(int16_t *a_coeffs, int16_t a_magic, int16_t a_mask);
void dap_mlkem_poly_add_neon(int16_t *a_r, const int16_t *a_a, const int16_t *a_b);
void dap_mlkem_poly_sub_neon(int16_t *a_r, const int16_t *a_a, const int16_t *a_b);
void dap_mlkem_poly_basemul_acc_montgomery_neon(int16_t *a_r, const int16_t * const *a_polys_a, const int16_t * const *a_polys_b, unsigned a_count);
#endif

#if defined(__x86_64__) || defined(_M_X64)

static inline int dap_mlkem_poly_simd_available(void)
{
    return dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2;
}

#define MLKEM_SIMD_DISPATCH(func, ...) \
    (dap_mlkem_poly_simd_available() \
        ? (dap_mlkem_poly_##func##_avx2(__VA_ARGS__), 1) : 0)

#elif defined(__aarch64__) || defined(_M_ARM64)

static inline int dap_mlkem_poly_simd_available(void) { return 1; }

#define MLKEM_SIMD_DISPATCH(func, ...) \
    (dap_mlkem_poly_##func##_neon(__VA_ARGS__), 1)

#else

static inline int dap_mlkem_poly_simd_available(void) { return 0; }
#define MLKEM_SIMD_DISPATCH(func, ...) 0

#endif
