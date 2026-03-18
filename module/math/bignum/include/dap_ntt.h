/**
 * @file dap_ntt.h
 * @brief Unified Number Theoretic Transform (NTT) library for lattice-based cryptography.
 *
 * Two parameter flavours:
 *   dap_ntt_params_t   — int32_t coefficients (Chipmunk N=512 q=3168257, Dilithium N=256 q=8380417)
 *   dap_ntt_params16_t — int16_t coefficients (Kyber N=256 q=3329, and similar small-q rings)
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 32-bit API (Chipmunk, Dilithium, …) ===== */

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

/**
 * @brief Forward NTT: time-domain → frequency-domain (Chipmunk-style, table-driven CT/GS)
 */
void dap_ntt_forward(int32_t *a_coeffs, const dap_ntt_params_t *a_params);

/**
 * @brief Inverse NTT: frequency-domain → time-domain (Chipmunk-style, table-driven CT/GS)
 */
void dap_ntt_inverse(int32_t *a_coeffs, const dap_ntt_params_t *a_params);

/**
 * @brief Montgomery-domain forward NTT (Dilithium-compatible).
 *
 * Sequential zeta walking (k++), Montgomery reduction in the butterfly.
 * Zetas must be in "sequential CT/GS" order.
 */
void dap_ntt_forward_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params);

/**
 * @brief Montgomery-domain inverse NTT (Dilithium-compatible).
 */
void dap_ntt_inverse_mont(int32_t *a_coeffs, const dap_ntt_params_t *a_params);

/**
 * @brief Montgomery reduction: a * R^{-1} mod q
 */
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

/**
 * @brief Barrett reduction: a mod q
 */
static inline int32_t dap_ntt_barrett_reduce(int32_t a_value, const dap_ntt_params_t *a_params)
{
    int32_t l_q = a_params->q;
    int32_t l_v = (int32_t)((int64_t)a_value * ((1LL << 26) / l_q) >> 26);
    int32_t l_t = a_value - l_v * l_q;
    if (l_t >= l_q) l_t -= l_q;
    if (l_t < 0)    l_t += l_q;
    return l_t;
}

/**
 * @brief Pointwise Montgomery multiplication in NTT domain: c[i] = a[i]*b[i]*R^{-1} mod q
 */
void dap_ntt_pointwise_montgomery(int32_t *a_c, const int32_t *a_a,
                                  const int32_t *a_b, const dap_ntt_params_t *a_params);

/* ===== 16-bit API (Kyber, and similar small-q rings with R=2^16) ===== */

typedef struct dap_ntt_params16 {
    uint16_t n;
    int16_t  q;
    int16_t  qinv;            ///< q^(-1) mod 2^16 (signed, as in Kyber)
    const int16_t *zetas;
    const int16_t *zetas_inv;
    uint16_t zetas_len;
} dap_ntt_params16_t;

/**
 * @brief Forward NTT (Kyber-style Cooley–Tukey, Montgomery butterfly, stops at len=2)
 */
void dap_ntt16_forward(int16_t *a_coeffs, const dap_ntt_params16_t *a_params);

/**
 * @brief Inverse NTT (Kyber-style Gentleman–Sande, Montgomery butterfly)
 */
void dap_ntt16_inverse(int16_t *a_coeffs, const dap_ntt_params16_t *a_params);

/**
 * @brief Montgomery reduction for 16-bit domain: a * R^{-1} mod q, R=2^16
 */
static inline int16_t dap_ntt16_montgomery_reduce(int32_t a_value, const dap_ntt_params16_t *a_params)
{
    int16_t l_u = (int16_t)a_value * a_params->qinv;
    int32_t l_t = a_value - (int32_t)l_u * a_params->q;
    return (int16_t)(l_t >> 16);
}

/**
 * @brief Barrett reduction for 16-bit domain: a mod q
 */
static inline int16_t dap_ntt16_barrett_reduce(int16_t a_value, const dap_ntt_params16_t *a_params)
{
    int16_t l_v = (int16_t)(((1U << 26) + a_params->q / 2) / a_params->q);
    int16_t l_t = (int16_t)((int32_t)l_v * a_value >> 26);
    l_t *= a_params->q;
    return a_value - l_t;
}

/**
 * @brief Field multiplication: a * b * R^{-1} mod q
 */
static inline int16_t dap_ntt16_fqmul(int16_t a_a, int16_t a_b, const dap_ntt_params16_t *a_params)
{
    return dap_ntt16_montgomery_reduce((int32_t)a_a * a_b, a_params);
}

/**
 * @brief Basemul: polynomial multiplication in Zq[X]/(X^2-zeta), for NTT-domain elements
 */
void dap_ntt16_basemul(int16_t a_r[2], const int16_t a_a[2], const int16_t a_b[2],
                       int16_t a_zeta, const dap_ntt_params16_t *a_params);

#ifdef __cplusplus
}
#endif
