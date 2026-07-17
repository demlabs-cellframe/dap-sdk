/*
 * chipmunk_fri_ntt.h — 2048-point Number Theoretic Transform for FRI-DEEP.
 *
 * Standard (cyclic) NTT over F_q, q = CHIPMUNK_Q = 3168257.
 * Uses primitive 2048-th root of unity omega_2048 from chipmunk_field.
 *
 * Domain: {omega^0, omega^1, ..., omega^{2047}}  (subgroup of F_q*)
 * Coset:  {g * omega^0, g * omega^1, ..., g * omega^{2047}}
 *
 * Non-Montgomery: all values in [0, q).  Direct modular arithmetic
 * matches the FRI folding/evaluation code path — no domain conversions.
 *
 * FRI usage:
 *   - RS encoding: degree-511 poly → 2048 evaluations (coset NTT)
 *   - Interpolation: 2048 evaluations → degree-511 poly (inverse NTT)
 *   - Composition polynomial evaluation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_FRI_NTT_H_
#define _CHIPMUNK_FRI_NTT_H_

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk_field.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NTT size: 2048 = 2^11 (FRI evaluation domain, blowup factor 4 × N=512) */
#define CHIPMUNK_FRI_NTT_LOG  11u
#define CHIPMUNK_FRI_NTT_SIZE (1u << CHIPMUNK_FRI_NTT_LOG)  /* 2048 */

/**
 * @brief Initialise FRI NTT twiddle tables.
 *
 * Generates forward and inverse twiddle tables using omega_2048 from
 * chipmunk_field.  Thread-safe: concurrent calls are fine; work done once.
 *
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_ntt_init(void);

/**
 * @brief Check if FRI NTT is initialised.
 */
bool chipmunk_fri_ntt_is_initialized(void);

/**
 * @brief Forward NTT (Gentleman-Sande DIF, in-place).
 *
 * Input:  a[i] in [0, q), standard coefficient order.
 * Output: a[k] = f(omega^k) for k = 0..N-1 (natural order).
 *
 * Algorithm: bit-reverse input, then DIF stages.  The pre-BRV + DIF
 * combination produces natural-order output (evaluations indexed by k).
 *
 * @param a Array of CHIPMUNK_FRI_NTT_SIZE elements.
 */
void chipmunk_fri_ntt_forward(int32_t a[CHIPMUNK_FRI_NTT_SIZE]);

/**
 * @brief Inverse NTT (Cooley-Tukey DIT, in-place).
 *
 * Input:  a[k] = evaluations in natural order (a[k] = f(omega^k)).
 * Output: a[i] = coefficients in standard order.
 *
 * Algorithm: DIT stages in reverse order, then bit-reverse output.
 * Applies 1/N scaling (N^{-1} mod q from chipmunk_field).
 *
 * @param a Array of CHIPMUNK_FRI_NTT_SIZE elements.
 */
void chipmunk_fri_ntt_inverse(int32_t a[CHIPMUNK_FRI_NTT_SIZE]);

/**
 * @brief Coset NTT: evaluate f at shifted domain {g * omega^k}.
 *
 * Equivalent to: multiply coefficient a[i] by g^i, then forward NTT.
 * Result: a[brv11(i)] = f(g * omega^i) for i = 0..N-1.
 *
 * The coset shift avoids degeneracy when the polynomial evaluates to 0
 * at domain points (important for DEEP-ALI soundness).
 *
 * @param a        Array of CHIPMUNK_FRI_NTT_SIZE elements (modified in-place).
 * @param coset_g  Coset generator g in F_q* (must be nonzero).
 */
void chipmunk_fri_ntt_coset_forward(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                    int32_t coset_g);

/* -------------------------------------------------------------------------
 * Per-q NTT context (Phase 9.13h)
 *
 * Holds twiddle tables for an arbitrary prime q. Built by
 * chipmunk_fri_ntt_ctx_init, freed by chipmunk_fri_ntt_ctx_free.
 * ---------------------------------------------------------------------- */

typedef struct chipmunk_fri_ntt_ctx {
    uint64_t  q;           /* field modulus */
    int32_t   omega;       /* primitive CHIPMUNK_FRI_NTT_SIZE-th root */
    int32_t   omega_inv;   /* its inverse */
    int32_t   inv_n;       /* N^{-1} mod q (for inverse NTT scaling) */
    int32_t  *zetas;       /* omega^k for k = 0..N-1 (heap) */
    int32_t  *zetas_inv;   /* omega^{-k} for k = 0..N-1 (heap) */
} chipmunk_fri_ntt_ctx_t;

/**
 * @brief Build per-q NTT twiddle tables.
 * @param ctx         Output context (caller allocates struct, tables heap-alloc'd).
 * @param q           Prime modulus.
 * @param two_adicity Must equal CHIPMUNK_FRI_NTT_LOG (11).
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_ntt_ctx_init(chipmunk_fri_ntt_ctx_t *ctx, uint64_t q,
                                uint32_t two_adicity);

/** @brief Free heap resources in a per-q NTT context. */
void chipmunk_fri_ntt_ctx_free(chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q forward NTT using ctx->zetas. */
void chipmunk_fri_ntt_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q inverse NTT using ctx->zetas_inv and ctx->inv_n. */
void chipmunk_fri_ntt_inverse_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q coset forward NTT. */
void chipmunk_fri_ntt_coset_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                        int32_t coset_g,
                                        const chipmunk_fri_ntt_ctx_t *ctx);

/**
 * @brief Get omega_2048 used by this NTT.
 *
 * Convenience accessor; delegates to chipmunk_field_omega_2048().
 * Requires chipmunk_fri_ntt_init() to have been called.
 */
int32_t chipmunk_fri_ntt_omega(void);

/**
 * @brief Fill array with the evaluation domain {omega^0, omega^1, ..., omega^{2047}}.
 *
 * @param domain Output array of CHIPMUNK_FRI_NTT_SIZE elements.
 */
void chipmunk_fri_ntt_domain(int32_t domain[CHIPMUNK_FRI_NTT_SIZE]);

/**
 * @brief Fill array with the coset-shifted domain {g*omega^0, g*omega^1, ..., g*omega^{2047}}.
 *
 * @param domain   Output array of CHIPMUNK_FRI_NTT_SIZE elements.
 * @param coset_g  Coset generator.
 */
void chipmunk_fri_ntt_coset_domain(int32_t domain[CHIPMUNK_FRI_NTT_SIZE],
                                    int32_t coset_g);

/**
 * @brief Bit-reverse an 11-bit index.
 *
 * @param x Index in [0, 2048).
 * @return   Bit-reversed index.
 */
static inline uint32_t chipmunk_fri_ntt_brv11(uint32_t a_x)
{
    uint32_t l_r = 0;
    for (unsigned i = 0; i < CHIPMUNK_FRI_NTT_LOG; ++i) {
        l_r = (l_r << 1) | (a_x & 1u);
        a_x >>= 1;
    }
    return l_r;
}

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FRI_NTT_H_ */
