/**
 * @file dap_ntt32_mont.h
 * @brief Generated Montgomery-domain NTT kernels (32-bit coefficients).
 *
 * AUTO-GENERATED from dap_ntt_mont.h.tpl — do not edit manually.
 * Parameters: BITS=32, COEFF_T=int32_t, PREFIX=dap_ntt32
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_ntt.h"


#define dap_ntt32_FQMUL(z, x, p)  dap_ntt_montgomery_reduce((int64_t)(z) * (x), (p))

#define dap_ntt32_FQMUL(z, x, p)  dap_ntt16_fqmul((z), (x), (p))


/**
 * @brief Montgomery-domain forward NTT (Cooley-Tukey, sequential zeta walk).
 *
 * Processes layers from len=n/2 down to len=1.
 * For partial SIMD fallback, use dap_ntt32_forward_mont_partial().
 */
static inline void dap_ntt32_forward_mont_kernel(int32_t *a_coeffs,
                                                   const dap_ntt_params_t *a_params)
{
    const int32_t *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;

    for (l_len = a_params->n / 2; l_len >= 1; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = dap_ntt32_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);

                a_coeffs[l_j + l_len] = a_coeffs[l_j] + 2 * a_params->q - l_t;
                a_coeffs[l_j] = a_coeffs[l_j] + l_t;

                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;

            }
        }
    }
}

/**
 * @brief Partial forward NTT: process layers from len_from down to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void dap_ntt32_forward_mont_partial(int32_t *a_coeffs,
                                                    const dap_ntt_params_t *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const int32_t *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = a_k;

    for (l_len = a_len_from; l_len >= a_len_to; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = dap_ntt32_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);

                a_coeffs[l_j + l_len] = a_coeffs[l_j] + 2 * a_params->q - l_t;
                a_coeffs[l_j] = a_coeffs[l_j] + l_t;

                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;

            }
        }
    }
}

/**
 * @brief Montgomery-domain inverse NTT (Gentleman-Sande, sequential zeta walk).
 *

 * Final scaling is NOT applied — caller must handle the combined
 * Montgomery + 1/n factor as appropriate for the algorithm.

 * Final scaling by zetas_inv[zetas_len-1] (combined 1/n * R factor) IS applied.

 */
static inline void dap_ntt32_inverse_mont_kernel(int32_t *a_coeffs,
                                                   const dap_ntt_params_t *a_params)
{
    const int32_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;


    const uint32_t l_nq = (uint32_t)a_params->n * (uint32_t)a_params->q;

    for (l_len = 1; l_len < a_params->n; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_t = (uint32_t)a_coeffs[l_j];
                uint32_t l_u = (uint32_t)a_coeffs[l_j + l_len];
                a_coeffs[l_j] = (int32_t)(l_t + l_u);
                uint32_t l_diff = l_t + l_nq - l_u;
                a_coeffs[l_j + l_len] = dap_ntt_montgomery_reduce(
                        (int64_t)l_zeta * (int64_t)(int32_t)l_diff, a_params);
            }
        }
    }

    for (l_len = 2; l_len <= a_params->n / 2; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = dap_ntt16_barrett_reduce(
                                            l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = l_t - a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len] = dap_ntt32_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
            }
        }
    }

    int32_t l_scale = l_zinv[a_params->zetas_len - 1];
    for (unsigned int i = 0; i < a_params->n; i++)
        a_coeffs[i] = dap_ntt32_FQMUL(a_coeffs[i], l_scale, a_params);

}

/**
 * @brief Partial inverse NTT: process layers from len_from up to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void dap_ntt32_inverse_mont_partial(int32_t *a_coeffs,
                                                    const dap_ntt_params_t *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const int32_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = a_k;


    const uint32_t l_nq = (uint32_t)a_params->n * (uint32_t)a_params->q;

    for (l_len = a_len_from; l_len <= a_len_to; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_t = (uint32_t)a_coeffs[l_j];
                uint32_t l_u = (uint32_t)a_coeffs[l_j + l_len];
                a_coeffs[l_j] = (int32_t)(l_t + l_u);
                uint32_t l_diff = l_t + l_nq - l_u;
                a_coeffs[l_j + l_len] = dap_ntt_montgomery_reduce(
                        (int64_t)l_zeta * (int64_t)(int32_t)l_diff, a_params);
            }
        }
    }

    for (l_len = a_len_from; l_len <= a_len_to; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = dap_ntt16_barrett_reduce(
                                            l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = l_t - a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len] = dap_ntt32_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
            }
        }
    }

}

/**
 * @brief Pointwise Montgomery multiplication: c[i] = a[i] * b[i] * R^{-1} mod q
 */
static inline void dap_ntt32_pointwise_mont_kernel(int32_t *a_c,
                                                     const int32_t *a_a,
                                                     const int32_t *a_b,
                                                     const dap_ntt_params_t *a_params)
{
    for (unsigned int i = 0; i < a_params->n; i++)
        a_c[i] = dap_ntt32_FQMUL(a_a[i], a_b[i], a_params);
}

#undef dap_ntt32_FQMUL
