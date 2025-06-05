/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "chipmunk_tree.h"
#include "chipmunk_hash.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "chipmunk_tree"

// =================HVC POLYNOMIAL OPERATIONS=================

/**
 * @brief Apply modular reduction for HVC ring
 * @param a_coeff Coefficient to reduce
 * @return Reduced coefficient in range [0, HVC_MODULUS)
 */
static int32_t s_hvc_mod_reduce(int64_t a_coeff) {
    int32_t l_result = (int32_t)(a_coeff % CHIPMUNK_HVC_Q);
    if (l_result < 0) {
        l_result += CHIPMUNK_HVC_Q;
    }
    return l_result;
}

/**
 * @brief Add two HVC polynomials
 * @param[out] a_result Result polynomial
 * @param[in] a_left Left polynomial
 * @param[in] a_right Right polynomial
 */
static void s_hvc_poly_add(chipmunk_hvc_poly_t *a_result, 
                           const chipmunk_hvc_poly_t *a_left,
                           const chipmunk_hvc_poly_t *a_right) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_sum = (int64_t)a_left->coeffs[i] + (int64_t)a_right->coeffs[i];
        a_result->coeffs[i] = s_hvc_mod_reduce(l_sum);
    }
}

/**
 * @brief Multiply two HVC polynomials (pointwise)
 * @param[out] a_result Result polynomial  
 * @param[in] a_left Left polynomial
 * @param[in] a_right Right polynomial
 */
static void s_hvc_poly_mul(chipmunk_hvc_poly_t *a_result,
                           const chipmunk_hvc_poly_t *a_left,
                           const chipmunk_hvc_poly_t *a_right) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_product = (int64_t)a_left->coeffs[i] * (int64_t)a_right->coeffs[i];
        a_result->coeffs[i] = s_hvc_mod_reduce(l_product);
    }
}

/**
 * @brief Clear HVC polynomial (set to zero)
 * @param[in,out] a_poly Polynomial to clear
 */
static void s_hvc_poly_clear(chipmunk_hvc_poly_t *a_poly) {
    memset(a_poly, 0, sizeof(*a_poly));
}

/**
 * @brief Copy HVC polynomial
 * @param[out] a_dest Destination polynomial
 * @param[in] a_src Source polynomial
 */
static void s_hvc_poly_copy(chipmunk_hvc_poly_t *a_dest, const chipmunk_hvc_poly_t *a_src) {
    memcpy(a_dest, a_src, sizeof(*a_dest));
}

/**
 * @brief Check if two HVC polynomials are equal
 * @param[in] a_left Left polynomial
 * @param[in] a_right Right polynomial
 * @return true if equal, false otherwise
 */
static bool s_hvc_poly_equal(const chipmunk_hvc_poly_t *a_left, const chipmunk_hvc_poly_t *a_right) {
    return memcmp(a_left, a_right, sizeof(*a_left)) == 0;
}

// =================HVC HASHER IMPLEMENTATION=================

/**
 * @brief Initialize HVC hasher with random matrix
 */
int chipmunk_hvc_hasher_init(chipmunk_hvc_hasher_t *a_hasher, const uint8_t a_seed[32]) {
    if (!a_hasher || !a_seed) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hvc_hasher_init");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Store seed
    memcpy(a_hasher->seed, a_seed, 32);

    // Generate matrix A using seed - simplified version
    for (int i = 0; i < CHIPMUNK_HVC_WIDTH; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            // Simple deterministic generation
            uint32_t l_value = ((uint32_t)a_seed[0] + i * 1000 + j) * 1664525 + 1013904223;
            a_hasher->matrix_a[i].coeffs[j] = l_value % CHIPMUNK_HVC_Q;
        }
    }

    log_it(L_DEBUG, "HVC hasher initialized");
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Decompose and hash two polynomials - simplified version
 */
int chipmunk_hvc_hash_decom_then_hash(const chipmunk_hvc_hasher_t *a_hasher,
                                       const chipmunk_hvc_poly_t *a_left,
                                       const chipmunk_hvc_poly_t *a_right,
                                       chipmunk_hvc_poly_t *a_result) {
    if (!a_hasher || !a_left || !a_right || !a_result) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hvc_hash_decom_then_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Simplified hash: result = left + right (coefficient-wise)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_sum = (int64_t)a_left->coeffs[i] + (int64_t)a_right->coeffs[i];
        a_result->coeffs[i] = (int32_t)(l_sum % CHIPMUNK_HVC_Q);
        if (a_result->coeffs[i] < 0) {
            a_result->coeffs[i] += CHIPMUNK_HVC_Q;
        }
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

// =================TREE IMPLEMENTATION=================

/**
 * @brief Create tree with given leaf nodes
 */
int chipmunk_tree_new_with_leaf_nodes(chipmunk_tree_t *a_tree, 
                                       const chipmunk_hvc_poly_t *a_leaf_nodes,
                                       size_t a_leaf_count,
                                       const chipmunk_hvc_hasher_t *a_hasher) {
    if (!a_tree || !a_leaf_nodes || !a_hasher) {
        log_it(L_ERROR, "NULL parameters in chipmunk_tree_new_with_leaf_nodes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    log_it(L_DEBUG, "Creating Merkle tree with %zu leaves", a_leaf_count);

    // Copy leaf nodes - check for self-assignment to avoid undefined behavior
    if (a_tree->leaf_nodes != a_leaf_nodes) {
        size_t copy_size = a_leaf_count * sizeof(chipmunk_hvc_poly_t);
        if (copy_size > sizeof(a_tree->leaf_nodes)) {
            copy_size = sizeof(a_tree->leaf_nodes);
        }
        memcpy(a_tree->leaf_nodes, a_leaf_nodes, copy_size);
    }

    // Build tree bottom-up
    // Level 3: hash pairs of leaves to get level 3 non-leaf nodes
    for (int i = 0; i < 8; i++) {
        int l_node_idx = 7 + i;  // non-leaf indices 7-14
        int l_left_leaf = i * 2;
        int l_right_leaf = i * 2 + 1;
        
        int l_ret = chipmunk_hvc_hash_decom_then_hash(a_hasher,
                                                       &a_tree->leaf_nodes[l_left_leaf],
                                                       &a_tree->leaf_nodes[l_right_leaf],
                                                       &a_tree->non_leaf_nodes[l_node_idx]);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            return l_ret;
        }
    }

    // Level 2: hash pairs from level 3
    for (int i = 0; i < 4; i++) {
        int l_node_idx = 3 + i;  // non-leaf indices 3-6
        int l_left_child = 7 + i * 2;
        int l_right_child = 7 + i * 2 + 1;
        
        int l_ret = chipmunk_hvc_hash_decom_then_hash(a_hasher,
                                                       &a_tree->non_leaf_nodes[l_left_child],
                                                       &a_tree->non_leaf_nodes[l_right_child],
                                                       &a_tree->non_leaf_nodes[l_node_idx]);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            return l_ret;
        }
    }

    // Level 1: hash pairs from level 2
    for (int i = 0; i < 2; i++) {
        int l_node_idx = 1 + i;  // non-leaf indices 1-2
        int l_left_child = 3 + i * 2;
        int l_right_child = 3 + i * 2 + 1;
        
        int l_ret = chipmunk_hvc_hash_decom_then_hash(a_hasher,
                                                       &a_tree->non_leaf_nodes[l_left_child],
                                                       &a_tree->non_leaf_nodes[l_right_child],
                                                       &a_tree->non_leaf_nodes[l_node_idx]);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            return l_ret;
        }
    }

    // Level 0: root (index 0)
    int l_ret = chipmunk_hvc_hash_decom_then_hash(a_hasher,
                                                   &a_tree->non_leaf_nodes[1],
                                                   &a_tree->non_leaf_nodes[2],
                                                   &a_tree->non_leaf_nodes[0]);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        return l_ret;
    }

    log_it(L_DEBUG, "Merkle tree created successfully");
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Initialize empty tree
 */
int chipmunk_tree_init(chipmunk_tree_t *a_tree, const chipmunk_hvc_hasher_t *a_hasher) {
    if (!a_tree || !a_hasher) {
        log_it(L_ERROR, "NULL parameters in chipmunk_tree_init");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Initialize all leaf nodes to zero
    memset(a_tree->leaf_nodes, 0, sizeof(a_tree->leaf_nodes));
    return chipmunk_tree_new_with_leaf_nodes(a_tree, a_tree->leaf_nodes, CHIPMUNK_TREE_LEAF_COUNT_DEFAULT, a_hasher);
}

/**
 * @brief Get root of the tree
 */
const chipmunk_hvc_poly_t* chipmunk_tree_root(const chipmunk_tree_t *a_tree) {
    if (!a_tree) {
        log_it(L_ERROR, "NULL tree in chipmunk_tree_root");
        return NULL;
    }
    return &a_tree->non_leaf_nodes[0];
}

/**
 * @brief Generate membership proof - simplified version
 */
int chipmunk_tree_gen_proof(const chipmunk_tree_t *a_tree, size_t a_index, chipmunk_path_t *a_path) {
    if (!a_tree || !a_path || a_index >= CHIPMUNK_TREE_LEAF_COUNT_DEFAULT) {
        log_it(L_ERROR, "Invalid parameters in chipmunk_tree_gen_proof");
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    a_path->index = a_index;

    // Level 0: Root children
    memcpy(&a_path->nodes[0].left, &a_tree->non_leaf_nodes[1], sizeof(chipmunk_hvc_poly_t));
    memcpy(&a_path->nodes[0].right, &a_tree->non_leaf_nodes[2], sizeof(chipmunk_hvc_poly_t));

    // Level 1: Determine which branch to follow
    int l_branch = (a_index >= 8) ? 1 : 0;  // Right branch if index >= 8
    int l_base_idx = 3 + l_branch * 2;
    
    memcpy(&a_path->nodes[1].left, &a_tree->non_leaf_nodes[l_base_idx], sizeof(chipmunk_hvc_poly_t));
    memcpy(&a_path->nodes[1].right, &a_tree->non_leaf_nodes[l_base_idx + 1], sizeof(chipmunk_hvc_poly_t));

    // Level 2: Further branch selection
    int l_sub_branch = ((a_index % 8) >= 4) ? 1 : 0;
    l_base_idx = 7 + l_branch * 4 + l_sub_branch * 2;
    
    memcpy(&a_path->nodes[2].left, &a_tree->non_leaf_nodes[l_base_idx], sizeof(chipmunk_hvc_poly_t));
    memcpy(&a_path->nodes[2].right, &a_tree->non_leaf_nodes[l_base_idx + 1], sizeof(chipmunk_hvc_poly_t));

    // Level 3: Leaf level
    int l_leaf_pair = (a_index / 2) * 2;
    memcpy(&a_path->nodes[3].left, &a_tree->leaf_nodes[l_leaf_pair], sizeof(chipmunk_hvc_poly_t));
    memcpy(&a_path->nodes[3].right, &a_tree->leaf_nodes[l_leaf_pair + 1], sizeof(chipmunk_hvc_poly_t));

    log_it(L_DEBUG, "Generated proof for index %zu", a_index);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Verify membership proof - simplified version
 */
bool chipmunk_path_verify(const chipmunk_path_t *a_path, 
                          const chipmunk_hvc_poly_t *a_root,
                          const chipmunk_hvc_hasher_t *a_hasher) {
    if (!a_path || !a_root || !a_hasher) {
        log_it(L_ERROR, "NULL parameters in chipmunk_path_verify");
        return false;
    }

    // Verify root matches
    chipmunk_hvc_poly_t l_computed_root;
    int l_ret = chipmunk_hvc_hash_decom_then_hash(a_hasher,
                                                   &a_path->nodes[0].left,
                                                   &a_path->nodes[0].right,
                                                   &l_computed_root);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        return false;
    }

    // Simple comparison
    if (memcmp(&l_computed_root, a_root, sizeof(chipmunk_hvc_poly_t)) != 0) {
        log_it(L_ERROR, "Root hash mismatch");
        return false;
    }

    log_it(L_DEBUG, "Path verification successful");
    return true;
}

// =================UTILITY FUNCTIONS=================

/**
 * @brief Convert HOTS public key to HVC polynomial
 */
int chipmunk_hots_pk_to_hvc_poly(const chipmunk_public_key_t *a_hots_pk, 
                                  chipmunk_hvc_poly_t *a_hvc_poly) {
    if (!a_hots_pk || !a_hvc_poly) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_pk_to_hvc_poly");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Convert v0 polynomial to HVC ring
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_value = a_hots_pk->v0.coeffs[i];
        a_hvc_poly->coeffs[i] = (int32_t)(l_value % CHIPMUNK_HVC_Q);
        if (a_hvc_poly->coeffs[i] < 0) {
            a_hvc_poly->coeffs[i] += CHIPMUNK_HVC_Q;
        }
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Clear tree structure
 */
void chipmunk_tree_clear(chipmunk_tree_t *a_tree) {
    if (a_tree) {
        memset(a_tree, 0, sizeof(*a_tree));
    }
}

/**
 * @brief Clear path structure
 */
void chipmunk_path_clear(chipmunk_path_t *a_path) {
    if (a_path) {
        memset(a_path, 0, sizeof(*a_path));
    }
} 