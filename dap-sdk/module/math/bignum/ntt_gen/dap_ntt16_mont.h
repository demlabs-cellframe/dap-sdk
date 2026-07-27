/**
 * @file dap_ntt16_mont.h
 * @brief Generated Montgomery-domain NTT kernels (16-bit coefficients).
 *
 * AUTO-GENERATED from dap_ntt_mont.h.tpl — do not edit manually.
 * Parameters: BITS=16, COEFF_T=int16_t, PREFIX=dap_ntt16
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_ntt.h"



/**
 * @brief Montgomery-domain forward NTT (Cooley-Tukey, sequential zeta walk).
 *
 * Processes layers from len=n/2 down to len=2.
 * For partial SIMD fallback, use dap_ntt16_forward_mont_partial().
 */
static inline void dap_ntt16_forward_mont_kernel(int16_t *a_coeffs,
                                                   const dap_ntt_params16_t *a_params)
{
    const int16_t *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;

    for (l_len = a_params->n / 2; l_len >= 2; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int16_t l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = dap_ntt16_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);

            }
        }
    }
}

/**
 * @brief Partial forward NTT: process layers from len_from down to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void dap_ntt16_forward_mont_partial(int16_t *a_coeffs,
                                                    const dap_ntt_params16_t *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const int16_t *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = a_k;

    for (l_len = a_len_from; l_len >= a_len_to; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int16_t l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = dap_ntt16_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);

            }
        }
    }
}

/**
 * @brief Montgomery-domain inverse NTT (Gentleman-Sande, sequential zeta walk).
 *

 */
static inline void dap_ntt16_inverse_mont_kernel(int16_t *a_coeffs,
                                                   const dap_ntt_params16_t *a_params)
{
    const int16_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;


}

/**
 * @brief Partial inverse NTT: process layers from len_from up to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void dap_ntt16_inverse_mont_partial(int16_t *a_coeffs,
                                                    const dap_ntt_params16_t *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const int16_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = a_k;


}

/**
 * @brief Pointwise Montgomery multiplication: c[i] = a[i] * b[i] * R^{-1} mod q
 */
static inline void dap_ntt16_pointwise_mont_kernel(int16_t *a_c,
                                                     const int16_t *a_a,
                                                     const int16_t *a_b,
                                                     const dap_ntt_params16_t *a_params)
{
    for (unsigned int i = 0; i < a_params->n; i++)
        a_c[i] = dap_ntt16_FQMUL(a_a[i], a_b[i], a_params);
}

#undef dap_ntt16_FQMUL
