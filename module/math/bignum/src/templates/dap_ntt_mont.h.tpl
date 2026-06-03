/**
 * @file {{PREFIX}}_mont.h
 * @brief Generated Montgomery-domain NTT kernels ({{BITS}}-bit coefficients).
 *
 * AUTO-GENERATED from dap_ntt_mont.h.tpl — do not edit manually.
 * Parameters: BITS={{BITS}}, COEFF_T={{COEFF_T}}, PREFIX={{PREFIX}}
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_ntt.h"

{{#if BITS == '32'}}
#define {{PREFIX}}_FQMUL(z, x, p)  dap_ntt_montgomery_reduce((int64_t)(z) * (x), (p))
{{#else}}
#define {{PREFIX}}_FQMUL(z, x, p)  dap_ntt16_fqmul((z), (x), (p))
{{/if}}

/**
 * @brief Montgomery-domain forward NTT (Cooley-Tukey, sequential zeta walk).
 *
 * Processes layers from len=n/2 down to len={{MIN_LEN}}.
 * For partial SIMD fallback, use {{PREFIX}}_forward_mont_partial().
 */
static inline void {{PREFIX}}_forward_mont_kernel({{COEFF_T}} *a_coeffs,
                                                   const {{PARAMS_T}} *a_params)
{
    const {{COEFF_T}} *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;

    for (l_len = a_params->n / 2; l_len >= {{MIN_LEN}}; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                {{COEFF_T}} l_t = {{PREFIX}}_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
{{#if BITS == '32'}}
                a_coeffs[l_j + l_len] = a_coeffs[l_j] + 2 * a_params->q - l_t;
                a_coeffs[l_j] = a_coeffs[l_j] + l_t;
{{#else}}
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
{{/if}}
            }
        }
    }
}

/**
 * @brief Partial forward NTT: process layers from len_from down to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void {{PREFIX}}_forward_mont_partial({{COEFF_T}} *a_coeffs,
                                                    const {{PARAMS_T}} *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const {{COEFF_T}} *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = a_k;

    for (l_len = a_len_from; l_len >= a_len_to; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                {{COEFF_T}} l_t = {{PREFIX}}_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
{{#if BITS == '32'}}
                a_coeffs[l_j + l_len] = a_coeffs[l_j] + 2 * a_params->q - l_t;
                a_coeffs[l_j] = a_coeffs[l_j] + l_t;
{{#else}}
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
{{/if}}
            }
        }
    }
}

/**
 * @brief Montgomery-domain inverse NTT (Gentleman-Sande, sequential zeta walk).
 *
{{#if BITS == '32'}}
 * Final scaling is NOT applied — caller must handle the combined
 * Montgomery + 1/n factor as appropriate for the algorithm.
{{#else}}
 * Final scaling by zetas_inv[zetas_len-1] (combined 1/n * R factor) IS applied.
{{/if}}
 */
static inline void {{PREFIX}}_inverse_mont_kernel({{COEFF_T}} *a_coeffs,
                                                   const {{PARAMS_T}} *a_params)
{
    const {{COEFF_T}} *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;

{{#if BITS == '32'}}
    const uint32_t l_nq = (uint32_t)a_params->n * (uint32_t)a_params->q;

    for (l_len = 1; l_len < a_params->n; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_t = (uint32_t)a_coeffs[l_j];
                uint32_t l_u = (uint32_t)a_coeffs[l_j + l_len];
                a_coeffs[l_j] = ({{COEFF_T}})(l_t + l_u);
                uint32_t l_diff = l_t + l_nq - l_u;
                a_coeffs[l_j + l_len] = dap_ntt_montgomery_reduce(
                        (int64_t)l_zeta * (int64_t)({{COEFF_T}})l_diff, a_params);
            }
        }
    }
{{#else}}
    for (l_len = 2; l_len <= a_params->n / 2; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                {{COEFF_T}} l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = dap_ntt16_barrett_reduce(
                                            l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = l_t - a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len] = {{PREFIX}}_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
            }
        }
    }

    {{COEFF_T}} l_scale = l_zinv[a_params->zetas_len - 1];
    for (unsigned int i = 0; i < a_params->n; i++)
        a_coeffs[i] = {{PREFIX}}_FQMUL(a_coeffs[i], l_scale, a_params);
{{/if}}
}

/**
 * @brief Partial inverse NTT: process layers from len_from up to len_to (inclusive).
 * Zeta index must be pre-computed by caller.
 */
static inline void {{PREFIX}}_inverse_mont_partial({{COEFF_T}} *a_coeffs,
                                                    const {{PARAMS_T}} *a_params,
                                                    unsigned int a_len_from,
                                                    unsigned int a_len_to,
                                                    unsigned int a_k)
{
    const {{COEFF_T}} *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = a_k;

{{#if BITS == '32'}}
    const uint32_t l_nq = (uint32_t)a_params->n * (uint32_t)a_params->q;

    for (l_len = a_len_from; l_len <= a_len_to; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                uint32_t l_t = (uint32_t)a_coeffs[l_j];
                uint32_t l_u = (uint32_t)a_coeffs[l_j + l_len];
                a_coeffs[l_j] = ({{COEFF_T}})(l_t + l_u);
                uint32_t l_diff = l_t + l_nq - l_u;
                a_coeffs[l_j + l_len] = dap_ntt_montgomery_reduce(
                        (int64_t)l_zeta * (int64_t)({{COEFF_T}})l_diff, a_params);
            }
        }
    }
{{#else}}
    for (l_len = a_len_from; l_len <= a_len_to; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            {{COEFF_T}} l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                {{COEFF_T}} l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = dap_ntt16_barrett_reduce(
                                            l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = l_t - a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len] = {{PREFIX}}_FQMUL(l_zeta, a_coeffs[l_j + l_len], a_params);
            }
        }
    }
{{/if}}
}

/**
 * @brief Pointwise Montgomery multiplication: c[i] = a[i] * b[i] * R^{-1} mod q
 */
static inline void {{PREFIX}}_pointwise_mont_kernel({{COEFF_T}} *a_c,
                                                     const {{COEFF_T}} *a_a,
                                                     const {{COEFF_T}} *a_b,
                                                     const {{PARAMS_T}} *a_params)
{
    for (unsigned int i = 0; i < a_params->n; i++)
        a_c[i] = {{PREFIX}}_FQMUL(a_a[i], a_b[i], a_params);
}

#undef {{PREFIX}}_FQMUL
