/*
 * chipmunk_merkle_pcs.h — Binary Merkle tree with cap for FRI-DEEP PCS.
 *
 * A simple binary Merkle tree over field elements (int32_t in [0, q))
 * using Poseidon hash (Phase 9.3) as the compression function.
 *
 * Tree structure:
 *   - Leaves: field elements (polynomial evaluations at FRI domain points)
 *   - Internal nodes: Poseidon(parent_left, parent_right)
 *   - Cap: top cap_size nodes stored separately (power of 2, <= n_leaves)
 *
 * The cap optimisation avoids proving the top levels of every Merkle tree
 * in the FRI commit phase.  The verifier receives the cap once and
 * verifies all query paths against it, saving log2(cap_size) auth-path
 * levels per query.
 *
 * FRI usage:
 *   - n_leaves = 2048 (height 11), cap_size = 16 (log2=4)
 *   - Auth path length = 11 - 4 = 7 (CHIPMUNK_MERKLE_AUTH_PATH)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_MERKLE_PCS_H_
#define _CHIPMUNK_MERKLE_PCS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** FRI Merkle tree: 2048 leaves = 2^11. */
#define CHIPMUNK_MERKLE_FRI_NLEAVES  2048u

/** Tree height (log2(2048) = 11). */
#define CHIPMUNK_MERKLE_FRI_HEIGHT   11u

/** Auth path length below cap for FRI: height - log2(cap_size) = 11 - 4 = 7. */
#define CHIPMUNK_MERKLE_AUTH_PATH    7u

/** FRI cap size: 16 nodes (= 2^4). */
#define CHIPMUNK_MERKLE_CAP_SIZE     16u

/**
 * @brief Authenticated path from a leaf to the cap.
 *
 * For each level below the cap, stores the sibling node.  The verifier
 * recomputes from leaf upward using these siblings, reaching the cap.
 *
 * sibling[i] = the node at level i that is NOT on the path to the leaf.
 *   - If leaf is the left child at level i, sibling[i] is the right child.
 *   - If leaf is the right child at level i, sibling[i] is the left child.
 */
typedef struct {
    int32_t sibling[CHIPMUNK_MERKLE_AUTH_PATH];  /**< Sibling at each level 0..6 */
    uint32_t index;                               /**< Original leaf index [0, n_leaves) */
} chipmunk_merkle_auth_path_t;

/**
 * @brief Build a Merkle tree and extract its cap.
 *
 * Builds a binary Merkle tree over n_leaves field elements using
 * Poseidon hash for internal nodes.  The top cap_size nodes are
 * stored in cap[], and the rest is discarded.
 *
 * @param leaves      Input leaf values in [0, q).  Must have exactly n_leaves entries.
 * @param n_leaves    Number of leaves (power of 2, typically 2048).
 * @param cap         Output: cap[cap_size] = top cap_size nodes.
 * @param cap_size    Number of cap nodes (power of 2, >= 1, <= n_leaves).
 *                    For FRI: 16.
 * @param scratch     Caller-provided scratch buffer (size >= 2 * n_leaves * sizeof(int32_t)).
 * @return            0 on success, -1 on invalid arguments.
 */
int chipmunk_merkle_build(const int32_t *leaves, uint32_t n_leaves,
                           int32_t *cap, uint32_t cap_size,
                           int32_t *scratch);

/**
 * @brief Open (prove) a leaf: generate auth path from leaf to cap.
 *
 * Rebuilds the tree levels from the leaves up to the cap, and at each
 * level extracts the sibling node of the path leading to the target index.
 *
 * @param leaves      Leaf values (same as passed to chipmunk_merkle_build).
 * @param n_leaves    Number of leaves.
 * @param index       Leaf index to open [0, n_leaves).
 * @param cap_size    Same cap_size as used in chipmunk_merkle_build.
 * @param path        Output: auth path with sibling nodes.
 * @param scratch     Caller-provided scratch buffer (size >= 2 * n_leaves * sizeof(int32_t)).
 * @return            0 on success, -1 on invalid arguments.
 */
int chipmunk_merkle_open(const int32_t *leaves, uint32_t n_leaves,
                          uint32_t index, uint32_t cap_size,
                          chipmunk_merkle_auth_path_t *path,
                          int32_t *scratch);

/**
 * @brief Verify an auth path against the cap.
 *
 * Starting from the claimed leaf value, recompute upward through the
 * auth path using Poseidon hash.  The final computed node must match
 * cap[cap_index].
 *
 * @param leaf        Claimed leaf value.
 * @param path        Auth path from chipmunk_merkle_open.
 * @param cap         The cap (from chipmunk_merkle_build).
 * @param cap_size    Number of nodes in the cap.
 * @return            true iff the path verifies.
 */
bool chipmunk_merkle_verify(int32_t leaf, uint32_t n_leaves,
                             const chipmunk_merkle_auth_path_t *path,
                             const int32_t *cap, uint32_t cap_size);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MERKLE_PCS_H_ */
