/**
 * @file dap_ntt_internal.h
 * @brief Internal backend declarations for NTT dispatch layer.
 *
 * Not for direct use — call the public API from dap_ntt.h instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_ntt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 32-bit reference backends ===== */

void dap_ntt_forward_ref(int32_t *, const dap_ntt_params_t *);
void dap_ntt_inverse_ref(int32_t *, const dap_ntt_params_t *);
void dap_ntt_forward_mont_ref(int32_t *, const dap_ntt_params_t *);
void dap_ntt_inverse_mont_ref(int32_t *, const dap_ntt_params_t *);
void dap_ntt_pointwise_montgomery_ref(int32_t *, const int32_t *,
                                      const int32_t *, const dap_ntt_params_t *);

/* ===== 16-bit reference backends ===== */

void dap_ntt16_forward_ref(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_inverse_ref(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_basemul_ref(int16_t [2], const int16_t [2], const int16_t [2],
                           int16_t, const dap_ntt_params16_t *);

/* ===== 16-bit SIMD backends (generated from dap_ntt16_simd.c.tpl) ===== */

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

void dap_ntt16_forward_avx2(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_inverse_avx2(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_basemul_avx2(int16_t [2], const int16_t [2], const int16_t [2],
                            int16_t, const dap_ntt_params16_t *);

void dap_ntt16_forward_avx512(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_inverse_avx512(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_basemul_avx512(int16_t [2], const int16_t [2], const int16_t [2],
                              int16_t, const dap_ntt_params16_t *);

#elif defined(__aarch64__) || defined(__arm__)

void dap_ntt16_forward_neon(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_inverse_neon(int16_t *, const dap_ntt_params16_t *);
void dap_ntt16_basemul_neon(int16_t [2], const int16_t [2], const int16_t [2],
                            int16_t, const dap_ntt_params16_t *);

#endif

#ifdef __cplusplus
}
#endif
