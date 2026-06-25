/*
 * chipmunk_range_proof.h — Lattice-based range proofs for confidential stakes.
 *
 * Proves that a committed value m lies in [0, 2^bits) without revealing m.
 * Uses Stern-like protocol over R_q for post-quantum security.
 *
 * Based on: "Efficient Range Proofs from Lattice Assumptions"
 * Adapted for Chipmunk's polynomial ring R_q = Z_q[X]/(X^N+1).
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

/* -------------------------------------------------------------------------
 * Parameters
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_RANGE_PROOF_MAX_BITS  64      /* Max bits for range [0, 2^64) */
#define CHIPMUNK_RANGE_PROOF_CHALLENGES 128     /* Stern challenges for 128-bit soundness */
#define CHIPMUNK_RANGE_PROOF_MAX_SIZE  8192    /* Max proof size in bytes */

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Range proof: Stern-like protocol transcript */
typedef struct chipmunk_range_proof {
    /* Commitment phase */
    chipmunk_pedersen_commit_t A;           /* Commitment to randomness */
    chipmunk_pedersen_commit_t B;           /* Commitment to blinded bits */
    uint8_t transcript_hash[32];            /* Hash of all commitments */

    /* Challenge-response (Stern-like) */
    uint8_t challenges[CHIPMUNK_RANGE_PROOF_CHALLENGES]; /* Binary challenges */
    chipmunk_poly_t responses[CHIPMUNK_RANGE_PROOF_CHALLENGES][3]; /* Responses */

    /* Metadata */
    uint32_t bits;                          /* Range: [0, 2^bits) */
    size_t proof_size;
} chipmunk_range_proof_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * Generate a range proof for a committed value.
 * Proves: value ∈ [0, 2^bits) without revealing value.
 *
 * @param proof Output proof.
 * @param params Pedersen public parameters.
 * @param commit The Pedersen commitment to the value.
 * @param value The actual value (witness).
 * @param randomness_seed Seed for the randomness used in commitment.
 * @param bits Number of bits in range (e.g., 64 for stake amounts).
 * @return 0 on success, negative on error.
 */
int chipmunk_range_proof_prove(chipmunk_range_proof_t *proof,
                                const chipmunk_pedersen_params_t *params,
                                const chipmunk_pedersen_commit_t *commit,
                                int64_t value,
                                const uint8_t randomness_seed[32],
                                uint32_t bits);

/**
 * Verify a range proof.
 * Checks: committed value ∈ [0, 2^bits) without learning the value.
 *
 * @param proof The proof to verify.
 * @param params Pedersen public parameters.
 * @param commit The Pedersen commitment.
 * @return 1 if valid, 0 if invalid, negative on error.
 */
int chipmunk_range_proof_verify(const chipmunk_range_proof_t *proof,
                                 const chipmunk_pedersen_params_t *params,
                                 const chipmunk_pedersen_commit_t *commit);

/**
 * Free range proof resources.
 */
void chipmunk_range_proof_free(chipmunk_range_proof_t *proof);

#ifdef __cplusplus
}
#endif
