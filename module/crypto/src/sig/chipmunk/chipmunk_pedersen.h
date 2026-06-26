/*
 * chipmunk_pedersen.h — Lattice-based Pedersen commitments for confidential amounts.
 *
 * Commitment: C = A * r + encode(m) mod q
 * where A is a public matrix, r is a random blinding vector, m is the message.
 *
 * Properties:
 * - Binding: hard to find (m', r') != (m, r) with same commitment (MSIS)
 * - Hiding: commitment reveals nothing about m (MLWE)
 * - Additive: Com(m1) + Com(m2) = Com(m1 + m2)
 * - Post-quantum: based on Module-SIS/Module-LWE
 *
 * Used for confidential validator stakes: commit to stake amount without
 * revealing the actual value. Range proofs prove stake > minimum.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_lrs.h"
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_lrs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Parameters
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_PEDERSEN_K         6       /* Matrix rows (security parameter) */
#define CHIPMUNK_PEDERSEN_COMMIT_BYTES  (CHIPMUNK_PEDERSEN_K * 1408)  /* q-packed commitment */

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Pedersen commitment: K polynomials in R_q */
typedef struct chipmunk_pedersen_commit {
    chipmunk_poly_t C[CHIPMUNK_PEDERSEN_K];
} chipmunk_pedersen_commit_t;

/* Pedersen public parameters: matrix A ∈ R_q^{K×L} */
typedef struct chipmunk_pedersen_params {
    chipmunk_poly_t A[CHIPMUNK_PEDERSEN_K][CHIPMUNK_LRS_K]; /* Public matrix */
    bool initialized;
} chipmunk_pedersen_params_t;

/* Pedersen opening (for decommitment) */
typedef struct chipmunk_pedersen_opening {
    int64_t message;                            /* Committed value */
    chipmunk_poly_t randomness[CHIPMUNK_LRS_K]; /* Blinding vector r */
} chipmunk_pedersen_opening_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * Initialize Pedersen public parameters from seed.
 * @param params Output parameters.
 * @param seed 32-byte seed for deterministic generation.
 * @return 0 on success.
 */
int chipmunk_pedersen_init(chipmunk_pedersen_params_t *params,
                           const uint8_t seed[32]);

/**
 * Create a Pedersen commitment to a value.
 * C = A * r + encode(m) mod q
 *
 * @param commit Output commitment.
 * @param params Public parameters.
 * @param message Value to commit (stake amount).
 * @param randomness_seed Seed for blinding vector r.
 * @return 0 on success.
 */
int chipmunk_pedersen_commit(chipmunk_pedersen_commit_t *commit,
                             const chipmunk_pedersen_params_t *params,
                             int64_t message,
                             const uint8_t randomness_seed[32]);

/**
 * Create a Pedersen commitment with explicit randomness vector.
 * C = A * r + encode(m) mod q
 *
 * @param commit Output commitment.
 * @param params Public parameters.
 * @param message Value to commit.
 * @param randomness Explicit blinding vector r (K polynomials).
 * @return 0 on success.
 */
int chipmunk_pedersen_commit_explicit(chipmunk_pedersen_commit_t *commit,
                                       const chipmunk_pedersen_params_t *params,
                                       int64_t message,
                                       const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

/**
 * Verify a Pedersen commitment opening.
 * Check that C = A * r + encode(m) mod q.
 *
 * @param commit The commitment.
 * @param params Public parameters.
 * @param opening The opening (message + randomness).
 * @return 1 if valid, 0 if invalid, negative on error.
 */
int chipmunk_pedersen_verify_opening(const chipmunk_pedersen_commit_t *commit,
                                     const chipmunk_pedersen_params_t *params,
                                     const chipmunk_pedersen_opening_t *opening);

/**
 * Add two commitments: C_sum = C1 + C2.
 * Additive homomorphism: Com(m1) + Com(m2) = Com(m1 + m2).
 *
 * @param sum Output sum commitment.
 * @param c1 First commitment.
 * @param c2 Second commitment.
 */
void chipmunk_pedersen_add(chipmunk_pedersen_commit_t *sum,
                           const chipmunk_pedersen_commit_t *c1,
                           const chipmunk_pedersen_commit_t *c2);

/**
 * Serialize commitment to bytes.
 */
int chipmunk_pedersen_commit_serialize(uint8_t *a_out, size_t a_out_size,
                                       const chipmunk_pedersen_commit_t *commit);

/**
 * Deserialize commitment from bytes.
 */
int chipmunk_pedersen_commit_deserialize(chipmunk_pedersen_commit_t *commit,
                                         const uint8_t *a_in, size_t a_in_size);

#ifdef __cplusplus
}
#endif
