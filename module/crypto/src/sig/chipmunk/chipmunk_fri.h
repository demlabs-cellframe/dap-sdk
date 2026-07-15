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
#include "chipmunk_fri_transcript.h"

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

/* Number of FRI queries (verifier checks). */
#define CHIPMUNK_FRI_NUM_QUERIES   8u

/**
 * @brief A single query opening: one leaf value + Merkle auth path per round.
 *
 * For round r, stores the leaf value at index `idx` and its auth path
 * to the cap.  The verifier checks:
 *   1. Auth path: leaf → cap (Merkle verify)
 *   2. Folding: H_{r+1}[idx/2] = [(1+α_r)·H_r[idx] + (1-α_r)·H_r[idx^n/2]] / 2
 */
typedef struct chipmunk_fri_query_opening {
    /** Leaf index at round 0 (in [0, 2048)). */
    uint32_t idx;

    /** Leaf values at each round (the queried leaf, not the sibling). */
    int32_t leaf_values[CHIPMUNK_FRI_ROUNDS];

    /** Sibling leaf values (the antipodal pair) at each round. */
    int32_t sibling_values[CHIPMUNK_FRI_ROUNDS];

    /** Merkle auth paths at each round (path to cap). */
    chipmunk_merkle_auth_path_t paths[CHIPMUNK_FRI_ROUNDS];
} chipmunk_fri_query_opening_t;

/**
 * @brief Complete FRI proof: commit + query openings.
 */
typedef struct chipmunk_fri_proof {
    chipmunk_fri_commit_proof_t  commit;
    chipmunk_fri_query_opening_t queries[CHIPMUNK_FRI_NUM_QUERIES];
} chipmunk_fri_proof_t;

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

/**
 * @brief FRI query phase: open multiple query positions across all rounds.
 *
 * For each query index, opens the leaf and its antipodal sibling at every
 * FRI round, generates Merkle auth paths, and stores the results.
 *
 * The query indices are provided by the caller (derived from the Fiat-Shamir
 * transcript in production; fixed for testing).
 *
 * @param prover       Prover state (must be committed).
 * @param num_queries  Number of queries (≤ CHIPMUNK_FRI_NUM_QUERIES).
 * @param indices      Query indices at round 0, each in [0, 2048).
 * @param out          Output: array of query openings.
 * @return             0 on success, negative on error.
 */
int chipmunk_fri_query(chipmunk_fri_prover_t *prover,
                        uint32_t num_queries,
                        const uint32_t indices[num_queries],
                        chipmunk_fri_query_opening_t out[num_queries]);

/**
 * @brief Verify a single query opening against the commit proof.
 *
 * For each round r = 0..6:
 *   1. Verify Merkle auth path: leaf → cap[r]
 *   2. Verify folding relation: leaf + sibling → next round leaf
 *
 * @param proof     FRI proof (commit + query).
 * @param q         Index of the query to verify.
 * @param alphas    Folding challenges (7 values).
 * @return          true if all checks pass, false otherwise.
 */
bool chipmunk_fri_verify_query(const chipmunk_fri_proof_t *proof,
                                uint32_t q,
                                const int32_t alphas[CHIPMUNK_FRI_ROUNDS]);

/**
 * @brief FRI proof verification result.
 */
typedef struct chipmunk_fri_verify_result {
    bool     valid;                /**< Overall verification result. */
    uint32_t grinding_nonce;       /**< Prover's grinding nonce (input). */
    uint32_t failed_query;         /**< Index of first failing query, or NUM_QUERIES if none. */
    uint32_t failed_round;         /**< Round index of first failure within query. */
    char     reason[64];           /**< Human-readable failure reason. */
} chipmunk_fri_verify_result_t;

/**
 * @brief Full FRI proof verification (verifier-side).
 *
 * Reconstructs the Fiat-Shamir transcript from the proof's Merkle caps
 * and the given alphas, verifies the grinding PoW, derives query indices,
 * then verifies all 8 query openings (Merkle auth paths + folding relations).
 *
 * In the full SNARK pipeline (Phase 9.11), alphas are derived from the
 * DEEP transcript before FRI commit. Here they are provided explicitly.
 *
 * @param proof        Complete FRI proof (commit + queries).
 * @param domain       16-byte domain separator (must match prover's).
 * @param alphas       7 FRI folding challenges in F_q.
 * @param result       Output: verification result details (may be NULL).
 * @return true if all checks pass, false otherwise.
 */
bool chipmunk_fri_verify(const chipmunk_fri_proof_t *proof,
                           const uint8_t domain[16],
                           const int32_t alphas[CHIPMUNK_FRI_ROUNDS],
                           chipmunk_fri_verify_result_t *result);

/**
 * @brief Fast FRI proof verification (verifier-side, no grinding search).
 *
 * Identical to chipmunk_fri_verify() but uses the prover's grinding nonce
 * directly (single hash verification) instead of performing the expensive
 * brute-force nonce search (~2^16 hashes).
 *
 * @param proof        Complete FRI proof (commit + queries).
 * @param domain       16-byte domain separator (must match prover's).
 * @param alphas       7 FRI folding challenges in F_q.
 * @param grinding_nonce  Prover's grinding nonce (from proof).
 * @param result       Output: verification result details (may be NULL).
 * @return true if all checks pass, false otherwise.
 */
bool chipmunk_fri_verify_fast(const chipmunk_fri_proof_t *proof,
                              const uint8_t domain[16],
                              const int32_t alphas[CHIPMUNK_FRI_ROUNDS],
                              uint32_t grinding_nonce,
                              chipmunk_fri_verify_result_t *result);

/**
 * @brief Derive FRI query indices from transcript.
 *
 * Squeezes num_queries indices in [0, domain_size) from the transcript.
 * Each index is derived by squeezing a value and taking modulo domain_size.
 *
 * @param tr           Transcript (must be finalized with caps absorbed).
 * @param num_queries  Number of indices to derive.
 * @param domain_size  Domain size (e.g. 2048).
 * @param out          Output: array of query indices.
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_derive_query_indices(chipmunk_fri_transcript_t *tr,
                                       uint32_t num_queries,
                                       uint32_t domain_size,
                                       uint32_t out[]);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FRI_H_ */
