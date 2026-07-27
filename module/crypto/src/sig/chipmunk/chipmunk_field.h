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

/* -------------------------------------------------------------------------
 * Per-q field constants (Phase 9.13)
 *
 * Functions that operate on an arbitrary prime modulus q, not the global
 * CHIPMUNK_Q. The output struct is caller-owned; no global state is used.
 * ---------------------------------------------------------------------- */

/** Field constants for an arbitrary prime q. */
typedef struct chipmunk_field_consts {
    uint64_t q;             /* the prime modulus */
    uint32_t two_adicity;   /* v_2(q-1) */
    int32_t  omega;         /* primitive 2^two_adicity-th root of unity */
    int32_t  omega_inv;     /* omega^{-1} mod q */
    int32_t  inv_domain;    /* 2^two_adicity^{-1} mod q (for inverse NTT) */
} chipmunk_field_consts_t;

/**
 * @brief Compute FRI-domain roots of unity and inverses for an arbitrary q.
 *
 * @param a_out       Output struct (caller-owned).
 * @param q           Prime modulus.
 * @param two_adicity v_2(q-1): 2^two_adicity divides q-1 (max root order).
 * @return 0 on success, negative on error.
 */
int chipmunk_field_compute_for_q(chipmunk_field_consts_t *a_out,
                                   uint64_t q, uint32_t two_adicity);

/** @brief Per-q modular inverse via Fermat: a^{-1} mod q. */
int32_t chipmunk_field_inv_q(int32_t a, uint64_t q);

/** @brief Per-q modular exponentiation: base^exp mod q. */
int32_t chipmunk_field_pow_q(int32_t base, uint32_t exp, uint64_t q);

/** @brief Per-q primitive root of order 2^k in F_q. */
int chipmunk_field_primitive_root_2k_q(uint32_t k, int32_t *out_omega, uint64_t q);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FIELD_H_ */
