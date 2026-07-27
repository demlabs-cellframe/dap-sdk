/*
 * chipmunk_poseidon.h — Poseidon hash over F_q for FRI-DEEP PCS.
 *
 * Implements the Poseidon permutation [AS+20] over the scalar field
 * F_q (q = 3168257, ~21.6 bits).  Used for:
 *   - Merkle tree leaf/node hashing (Phase 9.4)
 *   - Fiat-Shamir transcript sponge (Phase 9.9)
 *
 * Parameters (standardised for t=3, rate=2):
 *   t   = 3  (state width: 1 capacity word + 2 rate words)
 *   R_F = 8  (full rounds: 4 at start + 4 at end)
 *   R_P = 22 (partial rounds in the middle)
 *   R   = 30 (total rounds)
 *   S-box: x^5 (gcd(5, q-1) = 1, so bijective)
 *
 * MDS matrix: 3x3 Cauchy matrix over F_q (guaranteed MDS property).
 * Round constants: SHAKE256("ChipmunkPoseidon-FRI-Poseidon1" || counter).
 *
 * Hash API:
 *   poseidon_hash2(out, left, right) — Merkle leaf hash: 2 preimages → 1 digest
 *   poseidon_perm(state[3])           — raw permutation (for sponge mode)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_POSEIDON_H_
#define _CHIPMUNK_POSEIDON_H_

#include <stdint.h>
#include "chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State width (capacity=1, rate=2). */
#define CHIPMUNK_POSEIDON_T       3u

/** Number of full rounds (half at start, half at end). */
#define CHIPMUNK_POSEIDON_RF      8u

/** Number of partial rounds (middle). */
#define CHIPMUNK_POSEIDON_RP      22u

/** Total number of rounds. */
#define CHIPMUNK_POSEIDON_R       (CHIPMUNK_POSEIDON_RF / 2u + CHIPMUNK_POSEIDON_RP + CHIPMUNK_POSEIDON_RF / 2u)  /* 30 */

/**
 * @brief Poseidon permutation on a 3-word state.
 *
 * Applies the full Poseidon permutation: R rounds of AddRoundConstant → S-box → MDS.
 * Full rounds apply x^5 to all 3 state words; partial rounds apply it only to word 0.
 *
 * @param state  In/out: 3 field elements in [0, q).
 */
void chipmunk_poseidon_perm(int32_t state[CHIPMUNK_POSEIDON_T]);

/**
 * @brief Poseidon hash: 2 field elements → 1 field element.
 *
 * Implements a 1-to-1 permutation: sets state = [0, left, right],
 * applies the Poseidon permutation, returns state[0] (capacity word).
 *
 * This is the primary interface for Merkle tree leaf/node hashing:
 *   hash(left_child, right_child) → parent.
 *
 * @param left   Left preimage field element in [0, q).
 * @param right  Right preimage field element in [0, q).
 * @return       Hash digest (field element in [0, q)).
 */
int32_t chipmunk_poseidon_hash2(int32_t left, int32_t right);

/**
 * @brief Initialise the Poseidon hash module.
 *
 * Currently a no-op (no lazy state needed — all constants are compile-time).
 * Exists for API consistency with other chipmunk modules.
 *
 * @return 0 (always succeeds).
 */
int chipmunk_poseidon_init(void);

/* ===== Phase 9.14g: Per-q Poseidon ===== */

/** Per-q Poseidon parameters (heap-allocated MDS + round constants). */
typedef struct chipmunk_poseidon_params {
    uint64_t  q;                              /* field modulus */
    int32_t   mds[CHIPMUNK_POSEIDON_T][CHIPMUNK_POSEIDON_T]; /* Cauchy MDS */
    int32_t   rc[CHIPMUNK_POSEIDON_R][CHIPMUNK_POSEIDON_T];  /* round constants */
} chipmunk_poseidon_params_t;

/**
 * @brief Compute Poseidon parameters for an arbitrary prime q.
 *
 * Builds the Cauchy MDS matrix M[i][j] = (x_i + y_j)^{-1} mod q
 * (x = {0,1,2}, y = {3,4,5}) and generates round constants via
 * SHAKE256("ChipmunkPoseidon-FRI-Poseidon1" || counter) with rejection
 * sampling into [0, q).
 *
 * @param a_out  Output parameters struct.
 * @param q      Prime modulus.
 * @return 0 on success, negative on error.
 */
int chipmunk_poseidon_params_compute(chipmunk_poseidon_params_t *a_out, uint64_t q);

/** @brief Free heap resources (currently no-op since MDS+RC are inline). */
void chipmunk_poseidon_params_free(chipmunk_poseidon_params_t *a_params);

/** @brief Per-q Poseidon permutation. */
void chipmunk_poseidon_perm_q(int32_t state[CHIPMUNK_POSEIDON_T],
                                const chipmunk_poseidon_params_t *a_params);

/** @brief Per-q Poseidon hash2. */
int32_t chipmunk_poseidon_hash2_q(int32_t left, int32_t right,
                                    const chipmunk_poseidon_params_t *a_params);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_POSEIDON_H_ */
