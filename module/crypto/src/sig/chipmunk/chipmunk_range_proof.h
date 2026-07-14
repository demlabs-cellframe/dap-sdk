/*
 * chipmunk_range_proof.h — Lattice-based range proofs for uint256 amounts.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_pedersen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIPMUNK_RANGE_PROOF_MAX_BITS  256
#define CHIPMUNK_RANGE_PROOF_CHALLENGES 128
#define CHIPMUNK_RANGE_PROOF_MAX_SIZE  8192

typedef struct chipmunk_range_proof {
    chipmunk_pedersen_commit_t A;
    chipmunk_pedersen_commit_t B;
    uint8_t transcript_hash[32];
    uint8_t challenges[CHIPMUNK_RANGE_PROOF_CHALLENGES];
    chipmunk_poly_t responses[CHIPMUNK_RANGE_PROOF_CHALLENGES][3];
    uint32_t bits;
    size_t proof_size;
} chipmunk_range_proof_t;

/**
 * Prove that @a value (256-bit LE atomic amount) matches @a commit.
 */
int chipmunk_range_proof_prove(chipmunk_range_proof_t *proof,
                                const chipmunk_pedersen_params_t *params,
                                const chipmunk_pedersen_commit_t *commit,
                                const uint8_t value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                const uint8_t randomness_seed[32]);

/**
 * Prove range with explicit orig_r blinding polynomials.
 * orig_r is used directly for bit_r[255] residual computation.
 * randomness_seed is still needed for per-bit blinding (0..254)
 * and Stern challenge blinding derivations.
 */
int chipmunk_range_proof_prove_explicit(chipmunk_range_proof_t *proof,
                                         const chipmunk_pedersen_params_t *params,
                                         const chipmunk_pedersen_commit_t *commit,
                                         const uint8_t value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                         const chipmunk_poly_t orig_r[CHIPMUNK_LRS_K],
                                         const uint8_t randomness_seed[32]);

int chipmunk_range_proof_verify(const chipmunk_range_proof_t *proof,
                                 const chipmunk_pedersen_params_t *params,
                                 const chipmunk_pedersen_commit_t *commit);

void chipmunk_range_proof_free(chipmunk_range_proof_t *proof);

#ifdef __cplusplus
}
#endif
