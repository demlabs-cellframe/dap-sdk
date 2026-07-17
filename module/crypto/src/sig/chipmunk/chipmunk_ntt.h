/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk.h"
#include "dap_ntt.h"

// NTT parameters for q = 3168257 (corrected from original Rust implementation)
#define CHIPMUNK_ZETAS_MONT_LEN 128

// Montgomery parameters for q = 3168257
#define CHIPMUNK_MONT_R          (1U << 22)    // Montgomery reduction parameter R = 2^22
#define CHIPMUNK_MONT_R_INV      202470        // R^(-1) mod q
#define CHIPMUNK_QINV            202470        // R^(-1) mod q

/* ===== Phase 9.14a: Per-q NTT context ===== */

/**
 * @brief Per-q NTT context (Phase 9.14a).
 *
 * Wraps dap_ntt_params_t with ownership tracking for heap-allocated twiddle
 * tables. Built by chipmunk_ntt_params_compute(), freed by chipmunk_ntt_ctx_free().
 */
typedef struct chipmunk_ntt_ctx {
    dap_ntt_params_t params;   ///< The underlying NTT parameters
    uint64_t         q;        ///< The prime modulus (copy for convenience)
    bool             owns_tables; ///< True if zetas/zetas_inv are heap-allocated
} chipmunk_ntt_ctx_t;

/**
 * @brief Compute NTT parameters for an arbitrary prime q.
 *
 * Fills a_ctx with Montgomery constants and builds twiddle tables.
 * The context must be freed with chipmunk_ntt_ctx_free().
 *
 * @param a_ctx Output context.
 * @param q Prime modulus (must be odd, < 2^22).
 * @return 0 on success, negative on error.
 */
int chipmunk_ntt_params_compute(chipmunk_ntt_ctx_t *a_ctx, uint64_t q);

/** @brief Free heap resources in a per-q NTT context. */
void chipmunk_ntt_ctx_free(chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q forward NTT. */
void chipmunk_ntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q inverse NTT. */
void chipmunk_invntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q Montgomery multiply with explicit constants. */
int32_t chipmunk_ntt_montgomery_multiply_q(int32_t a_a, int32_t a_b, uint64_t q,
                                             uint32_t qinv_neg, uint32_t mont_r_bits,
                                             uint32_t mont_r_mask);

/** @brief Per-q pointwise Montgomery multiply. */
int chipmunk_ntt_pointwise_montgomery_q(int32_t a_c[CHIPMUNK_N],
                                          const int32_t a_a[CHIPMUNK_N],
                                          const int32_t a_b[CHIPMUNK_N],
                                          const chipmunk_ntt_ctx_t *a_ctx);

/**
 * @brief Transform polynomial to NTT form (CHIPMUNK_Q wrapper).
 * Delegates to chipmunk_ntt_q() with global context.
 * @param[in,out] a_r Polynomial coefficients array
 */
void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]);

/**
 * @brief Inverse transform from NTT form (CHIPMUNK_Q wrapper).
 * Delegates to chipmunk_invntt_q() with global context.
 * @param[in,out] a_r Polynomial coefficients array
 */
void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]);

/**
 * @brief Pointwise Montgomery multiply in NTT domain (CHIPMUNK_Q wrapper).
 * Delegates to chipmunk_ntt_pointwise_montgomery_q() with global context.
 * @param[out] a_c Output polynomial coefficients
 * @param[in] a_a First polynomial coefficients
 * @param[in] a_b Second polynomial coefficients
 * @return Returns 0 on success, negative error code on failure
 */
int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N],
                                     const int32_t a_b[CHIPMUNK_N]);