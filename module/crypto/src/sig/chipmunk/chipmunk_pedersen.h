/*
 * chipmunk_pedersen.h — Lattice-based Pedersen commitments for confidential amounts.
 *
 * Commitment: C = A * r + encode(m) mod q
 * where A is a public matrix, r is a random blinding vector, m is a uint256 amount.
 *
 * Amount encoding: bit i of the 256-bit LE value is stored at polynomial coeff[i].
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_lrs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIPMUNK_PEDERSEN_K         6
#define CHIPMUNK_PEDERSEN_COMMIT_BYTES  (CHIPMUNK_PEDERSEN_K * 1408)
#define CHIPMUNK_PEDERSEN_VALUE_BYTES   32
#define CHIPMUNK_PEDERSEN_VALUE_BITS    256

typedef struct chipmunk_pedersen_commit {
    chipmunk_poly_t C[CHIPMUNK_PEDERSEN_K];
} chipmunk_pedersen_commit_t;

typedef struct chipmunk_pedersen_params {
    chipmunk_poly_t A[CHIPMUNK_PEDERSEN_K][CHIPMUNK_LRS_K];
    bool initialized;
} chipmunk_pedersen_params_t;

typedef struct chipmunk_pedersen_opening {
    uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    chipmunk_poly_t randomness[CHIPMUNK_LRS_K];
} chipmunk_pedersen_opening_t;

int chipmunk_pedersen_init(chipmunk_pedersen_params_t *params,
                           const uint8_t seed[32]);

/**
 * Commit to a 256-bit atomic amount (LE bytes).
 * Bit i of the value is encoded at polynomial coefficient i.
 */
int chipmunk_pedersen_commit(chipmunk_pedersen_commit_t *commit,
                             const chipmunk_pedersen_params_t *params,
                             const uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                             const uint8_t randomness_seed[32]);

/**
 * Commit to a single bit (0/1) at coefficient @a bit_pos (used by range proofs).
 */
int chipmunk_pedersen_commit_explicit_bit(chipmunk_pedersen_commit_t *commit,
                                          const chipmunk_pedersen_params_t *params,
                                          uint8_t bit,
                                          uint32_t bit_pos,
                                          const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

int chipmunk_pedersen_verify_opening(const chipmunk_pedersen_commit_t *commit,
                                     const chipmunk_pedersen_params_t *params,
                                     const chipmunk_pedersen_opening_t *opening);

void chipmunk_pedersen_add(chipmunk_pedersen_commit_t *sum,
                           const chipmunk_pedersen_commit_t *c1,
                           const chipmunk_pedersen_commit_t *c2);

/**
 * Derive blinding polynomial vector from a 32-byte seed.
 * Extracts the deterministic SHAKE256-based derivation used internally
 * by chipmunk_pedersen_commit (seed || "pedersen-randomness-v1").
 */
int chipmunk_pedersen_derive_blinding(chipmunk_poly_t r[CHIPMUNK_LRS_K],
                                       const uint8_t randomness_seed[32]);

/**
 * Commit to a 256-bit amount with explicit blinding polynomials.
 * Same as chipmunk_pedersen_commit but accepts pre-derived randomness
 * instead of deriving from a seed.
 */
int chipmunk_pedersen_commit_explicit(chipmunk_pedersen_commit_t *commit,
                                       const chipmunk_pedersen_params_t *params,
                                       const uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                       const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

/**
 * Subtract blinding vectors: result[j] = a[j] - b[j]  (mod q), for each j in [0, LRS_K).
 */
void chipmunk_pedersen_blinding_sub(chipmunk_poly_t result[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t a[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t b[CHIPMUNK_LRS_K]);

int chipmunk_pedersen_commit_serialize(uint8_t *a_out, size_t a_out_size,
                                       const chipmunk_pedersen_commit_t *commit);

int chipmunk_pedersen_commit_deserialize(chipmunk_pedersen_commit_t *commit,
                                         const uint8_t *a_in, size_t a_in_size);

#ifdef __cplusplus
}
#endif
