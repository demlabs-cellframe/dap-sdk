/*
 * LoTRS — Practical Post-Quantum Structured Threshold Ring Signatures.
 *
 * Parameter sets.  TEST (d=32) for validation; BENCH (d=128) for production.
 *
 * Paper: IACR ePrint 2026/974 (Jagganath, Steinfeld, Esgin, Sakzad,
 *        Liu, Saarinen).
 * Reference: lotrs-sig/lotrs/lotrs-rs (Rust), lotrs-sig/lotrs/lotrs-py (Python).
 */

#pragma once
#ifndef _LOTRS_PARAMS_H_
#define _LOTRS_PARAMS_H_

#include <stdint.h>

typedef struct lotrs_params {
    uint32_t d;             /* ring dimension: R_q = Z_q[X]/(X^d + 1) */
    uint64_t q;             /* primary modulus */
    uint64_t q_hat;         /* secondary modulus (RS proof) */
    uint32_t beta;          /* ring size N = beta^kappa */
    uint32_t kappa;         /* ring nesting depth (1 for flat ring) */
    uint32_t T;             /* threshold */
    uint32_t k;             /* public-key dimension */
    uint32_t l;             /* secret-key short part */
    uint32_t l_prime;       /* secret-key auxiliary part */
    uint32_t n_hat;         /* RS proof: number of challenge polys */
    uint32_t k_hat;         /* RS proof: number of response polys */
    uint32_t w;             /* ternary challenge weight */
    uint32_t eta;           /* short-vector bound */
    double   phi;           /* rejection sampling parameter */
    double   phi_a;         /* RS mask sampling parameter */
    double   phi_b;         /* RS commitment sampling parameter */
    uint32_t K_A;           /* RS A-bin decomposition bits */
    uint32_t K_B;           /* RS B-bin decomposition bits */
    uint32_t K_w;           /* DualMS w-decomposition bits */
    uint32_t x_seed_len;    /* FS challenge seed length (bytes) */
} lotrs_params_t;

/* TEST parameter set: d=32, q~2^22, N=4, T=2. */
extern const lotrs_params_t LOTRS_PARAMS_TEST;

/* BENCH_4OF32: d=128, q~48-bit, N=32, T=4. */
extern const lotrs_params_t LOTRS_PARAMS_BENCH_4OF32;

/* BENCH: d=128, q~48-bit, N=32, T=16. */
extern const lotrs_params_t LOTRS_PARAMS_BENCH;

/* Derived constants (computed at compile time for TEST). */
#define LOTRS_TEST_D       32u
#define LOTRS_TEST_Q       4194389ULL
#define LOTRS_TEST_Q_HAT   7000061ULL

#endif /* _LOTRS_PARAMS_H_ */
