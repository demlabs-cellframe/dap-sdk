/*
 * chipmunk_ntt.h — 512-point NTT for chipmunk lattice cryptography.
 *
 * Uses dap_ntt Montgomery kernels with R = 2^32 and SIMD dispatch.
 * All twiddle tables are computed at runtime — no hardcoded constants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk.h"
#include "dap_ntt.h"

/* ===== Per-q NTT context ===== */

/**
 * @brief Per-q NTT context.
 *
 * Wraps dap_ntt_params_t with ownership tracking for heap-allocated twiddle
 * tables. Built by chipmunk_ntt_params_compute(), freed by chipmunk_ntt_ctx_free().
 * Montgomery R = 2^32 is used to match dap_ntt32 SIMD kernel guard.
 */
typedef struct chipmunk_ntt_ctx {
    dap_ntt_params_t params;
    uint64_t         q;
    bool             owns_tables;
    /* Cached negacyclic NTT roots — computed once in params_compute,
     * used by chipmunk_ntt_q / chipmunk_invntt_q. */
    int32_t          psi;       /* primitive 2N-th root (psi^N = -1) */
    int32_t          psi_inv;   /* psi^{-1} mod q */
    int32_t          omega;     /* psi^2 = primitive N-th root */
    int32_t          omega_inv; /* omega^{-1} mod q */
} chipmunk_ntt_ctx_t;

/**
 * @brief Compute NTT parameters for an arbitrary prime q.
 *
 * Fills a_ctx with Montgomery constants (R = 2^32) and builds twiddle
 * tables in Montgomery form: omega^{brv9(i)} * R mod q.
 * The context must be freed with chipmunk_ntt_ctx_free().
 *
 * @param a_ctx Output context.
 * @param q Prime modulus (must be odd, < 2^31).
 * @return 0 on success, negative on error.
 */
int chipmunk_ntt_params_compute(chipmunk_ntt_ctx_t *a_ctx, uint64_t q);

/** @brief Free heap resources in a per-q NTT context. */
void chipmunk_ntt_ctx_free(chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q forward NTT (SIMD-dispatched Montgomery kernel). */
void chipmunk_ntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q inverse NTT + post-pass (cancel R, apply 1/N, center). */
void chipmunk_invntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q pointwise Montgomery multiply (SIMD-dispatched). */
int chipmunk_ntt_pointwise_montgomery_q(int32_t a_c[CHIPMUNK_N],
                                          const int32_t a_a[CHIPMUNK_N],
                                          const int32_t a_b[CHIPMUNK_N],
                                          const chipmunk_ntt_ctx_t *a_ctx);

/** @brief Per-q scalar Montgomery multiply with explicit constants. */
int32_t chipmunk_ntt_montgomery_multiply_q(int32_t a_a, int32_t a_b, uint64_t q,
                                             uint32_t qinv_neg, uint32_t mont_r_bits,
                                             uint32_t mont_r_mask);

/* ===== Domain conversion helpers ===== */

/**
 * @brief Convert a standard-form coefficient to Montgomery domain.
 * @return a_c * R mod q (Montgomery form).
 */
int32_t chipmunk_ntt_to_mont(int32_t a_c, const chipmunk_ntt_ctx_t *a_ctx);

/**
 * @brief Convert a Montgomery-domain coefficient to standard form.
 * @return a_c * R^{-1} mod q (standard form in [0,q)).
 */
int32_t chipmunk_ntt_from_mont(int32_t a_c, const chipmunk_ntt_ctx_t *a_ctx);

/* ===== Global CHIPMUNK_Q wrappers ===== */

/** @brief Get the global NTT params (lazy-built for CHIPMUNK_Q). */
const dap_ntt_params_t *chipmunk_ntt_global_params(void);

/** @brief Get the full global NTT ctx (for ψ-twist in batch verify). */
const chipmunk_ntt_ctx_t *chipmunk_ntt_global_ctx(void);

/** @brief Forward NTT using global CHIPMUNK_Q context. */
void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]);

/** @brief Inverse NTT using global CHIPMUNK_Q context. */
void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]);

/** @brief Pointwise Montgomery multiply using global CHIPMUNK_Q context. */
int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N],
                                     const int32_t a_b[CHIPMUNK_N]);
