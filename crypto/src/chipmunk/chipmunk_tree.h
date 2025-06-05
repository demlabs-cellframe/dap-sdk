/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2025
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

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "chipmunk.h"
#include "chipmunk_poly.h"

/**
 * @file chipmunk_tree.h
 * @brief Merkle Tree implementation for Chipmunk Multi-Signature scheme
 * 
 * Based on original Rust implementation from Chipmunk paper:
 * "Chipmunk: Better Synchronized Multi-Signatures from Lattices"
 * 
 * Tree structure:
 * - HEIGHT = 5 levels
 * - 2^(HEIGHT-1) = 16 leaf nodes (HOTS public keys)
 * - Non-leaf nodes store HVC polynomial hashes
 * - Level-order storage: root at index 0
 */

// Tree parameters from original Rust param.rs
#define CHIPMUNK_TREE_HEIGHT         5                           ///< Height of the Merkle tree
#define CHIPMUNK_TREE_LEAF_COUNT     (1 << (CHIPMUNK_TREE_HEIGHT - 1))  ///< Number of leaves = 2^(HEIGHT-1) = 16
#define CHIPMUNK_TREE_NON_LEAF_COUNT (CHIPMUNK_TREE_LEAF_COUNT - 1)     ///< Number of non-leaf nodes = 15

/**
 * @brief HVC polynomial structure for tree nodes
 * @details Uses smaller ring for efficient tree operations
 * Based on original Rust HVCPoly with HVC_MODULUS = 202753
 */
typedef struct chipmunk_hvc_poly {
    int32_t coeffs[CHIPMUNK_N];  ///< Coefficients in HVC ring Z_q[X]/(X^N + 1)
} chipmunk_hvc_poly_t;

/**
 * @brief Merkle tree structure for organizing HOTS public keys
 * @details Stores tree in level-order: root at index 0, children at 2*i+1, 2*i+2
 * Follows original Rust Tree implementation
 */
typedef struct chipmunk_tree {
    chipmunk_hvc_poly_t non_leaf_nodes[CHIPMUNK_TREE_NON_LEAF_COUNT]; ///< Non-leaf nodes in level order
    chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT];         ///< Leaf nodes (HOTS public key hashes)
} chipmunk_tree_t;

/**
 * @brief Node pair for membership proof path
 * @details Each level of the proof contains left and right node
 */
typedef struct chipmunk_path_node {
    chipmunk_hvc_poly_t left;   ///< Left node polynomial
    chipmunk_hvc_poly_t right;  ///< Right node polynomial
} chipmunk_path_node_t;

/**
 * @brief Membership proof path from leaf to root
 * @details Based on original Rust Path structure
 * Path length = HEIGHT - 1 = 4 nodes (excluding root)
 */
typedef struct chipmunk_path {
    chipmunk_path_node_t nodes[CHIPMUNK_TREE_HEIGHT - 1]; ///< Path nodes from top to bottom
    size_t index;                                          ///< Index of the leaf being proved
} chipmunk_path_t;

/**
 * @brief HVC hasher for tree operations
 * @details Based on original Rust HVCHash with decomposition parameters
 */
typedef struct chipmunk_hvc_hasher {
    chipmunk_hvc_poly_t matrix_a[CHIPMUNK_HVC_WIDTH];  ///< Public matrix for HVC hash
    uint8_t seed[32];                                   ///< Seed for hasher initialization
} chipmunk_hvc_hasher_t;

// =================TREE CONSTRUCTION FUNCTIONS=================

/**
 * @brief Initialize empty tree with default leaf nodes
 * @param[out] a_tree Tree to initialize
 * @param[in] a_hasher HVC hasher for computing internal nodes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_tree_init(chipmunk_tree_t *a_tree, const chipmunk_hvc_hasher_t *a_hasher);

/**
 * @brief Create tree with given leaf nodes (HOTS public keys)
 * @param[out] a_tree Tree to create
 * @param[in] a_leaf_nodes Array of exactly CHIPMUNK_TREE_LEAF_COUNT leaf nodes
 * @param[in] a_hasher HVC hasher for computing internal nodes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 * 
 * Based on Rust Tree::new_with_leaf_nodes()
 */
int chipmunk_tree_new_with_leaf_nodes(chipmunk_tree_t *a_tree, 
                                       const chipmunk_hvc_poly_t a_leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT],
                                       const chipmunk_hvc_hasher_t *a_hasher);

/**
 * @brief Get root of the tree (public key)
 * @param[in] a_tree Tree to get root from
 * @return Pointer to root polynomial, or NULL on error
 */
const chipmunk_hvc_poly_t* chipmunk_tree_root(const chipmunk_tree_t *a_tree);

// =================PROOF FUNCTIONS=================

/**
 * @brief Generate membership proof for leaf at given index
 * @param[in] a_tree Tree to generate proof from
 * @param[in] a_index Index of leaf node (0 to CHIPMUNK_TREE_LEAF_COUNT-1)
 * @param[out] a_path Generated proof path
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 * 
 * Based on Rust Tree::gen_proof()
 */
int chipmunk_tree_gen_proof(const chipmunk_tree_t *a_tree, size_t a_index, chipmunk_path_t *a_path);

/**
 * @brief Verify membership proof against tree root
 * @param[in] a_path Proof path to verify
 * @param[in] a_root Expected root polynomial
 * @param[in] a_hasher HVC hasher for verification
 * @return true if proof is valid, false otherwise
 * 
 * Based on Rust Path::verify()
 */
bool chipmunk_path_verify(const chipmunk_path_t *a_path, 
                          const chipmunk_hvc_poly_t *a_root,
                          const chipmunk_hvc_hasher_t *a_hasher);

// =================HVC HASHER FUNCTIONS=================

/**
 * @brief Initialize HVC hasher with random matrix
 * @param[out] a_hasher Hasher to initialize
 * @param[in] a_seed Random seed for matrix generation
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_hvc_hasher_init(chipmunk_hvc_hasher_t *a_hasher, const uint8_t a_seed[32]);

/**
 * @brief Decompose and hash two polynomials
 * @param[in] a_hasher HVC hasher
 * @param[in] a_left Left polynomial
 * @param[in] a_right Right polynomial  
 * @param[out] a_result Hash result
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 * 
 * Based on Rust HVCHash::decom_then_hash()
 */
int chipmunk_hvc_hash_decom_then_hash(const chipmunk_hvc_hasher_t *a_hasher,
                                       const chipmunk_hvc_poly_t *a_left,
                                       const chipmunk_hvc_poly_t *a_right,
                                       chipmunk_hvc_poly_t *a_result);

// =================UTILITY FUNCTIONS=================

/**
 * @brief Convert HOTS public key to HVC polynomial
 * @param[in] a_hots_pk HOTS public key
 * @param[out] a_hvc_poly HVC polynomial representation
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_hots_pk_to_hvc_poly(const chipmunk_public_key_t *a_hots_pk, 
                                  chipmunk_hvc_poly_t *a_hvc_poly);

/**
 * @brief Clear sensitive data from tree structure
 * @param[in,out] a_tree Tree to clear
 */
void chipmunk_tree_clear(chipmunk_tree_t *a_tree);

/**
 * @brief Clear sensitive data from path structure  
 * @param[in,out] a_path Path to clear
 */
void chipmunk_path_clear(chipmunk_path_t *a_path);

// =================TREE NAVIGATION HELPER FUNCTIONS=================

/**
 * @brief Get index of left child
 * @param a_index Parent index
 * @return Left child index
 */
static inline size_t chipmunk_tree_left_child_index(size_t a_index) {
    return 2 * a_index + 1;
}

/**
 * @brief Get index of right child
 * @param a_index Parent index  
 * @return Right child index
 */
static inline size_t chipmunk_tree_right_child_index(size_t a_index) {
    return 2 * a_index + 2;
}

/**
 * @brief Get index of parent
 * @param a_index Child index
 * @return Parent index, or SIZE_MAX if no parent (root)
 */
static inline size_t chipmunk_tree_parent_index(size_t a_index) {
    return (a_index > 0) ? ((a_index - 1) >> 1) : SIZE_MAX;
}

/**
 * @brief Check if index represents left child
 * @param a_index Node index
 * @return true if left child, false otherwise
 */
static inline bool chipmunk_tree_is_left_child(size_t a_index) {
    return (a_index % 2) == 1;
}

/**
 * @brief Get sibling index
 * @param a_index Node index
 * @return Sibling index, or SIZE_MAX if no sibling (root)
 */
static inline size_t chipmunk_tree_sibling_index(size_t a_index) {
    if (a_index == 0) return SIZE_MAX;
    return chipmunk_tree_is_left_child(a_index) ? (a_index + 1) : (a_index - 1);
}

/**
 * @brief Convert leaf index to tree index
 * @param a_leaf_index Index in leaf array (0 to CHIPMUNK_TREE_LEAF_COUNT-1)
 * @return Index in full tree
 */
static inline size_t chipmunk_tree_leaf_to_tree_index(size_t a_leaf_index) {
    return a_leaf_index + CHIPMUNK_TREE_NON_LEAF_COUNT;
} 