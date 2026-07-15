/*
 * chipmunk_fri.h — FRI (Fast Reed-Solomon Interactive Oracle Proof of Proximity).
 *
 * Post-quantum polynomial commitment scheme for Cellframe anonymous transactions.
 * Implements the prover side of FRI over F_q (q = CHIPMUNK_Q = 3168257).
 *
 * FRI parameters:
 *   - Initial codeword: 2048 field elements (RS-encoded degree-511 polynomial)
 *   - Blowup factor: 4 (rate ρ = 1/4)
 *   - FRI rounds: 7 (2048 → 1024 → 512 → 256 → 128 → 64 → 32 → 16)
 *   - Final polynomial: 16 coefficients
 *   - Merkle cap size: 16 nodes per round
 *   - Coset generator: g = 3 (primitive root of F_q*)
 *
 * Folding formula (per round r):
 *   H_{r+1}[l] = [(1 + α_r) · H_r[l] + (1 − α_r) · H_r[l + n_r/2]] / 2
 * where n_r is the codeword length at round r, and α_r ∈ F_q is the
 * verifier's challenge (drawn from the Fiat-Shamir transcript in production;
 * passed as parameter for testing).
 *
 * Proof structure (commit phase only):
 *   - 7 Merkle caps (one per round), each 16 × int32_t
 *   - 16 final polynomial evaluation values
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_FRI_H_
#define _CHIPMUNK_FRI_H_

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk_field.h"
#include "chipmunk_fri_ntt.h"
#include "chipmunk_rs.h"
#include "chipmunk_merkle_pcs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of FRI folding rounds. */
#define CHIPMUNK_FRI_ROUNDS         7u

/* Codeword sizes at each round: 2048, 1024, 512, 256, 128, 64, 32. */
#define CHIPMUNK_FRI_INIT_SIZE      CHIPMUNK_RS_CODE_LEN   /* 2048 */
#define CHIPMUNK_FRI_FINAL_SIZE     16u

/* Merkle cap size (number of top-level nodes stored per round). */
#define CHIPMUNK_FRI_CAP_SIZE       CHIPMUNK_MERKLE_CAP_SIZE  /* 16 */

/* Maximum auth path length (for 2048-leaf tree with cap 16). */
#define CHIPMUNK_FRI_MAX_AUTH_PATH  CHIPMUNK_MERKLE_AUTH_PATH  /* 7 */

/**
 * @brief Merkle cap for one FRI round.
 *
 * Stores the top cap_size nodes of the Merkle tree built over the
 * codeword at that round.  Size: CHIPMUNK_FRI_CAP_SIZE × int32_t.
 */
typedef struct chipmunk_fri_cap {
    int32_t nodes[CHIPMUNK_FRI_CAP_SIZE];
} chipmunk_fri_cap_t;

/**
 * @brief FRI commit proof (output of the prover's commit phase).
 *
 * Contains all Merkle caps (one per round) and the final polynomial
 * evaluations (16 values).  Query openings are handled separately
 * in Phase 9.7.
 */
typedef struct chipmunk_fri_commit_proof {
    chipmunk_fri_cap_t caps[CHIPMUNK_FRI_ROUNDS];
    int32_t           final_evals[CHIPMUNK_FRI_FINAL_SIZE];
} chipmunk_fri_commit_proof_t;

/**
 * @brief Internal state for the FRI prover (used during commit and query phases).
 *
 * Stores all round codewords and Merkle tree scratch space.
 * Allocated by chipmunk_fri_prover_alloc(), freed by chipmunk_fri_prover_free().
 */
typedef struct chipmunk_fri_prover {
    /* Codeword at each round + final evals.  Total: 2048+1024+512+256+128+64+32+16 = 4080 */
    int32_t *round_data;

    /* Merkle tree scratch space (reused across rounds).  Size = 2 × 2048 = 4096 */
    int32_t *merkle_scratch;

    /* Commit proof filled during commit phase. */
    chipmunk_fri_commit_proof_t proof;

    /* Codeword sizes per round: [2048, 1024, 512, 256, 128, 64, 32] */
    uint32_t round_sizes[CHIPMUNK_FRI_ROUNDS];

    /* Cap sizes per round: [16, 16, 16, 16, 16, 16, 16]
     * For small trees (≤16 leaves), cap_size = tree size. */
    uint32_t cap_sizes[CHIPMUNK_FRI_ROUNDS];

    /* Auth path lengths per round. */
    uint32_t auth_path_lens[CHIPMUNK_FRI_ROUNDS];

    /* 2^{-1} mod q (precomputed). */
    int32_t inv_2;

    /* Flag: commit phase completed. */
    bool committed;
} chipmunk_fri_prover_t;

/**
 * @brief Allocate and initialise FRI prover state.
 *
 * @param prover  Pointer to prover struct (caller allocates).
 * @return        0 on success, negative on error.
 */
int chipmunk_fri_prover_init(chipmunk_fri_prover_t *prover);

/**
 * @brief Free FRI prover internal allocations.
 */
void chipmunk_fri_prover_free(chipmunk_fri_prover_t *prover);

/**
 * @brief FRI commit phase: encode polynomial, fold through 7 rounds,
 *        build Merkle trees, store caps and final evaluations.
 *
 * Takes a degree-511 polynomial (512 coefficients), RS-encodes it to 2048
 * evaluations, then performs 7 rounds of FRI folding.
 *
 * @param prover   Initialised prover state.
 * @param poly     Input polynomial coefficients (512 elements).
 * @param alphas   Array of CHIPMUNK_FRI_ROUNDS folding challenges in F_q.
 *                 In production these come from the Fiat-Shamir transcript;
 *                 for testing they are passed directly.
 * @return         0 on success, negative on error.
 */
int chipmunk_fri_commit(chipmunk_fri_prover_t *prover,
                         const int32_t poly[CHIPMUNK_RS_MSG_LEN],
                         const int32_t alphas[CHIPMUNK_FRI_ROUNDS]);

/**
 * @brief Get pointer to round r codeword data.
 *
 * @param prover  Prover state (must be committed).
 * @param round   Round index (0..6).
 * @param len     Output: number of elements.
 * @return        Pointer to codeword array, or NULL on error.
 */
const int32_t *chipmunk_fri_prover_round_data(
    const chipmunk_fri_prover_t *prover, uint32_t round, uint32_t *len);

/**
 * @brief Get the cap for round r.
 */
const chipmunk_fri_cap_t *chipmunk_fri_prover_cap(
    const chipmunk_fri_prover_t *prover, uint32_t round);

/**
 * @brief Get final evaluation values.
 */
const int32_t *chipmunk_fri_prover_final_evals(
    const chipmunk_fri_prover_t *prover);

/**
 * @brief Verify a folding relation between two consecutive rounds.
 *
 * Checks that for a given index l and round r:
 *   H_{r+1}[l] = [(1+α_r)·H_r[l] + (1-α_r)·H_r[l+n_r/2]] / 2
 *
 * @param h_r       Codeword at round r.
 * @param h_r1      Codeword at round r+1.
 * @param n_r       Codeword length at round r.
 * @param alpha     Folding challenge for round r.
 * @param l         Index to check (0 ≤ l < n_r/2).
 * @return          true if relation holds, false otherwise.
 */
bool chipmunk_fri_verify_fold(const int32_t *h_r, const int32_t *h_r1,
                               uint32_t n_r, int32_t alpha, uint32_t l);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FRI_H_ */
