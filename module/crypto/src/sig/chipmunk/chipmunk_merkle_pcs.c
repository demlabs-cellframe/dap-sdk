/*
 * chipmunk_merkle_pcs.c — Binary Merkle tree with cap for FRI-DEEP PCS.
 *
 * See chipmunk_merkle_pcs.h for documentation.
 *
 * Cap model:
 *   cap_size = number of top nodes in the cap (power of 2, <= n_leaves).
 *   Auth path length = log2(n_leaves) - log2(cap_size).
 *
 *   For FRI: n_leaves=2048, cap_size=16, auth=7.
 *   For tests: n_leaves=4, cap_size=1 (root only), auth=2.
 *
 * Build: scratch[0..n) = leaves, bottom-up hashing.
 * Open: rebuild tree, extract siblings along path to cap.
 * Verify: walk from leaf upward using siblings, check against cap.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_merkle_pcs.h"
#include "chipmunk_poseidon.h"
#include <string.h>

#include "dap_common.h"

#define LOG_TAG "chipmunk_merkle_pcs"

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static inline int32_t s_hash_pair(int32_t left, int32_t right)
{
    return chipmunk_poseidon_hash2(left, right);
}

static bool s_is_power_of_2(uint32_t n)
{
    return n > 0 && (n & (n - 1u)) == 0;
}

static uint32_t s_log2_pow2(uint32_t n)
{
    uint32_t r = 0;
    while (n > 1u) { n >>= 1u; ++r; }
    return r;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int chipmunk_merkle_build(const int32_t *leaves, uint32_t n_leaves,
                           int32_t *cap, uint32_t cap_size,
                           int32_t *scratch)
{
    if (!leaves || !cap || !scratch)
        return -1;
    if (!s_is_power_of_2(n_leaves) || n_leaves < 2u)
        return -1;
    if (!s_is_power_of_2(cap_size) || cap_size < 1u || cap_size > n_leaves)
        return -1;

    /* Copy leaves to scratch[0..n_leaves) */
    memcpy(scratch, leaves, (size_t)n_leaves * sizeof(int32_t));

    uint32_t n_cur = n_leaves;
    int32_t *cur = scratch;
    int32_t *next = scratch + n_leaves;

    /* Build levels bottom-up until cap_size nodes remain.
     * Levels to build = log2(n_leaves) - log2(cap_size). */
    uint32_t levels_to_build = s_log2_pow2(n_leaves) - s_log2_pow2(cap_size);

    for (uint32_t lvl = 0; lvl < levels_to_build; ++lvl) {
        uint32_t n_next = n_cur >> 1u;
        for (uint32_t i = 0; i < n_next; ++i) {
            next[i] = s_hash_pair(cur[2u * i], cur[2u * i + 1u]);
        }
        /* Swap */
        int32_t *tmp = cur;
        cur = next;
        next = tmp;
        n_cur = n_next;
    }

    /* cur now has cap_size nodes — copy to cap */
    memcpy(cap, cur, (size_t)cap_size * sizeof(int32_t));

    return 0;
}

int chipmunk_merkle_open(const int32_t *leaves, uint32_t n_leaves,
                          uint32_t index, uint32_t cap_size,
                          chipmunk_merkle_auth_path_t *path,
                          int32_t *scratch)
{
    if (!leaves || !path || !scratch)
        return -1;
    if (!s_is_power_of_2(n_leaves) || index >= n_leaves)
        return -1;
    if (!s_is_power_of_2(cap_size) || cap_size < 1u || cap_size > n_leaves)
        return -1;

    path->index = index;

    /* Copy leaves */
    memcpy(scratch, leaves, (size_t)n_leaves * sizeof(int32_t));

    uint32_t n_cur = n_leaves;
    int32_t *cur = scratch;
    int32_t *next = scratch + n_leaves;

    uint32_t levels_below_cap = s_log2_pow2(n_leaves) - s_log2_pow2(cap_size);

    for (uint32_t lvl = 0; lvl < levels_below_cap; ++lvl) {
        /* Record sibling (only if it fits in the auth path array) */
        if (lvl < CHIPMUNK_MERKLE_AUTH_PATH) {
            path->sibling[lvl] = cur[index ^ 1u];
        }

        /* Build next level */
        uint32_t n_next = n_cur >> 1u;
        for (uint32_t i = 0; i < n_next; ++i) {
            next[i] = s_hash_pair(cur[2u * i], cur[2u * i + 1u]);
        }

        /* Swap */
        int32_t *tmp = cur;
        cur = next;
        next = tmp;
        n_cur = n_next;

        index >>= 1u;
    }

    /* Zero out unused sibling slots */
    for (uint32_t lvl = levels_below_cap; lvl < CHIPMUNK_MERKLE_AUTH_PATH; ++lvl) {
        path->sibling[lvl] = 0;
    }

    return 0;
}

bool chipmunk_merkle_verify(int32_t leaf, uint32_t n_leaves,
                             const chipmunk_merkle_auth_path_t *path,
                             const int32_t *cap, uint32_t cap_size)
{
    if (!path || !cap)
        return false;
    if (!s_is_power_of_2(n_leaves) || n_leaves < 2u)
        return false;
    if (!s_is_power_of_2(cap_size) || cap_size < 1u || cap_size > n_leaves)
        return false;

    uint32_t levels_below_cap = s_log2_pow2(n_leaves) - s_log2_pow2(cap_size);
    if (levels_below_cap > CHIPMUNK_MERKLE_AUTH_PATH)
        return false;

    /* Walk from leaf up through the auth path to the cap */
    uint32_t index = path->index;
    int32_t current = leaf;

    for (uint32_t lvl = 0; lvl < levels_below_cap; ++lvl) {
        if (index & 1u) {
            current = s_hash_pair(path->sibling[lvl], current);
        } else {
            current = s_hash_pair(current, path->sibling[lvl]);
        }
        index >>= 1u;
    }

    /* index is now the position in the cap */
    if (index >= cap_size)
        return false;

    return current == cap[index];
}
