/*
 * chipmunk_field.h — Scalar field arithmetic for F_q (q = CHIPMUNK_Q = 3168257).
 *
 * Provides modular inverse, modular exponentiation, and primitive root-of-unity
 * discovery.  These are the foundational building blocks for the FRI-DEEP
 * polynomial commitment scheme (Phase 9), which requires:
 *   - omega_2048: primitive 2^11-th root of unity (FRI evaluation domain)
 *   - N^{-1} mod q: for inverse NTT normalisation
 *   - a^{q-2} mod q: Fermat-based inverse (avoiding extended Euclid branches)
 *
 * Field properties:
 *   q = 3168257 (prime)
 *   q - 1 = 2^11 × 1547 = 2^11 × 7 × 13 × 17
 *   2-adicity = 11  →  max power-of-two root: omega_{2048}
 *   log2(q) ≈ 21.6 bits
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_FIELD_H_
#define _CHIPMUNK_FIELD_H_

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Modular inverse in F_q
 *
 * Returns a^{-1} mod CHIPMUNK_Q in [1, q), or 0 if a ≡ 0 (not invertible).
 *
 * Uses Fermat's little theorem: a^{-1} = a^{q-2} mod q.
 * Since q is prime and a ∈ [1, q-1], this always succeeds.
 *
 * For a = 0, returns 0 (caller should check before dividing).
 * ---------------------------------------------------------------------- */

/**
 * @brief Modular inverse via Fermat's little theorem.
 *
 * @param a  Element of F_q in [0, q).  If a == 0, returns 0.
 * @return   a^{-1} mod q in [1, q), or 0 if a == 0.
 */
int32_t chipmunk_field_inv(int32_t a);

/* -------------------------------------------------------------------------
 * Modular exponentiation in F_q
 *
 * Computes a^e mod q using square-and-multiply (binary method).
 * Handles negative bases by canonicalising to [0, q) first.
 * ---------------------------------------------------------------------- */

/**
 * @brief Modular exponentiation: base^exp mod CHIPMUNK_Q.
 *
 * @param base  Base element (any int32_t, canonicalised internally).
 * @param exp   Exponent (unsigned, any value).
 * @return      base^exp mod q in [0, q).
 */
int32_t chipmunk_field_pow(int32_t base, uint32_t exp);

/* -------------------------------------------------------------------------
 * Primitive root of unity of order 2^k
 *
 * Finds omega ∈ F_q* such that:
 *   - omega^{2^k} ≡ 1 (mod q)
 *   - omega^{2^{k-1}} ≡ q-1 ≡ -1 (mod q)  [order is exactly 2^k]
 *
 * This exists iff k ≤ 2-adicity(q-1) = 11 for CHIPMUNK_Q.
 *
 * Algorithm: probe generators g = 2, 3, 4, ... and compute
 *   omega = g^{(q-1)/2^k} mod q, then verify omega^N == q-1 where N = 2^{k-1}.
 * Since (q-1)/2^k is odd for k = 11 (specifically 1547 = 7×13×17), the first
 * few generators almost always work.
 * ---------------------------------------------------------------------- */

/**
 * @brief Find a primitive 2^k-th root of unity in F_q.
 *
 * @param k        Exponent: find omega of order 2^k.  Must be ≤ 11 for CHIPMUNK_Q.
 * @param out_omega  Output: omega in [1, q).
 * @return          0 on success, -1 if no such root exists (k > 2-adicity).
 */
int chipmunk_field_primitive_root_2k(uint32_t k, int32_t *out_omega);

/* -------------------------------------------------------------------------
 * Precomputed constants for the FRI domain
 *
 * The FRI evaluation domain has size 2048 = 2^11, requiring:
 *   - omega_2048: primitive 2048-th root of unity
 *   - omega_2048_inv: omega^{-1} mod q
 *   - omega_512: primitive 512-th root of unity (= omega_2048^4)
 *   - omega_512_inv: omega^{-1}_512 mod q
 *   - inv_2048: 2048^{-1} mod q (for inverse NTT)
 *   - inv_512: 512^{-1} mod q (for inverse NTT)
 *
 * These are lazily computed on first access (thread-safe).
 * ---------------------------------------------------------------------- */

/** Maximum 2-adicity of CHIPMUNK_Q (v_2(q-1) = 11). */
#define CHIPMUNK_FIELD_TWO_ADICITY  11u

/** FRI evaluation domain size (blowup 4 × N = 2048). */
#define CHIPMUNK_FIELD_FRI_DOMAIN   (1u << CHIPMUNK_FIELD_TWO_ADICITY)  /* 2048 */

/**
 * @brief Initialise the field constants module.
 *
 * Computes and caches omega_2048, omega_512, their inverses, and N^{-1}.
 * Thread-safe: concurrent calls are fine; work is done exactly once.
 *
 * @return 0 on success, negative on error (no primitive root found).
 */
int chipmunk_field_init(void);

/**
 * @brief Check if field constants are initialised.
 */
bool chipmunk_field_is_initialized(void);

/**
 * @brief Get primitive 2048-th root of unity omega_2048.
 *
 * Requires chipmunk_field_init() to have been called.
 */
int32_t chipmunk_field_omega_2048(void);

/**
 * @brief Get inverse of omega_2048.
 */
int32_t chipmunk_field_omega_2048_inv(void);

/**
 * @brief Get primitive 512-th root of unity omega_512 = omega_2048^4.
 */
int32_t chipmunk_field_omega_512(void);

/**
 * @brief Get inverse of omega_512.
 */
int32_t chipmunk_field_omega_512_inv(void);

/**
 * @brief Get 2048^{-1} mod q.
 */
int32_t chipmunk_field_inv_2048(void);

/**
 * @brief Get 512^{-1} mod q.
 */
int32_t chipmunk_field_inv_512(void);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FIELD_H_ */
