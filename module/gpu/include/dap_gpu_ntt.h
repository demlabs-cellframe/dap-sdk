/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_gpu_ntt.h
 * @brief GPU-accelerated batch NTT (Number Theoretic Transform)
 *
 * Computes batch_count independent Montgomery NTT transforms in parallel
 * on the GPU. Each polynomial has N coefficients (int32_t) and uses the
 * same set of twiddle factors (zetas) and modulus q.
 *
 * Use case: batch ML-DSA signature verification, batch ML-KEM operations,
 * where hundreds+ of NTTs must be computed.
 *
 * Falls back to CPU dap_ntt_forward_mont() / dap_ntt_inverse_mont() when
 * GPU is unavailable or batch_count is below threshold.
 */

#pragma once

#include "dap_gpu.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for a GPU NTT plan (pre-compiled pipeline + twiddle buffer)
 *
 * Create once per (N, q, zetas) parameter set, reuse for many batch dispatches.
 */
typedef struct dap_gpu_ntt_plan dap_gpu_ntt_plan_t;

/**
 * @brief Create a GPU NTT plan for a specific parameter set
 *
 * Uploads twiddle factors to GPU, creates compute pipeline.
 * Thread-safe after creation — the plan is immutable.
 *
 * @param a_n           Polynomial degree (must be power of 2, typically 256 or 512)
 * @param a_q           Modulus
 * @param a_qinv        Montgomery constant: -q^{-1} mod 2^32
 * @param a_zetas       Forward twiddle factors (N entries, Montgomery domain)
 * @param a_zetas_inv   Inverse twiddle factors (N entries, Montgomery domain)
 * @param a_zetas_len   Length of zetas/zetas_inv arrays
 * @param a_out_plan    [out] Created plan
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_ntt_plan_create(uint32_t a_n, int32_t a_q, uint32_t a_qinv,
                                        const int32_t *a_zetas,
                                        const int32_t *a_zetas_inv,
                                        uint32_t a_zetas_len,
                                        dap_gpu_ntt_plan_t **a_out_plan);

/**
 * @brief Destroy a GPU NTT plan
 */
void dap_gpu_ntt_plan_destroy(dap_gpu_ntt_plan_t *a_plan);

/**
 * @brief Execute batch forward Montgomery NTT on GPU
 *
 * Transforms batch_count polynomials in-place. Each polynomial is
 * a_n int32_t coefficients stored contiguously:
 *   a_coeffs[0..a_n-1] = polynomial 0
 *   a_coeffs[a_n..2*a_n-1] = polynomial 1
 *   ...
 *
 * If GPU is unavailable or batch_count < threshold, falls back to
 * CPU dap_ntt_forward_mont() called in a loop.
 *
 * @param a_plan        Plan created with dap_gpu_ntt_plan_create()
 * @param a_coeffs      [in/out] Array of batch_count * N int32_t coefficients
 * @param a_batch_count Number of independent NTTs to compute
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_ntt_forward_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count);

/**
 * @brief Execute batch inverse Montgomery NTT on GPU
 *
 * Same layout as forward. Does NOT apply the final n^{-1} scaling —
 * the caller must do that (matching the CPU dap_ntt_inverse_mont behavior).
 */
dap_gpu_error_t dap_gpu_ntt_inverse_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count);

/**
 * @brief Create a GPU NTT plan for plain (non-Montgomery) modular NTT.
 *
 * For Chipmunk and other schemes that use (a * b) % q butterfly.
 * The inverse NTT includes the 1/n scaling on the GPU.
 *
 * @param a_n           Polynomial degree (power of 2)
 * @param a_q           Modulus
 * @param a_one_over_n  Inverse of n mod q (for inverse NTT scaling)
 * @param a_zetas       Forward twiddle factors (N entries)
 * @param a_zetas_inv   Inverse twiddle factors (N entries)
 * @param a_zetas_len   Length of zetas arrays
 * @param a_out_plan    [out] Created plan
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_ntt_plan_create_plain(uint32_t a_n, int32_t a_q,
                                              int32_t a_one_over_n,
                                              const int32_t *a_zetas,
                                              const int32_t *a_zetas_inv,
                                              uint32_t a_zetas_len,
                                              dap_gpu_ntt_plan_t **a_out_plan);

/**
 * @brief Execute batch forward plain NTT on GPU
 */
dap_gpu_error_t dap_gpu_ntt_forward(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count);

/**
 * @brief Execute batch inverse plain NTT on GPU (includes 1/n scaling)
 */
dap_gpu_error_t dap_gpu_ntt_inverse(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count);

/* ===== 16-bit NTT API for ML-KEM (Kyber) ===== */

typedef struct dap_gpu_ntt16_plan dap_gpu_ntt16_plan_t;

/**
 * @brief Create a 16-bit GPU NTT plan for ML-KEM.
 *
 * Host-side int16 coefficients are widened to int32 for GPU dispatch,
 * then narrowed back after download.
 *
 * @param a_n           Polynomial degree (256 for Kyber)
 * @param a_q           Modulus (3329 for Kyber)
 * @param a_qinv16      Montgomery constant: q^{-1} mod 2^16
 * @param a_zetas       Forward zetas (128 entries for Kyber)
 * @param a_zetas_inv   Inverse zetas (128 entries for Kyber)
 * @param a_zetas_len   Length of zetas arrays
 * @param a_out_plan    [out] Created plan
 */
dap_gpu_error_t dap_gpu_ntt16_plan_create(uint32_t a_n, int16_t a_q, int16_t a_qinv16,
                                          const int16_t *a_zetas,
                                          const int16_t *a_zetas_inv,
                                          uint32_t a_zetas_len,
                                          dap_gpu_ntt16_plan_t **a_out_plan);

void dap_gpu_ntt16_plan_destroy(dap_gpu_ntt16_plan_t *a_plan);

/**
 * @brief Execute batch 16-bit forward Montgomery NTT on GPU.
 *
 * @param a_coeffs  Array of batch_count * N int16_t coefficients (modified in-place)
 */
dap_gpu_error_t dap_gpu_ntt16_forward_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count);

/**
 * @brief Execute batch 16-bit inverse Montgomery NTT on GPU (includes scaling).
 */
dap_gpu_error_t dap_gpu_ntt16_inverse_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count);

#ifdef __cplusplus
}
#endif
