/**
 * @file dap_ntt.h
 * @brief Unified Number Theoretic Transform (NTT) library for lattice-based cryptography.
 *
 * Two parameter flavours:
 *   dap_ntt_params_t   — int32_t coefficients (Chipmunk N=512 q=3168257, Dilithium N=256 q=8380417)
 *   dap_ntt_params16_t — int16_t coefficients (Kyber N=256 q=3329, and similar small-q rings)
 *
 * Dispatch is zero-overhead: static inline wrappers resolve function pointers
 * lazily on first call, then each subsequent call is a single indirect branch.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "dap_arch_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 32-bit parameter set (Chipmunk, Dilithium, …) ===== */

typedef struct dap_ntt_params {
    uint32_t n;
    int32_t  q;
    uint32_t qinv;            ///< -q^(-1) mod 2^mont_r_bits (unsigned)
    uint32_t mont_r_bits;     ///< R = 2^mont_r_bits
    uint32_t mont_r_mask;     ///< R - 1
    int32_t  one_over_n;      ///< n^(-1) mod q
    const int32_t *zetas;
    const int32_t *zetas_inv;
    uint32_t zetas_len;
} dap_ntt_params_t;

/* ===== 16-bit parameter set (Kyber, and similar small-q rings with R=2^16) ===== */

typedef struct dap_ntt_params16 {
    uint16_t n;
    int16_t  q;
    int16_t  qinv;            ///< q^(-1) mod 2^16 (signed, as in Kyber)
    const int16_t *zetas;
    const int16_t *zetas_inv;
    uint16_t zetas_len;
} dap_ntt_params16_t;

/* ===== Dispatch pointer declarations (storage is in dap_ntt_dispatch.c) ===== */

DAP_DISPATCH_DECLARE(dap_ntt_forward,                void, int32_t *, const dap_ntt_params_t *);
DAP_DISPATCH_DECLARE(dap_ntt_inverse,                void, int32_t *, const dap_ntt_params_t *);
DAP_DISPATCH_DECLARE(dap_ntt_forward_mont,           void, int32_t *, const dap_ntt_params_t *);
DAP_DISPATCH_DECLARE(dap_ntt_inverse_mont,           void, int32_t *, const dap_ntt_params_t *);
DAP_DISPATCH_DECLARE(dap_ntt_pointwise_montgomery,   void, int32_t *, const int32_t *, const int32_t *, const dap_ntt_params_t *);
DAP_DISPATCH_DECLARE(dap_ntt16_forward,              void, int16_t *, const dap_ntt_params16_t *);
DAP_DISPATCH_DECLARE(dap_ntt16_inverse,              void, int16_t *, const dap_ntt_params16_t *);
DAP_DISPATCH_DECLARE(dap_ntt16_basemul,              void, int16_t [2], const int16_t [2], const int16_t [2], int16_t, const dap_ntt_params16_t *);

extern void dap_ntt_dispatch_init(void);

/* ===== 32-bit public API — static inline for zero-overhead dispatch ===== */

static inline void dap_ntt_forward(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt_forward, dap_ntt_dispatch_init);
    dap_ntt_forward_ptr(a_coeffs, a_params);
}

static inline void dap_ntt_inverse(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt_inverse, dap_ntt_dispatch_init);
    dap_ntt_inverse_ptr(a_coeffs, a_params);
}

static inline void dap_ntt_forward_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt_forward_mont, dap_ntt_dispatch_init);
    dap_ntt_forward_mont_ptr(a_coeffs, a_params);
}

static inline void dap_ntt_inverse_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt_inverse_mont, dap_ntt_dispatch_init);
    dap_ntt_inverse_mont_ptr(a_coeffs, a_params);
}

static inline void dap_ntt_pointwise_montgomery(int32_t *a_c, const int32_t *a_a,
                                                const int32_t *a_b, const dap_ntt_params_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt_pointwise_montgomery, dap_ntt_dispatch_init);
    dap_ntt_pointwise_montgomery_ptr(a_c, a_a, a_b, a_params);
}

/* ===== 16-bit public API — static inline for zero-overhead dispatch ===== */

static inline void dap_ntt16_forward(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt16_forward, dap_ntt_dispatch_init);
    dap_ntt16_forward_ptr(a_coeffs, a_params);
}

static inline void dap_ntt16_inverse(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt16_inverse, dap_ntt_dispatch_init);
    dap_ntt16_inverse_ptr(a_coeffs, a_params);
}

static inline void dap_ntt16_basemul(int16_t a_r[2], const int16_t a_a[2], const int16_t a_b[2],
                                     int16_t a_zeta, const dap_ntt_params16_t *a_params)
{
    DAP_DISPATCH_ENSURE(dap_ntt16_basemul, dap_ntt_dispatch_init);
    dap_ntt16_basemul_ptr(a_r, a_a, a_b, a_zeta, a_params);
}

/* ===== Arithmetic helpers (always inline, no dispatch needed) ===== */

static inline int32_t dap_ntt_montgomery_reduce(int64_t a_value, const dap_ntt_params_t *a_params)
{
    uint32_t l_u = (uint32_t)(a_value & a_params->mont_r_mask) * a_params->qinv;
    l_u &= a_params->mont_r_mask;
    int64_t l_t = a_value + (int64_t)l_u * a_params->q;
    int32_t l_result = (int32_t)(l_t >> a_params->mont_r_bits);
    if (l_result >= a_params->q) l_result -= a_params->q;
    if (l_result < 0)           l_result += a_params->q;
    return l_result;
}

static inline int32_t dap_ntt_barrett_reduce(int32_t a_value, const dap_ntt_params_t *a_params)
{
    int32_t l_q = a_params->q;
    int32_t l_v = (int32_t)((int64_t)a_value * ((1LL << 26) / l_q) >> 26);
    int32_t l_t = a_value - l_v * l_q;
    if (l_t >= l_q) l_t -= l_q;
    if (l_t < 0)    l_t += l_q;
    return l_t;
}

static inline int16_t dap_ntt16_montgomery_reduce(int32_t a_value, const dap_ntt_params16_t *a_params)
{
    int16_t l_u = (int16_t)a_value * a_params->qinv;
    int32_t l_t = a_value - (int32_t)l_u * a_params->q;
    return (int16_t)(l_t >> 16);
}

static inline int16_t dap_ntt16_barrett_reduce(int16_t a_value, const dap_ntt_params16_t *a_params)
{
    int16_t l_v = (int16_t)(((1U << 26) + a_params->q / 2) / a_params->q);
    int16_t l_t = (int16_t)((int32_t)l_v * a_value >> 26);
    l_t *= a_params->q;
    return a_value - l_t;
}

static inline int16_t dap_ntt16_fqmul(int16_t a_a, int16_t a_b, const dap_ntt_params16_t *a_params)
{
    return dap_ntt16_montgomery_reduce((int32_t)a_a * a_b, a_params);
}

/**
 * 32-bit butterfly add/sub with two's-complement wrap (matches SIMD vpadd/vpsub).
 * Avoids signed-int overflow UB while preserving Dilithium Montgomery NTT semantics.
 */
static inline int32_t dap_ntt_i32_add_wrap(int32_t a_a, int32_t a_b)
{
    return (int32_t)((uint32_t)a_a + (uint32_t)a_b);
}

static inline int32_t dap_ntt_i32_sub_wrap(int32_t a_a, int32_t a_b)
{
    return (int32_t)((uint32_t)a_a - (uint32_t)a_b);
}

#ifdef __cplusplus
}
#endif
