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
 * @brief Forward NTT (Cooley-Tukey, in-place).
 *
 * Input:  a[i] in [0, q), standard coefficient order.
 * Output: a[brv11(i)] = f(omega^i)  ( evaluations in bit-reversed order).
 *
 * The output is in bit-reversed order because the Cooley-Tukey algorithm
 * naturally produces this layout.  Callers who need canonical order
 * should bit-reverse the output, or use chipmunk_fri_ntt_domain() to
 * index evaluations directly.
 *
 * @param a Array of CHIPMUNK_FRI_NTT_SIZE elements.
 */
void chipmunk_fri_ntt_forward(int32_t a[CHIPMUNK_FRI_NTT_SIZE]);

/**
 * @brief Inverse NTT (Gentleman-Sande, in-place).
 *
 * Input:  a[brv11(i)] = evaluations in bit-reversed order.
 * Output: a[i] = coefficients in standard order.
 *
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
