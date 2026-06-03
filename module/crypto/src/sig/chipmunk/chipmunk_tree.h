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
 * @brief Merkle Tree implementation for Chipmunk Multi-Signature scheme with large-scale support
 * 
 * Based on original Rust implementation from Chipmunk paper:
 * "Chipmunk: Better Synchronized Multi-Signatures from Lattices"
 * 
 * Scalable tree structure:
 * - Configurable HEIGHT (5 to 16 levels)
 * - Dynamic allocation for leaf count from 16 to 32,768 participants
 * - Memory-efficient design for large multi-signatures
 * - Level-order storage: root at index 0
 */

// Tree parameters - now configurable for large scale
#define CHIPMUNK_TREE_HEIGHT_MIN     5                           ///< Minimum height (16 participants)
#define CHIPMUNK_TREE_HEIGHT_MAX     16                          ///< Maximum height (32,768 participants)
#define CHIPMUNK_TREE_HEIGHT_DEFAULT 5                           ///< Default height for compatibility

// Macros for calculating tree sizes based on height
#define CHIPMUNK_TREE_LEAF_COUNT(height)     (1UL << ((height) - 1))         ///< Number of leaves = 2^(HEIGHT-1)
#define CHIPMUNK_TREE_NON_LEAF_COUNT(height) (CHIPMUNK_TREE_LEAF_COUNT(height) - 1)  ///< Number of non-leaf nodes

// Default sizes for backward compatibility
#define CHIPMUNK_TREE_LEAF_COUNT_DEFAULT     CHIPMUNK_TREE_LEAF_COUNT(CHIPMUNK_TREE_HEIGHT_DEFAULT)     ///< 16 leaves
#define CHIPMUNK_TREE_NON_LEAF_COUNT_DEFAULT CHIPMUNK_TREE_NON_LEAF_COUNT(CHIPMUNK_TREE_HEIGHT_DEFAULT) ///< 15 non-leaf nodes

// Large-scale constants
#define CHIPMUNK_TREE_MAX_PARTICIPANTS       32768               ///< Maximum participants (2^15)
#define CHIPMUNK_TREE_TARGET_PARTICIPANTS    30000               ///< Target scale for blockchain applications

/**
 * @brief HVC polynomial structure for tree nodes
 * @details Uses smaller ring for efficient tree operations
 * Based on original Rust HVCPoly with HVC_MODULUS = 202753
 */
typedef struct chipmunk_hvc_poly {
    int32_t coeffs[CHIPMUNK_N];  ///< Coefficients in HVC ring Z_q[X]/(X^N + 1)
} chipmunk_hvc_poly_t;

/**
 * @brief Scalable Merkle tree structure for organizing HOTS public keys
 * @details Dynamic allocation based on participant count
 * Stores tree in level-order: root at index 0, children at 2*i+1, 2*i+2
 * Follows original Rust Tree implementation with scalability extensions
 */
typedef struct chipmunk_tree {
    uint32_t height;                            ///< Tree height (5 to 16)
    size_t leaf_count;                          ///< Number of leaf nodes
    size_t non_leaf_count;                      ///< Number of non-leaf nodes
    chipmunk_hvc_poly_t *non_leaf_nodes;        ///< Non-leaf nodes in level order (dynamically allocated)
    chipmunk_hvc_poly_t *leaf_nodes;            ///< Leaf nodes (HOTS public key hashes, dynamically allocated)
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
 * @brief Scalable membership proof path from leaf to root
 * @details Based on original Rust Path structure with dynamic sizing
 * Path length = HEIGHT - 1 nodes (excluding root)
 */
typedef struct chipmunk_path {
    chipmunk_path_node_t *nodes;    ///< Path nodes from top to bottom (dynamically allocated)
    size_t path_length;             ///< Length of path (height - 1)
    size_t index;                   ///< Index of the leaf being proved
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
 * @brief Initialize empty tree with default height and leaf nodes
 * @param[out] a_tree Tree to initialize
 * @param[in] a_hasher HVC hasher for computing internal nodes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_tree_init(chipmunk_tree_t *a_tree, const chipmunk_hvc_hasher_t *a_hasher);

/**
 * @brief Initialize tree with specific participant count
 * @param[out] a_tree Tree to initialize
 * @param[in] a_participant_count Number of participants (up to CHIPMUNK_TREE_MAX_PARTICIPANTS)
 * @param[in] a_hasher HVC hasher for computing internal nodes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_tree_init_with_size(chipmunk_tree_t *a_tree, 
                                  size_t a_participant_count,
                                  const chipmunk_hvc_hasher_t *a_hasher);

/**
 * @brief Create tree with given leaf nodes (HOTS public keys)
 * @param[out] a_tree Tree to create
 * @param[in] a_leaf_nodes Array of leaf nodes
 * @param[in] a_leaf_count Number of leaf nodes (must be power of 2)
 * @param[in] a_hasher HVC hasher for computing internal nodes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 * 
 * Based on Rust Tree::new_with_leaf_nodes() with scalability extensions
 */
int chipmunk_tree_new_with_leaf_nodes(chipmunk_tree_t *a_tree, 
                                       const chipmunk_hvc_poly_t *a_leaf_nodes,
                                       size_t a_leaf_count,
                                       const chipmunk_hvc_hasher_t *a_hasher);

/**
 * @brief Get root of the tree (public key)
 * @param[in] a_tree Tree to get root from
 * @return Pointer to root polynomial, or NULL on error
 */
const chipmunk_hvc_poly_t* chipmunk_tree_root(const chipmunk_tree_t *a_tree);

/**
 * @brief Get tree statistics for monitoring large-scale operations
 * @param[in] a_tree Tree to analyze
 * @param[out] a_height Tree height
 * @param[out] a_leaf_count Number of leaves
 * @param[out] a_memory_usage Estimated memory usage in bytes
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_tree_get_stats(const chipmunk_tree_t *a_tree,
                             uint32_t *a_height,
                             size_t *a_leaf_count, 
                             size_t *a_memory_usage);

// =================PROOF FUNCTIONS=================

/**
 * @brief Generate membership proof for leaf at given index
 * @param[in] a_tree Tree to generate proof from
 * @param[in] a_index Index of leaf node (0 to leaf_count-1)
 * @param[out] a_path Generated proof path (caller must free with chipmunk_path_free)
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

// =================MEMORY MANAGEMENT=================

/**
 * @brief Free tree resources
 * @param[in,out] a_tree Tree to free
 */
void chipmunk_tree_free(chipmunk_tree_t *a_tree);

/**
 * @brief Free path resources
 * @param[in,out] a_path Path to free
 */
void chipmunk_path_free(chipmunk_path_t *a_path);

/**
 * @brief Clear sensitive data from tree structure (but keep allocation)
 * @param[in,out] a_tree Tree to clear
 */
void chipmunk_tree_clear(chipmunk_tree_t *a_tree);

/**
 * @brief Clear sensitive data from path structure (but keep allocation)
 * @param[in,out] a_path Path to clear
 */
void chipmunk_path_clear(chipmunk_path_t *a_path);

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
 * @brief Calculate required tree height for given participant count
 * @param[in] a_participant_count Number of participants
 * @return Required tree height, or 0 if too many participants
 */
uint32_t chipmunk_tree_calculate_height(size_t a_participant_count);

/**
 * @brief Check if participant count is valid (power of 2, within limits)
 * @param[in] a_participant_count Number of participants to validate
 * @return true if valid, false otherwise
 */
bool chipmunk_tree_validate_participant_count(size_t a_participant_count);

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
 * @brief Get index of sibling node
 * @param a_index Node index
 * @return Sibling index, or SIZE_MAX if no sibling (root)
 */
static inline size_t chipmunk_tree_sibling_index(size_t a_index) {
    if (a_index == 0) return SIZE_MAX;
    return chipmunk_tree_is_left_child(a_index) ? (a_index + 1) : (a_index - 1);
}

/**
 * @brief Convert leaf index to tree node index
 * @param a_leaf_index Leaf index (0-based)
 * @param a_tree_height Tree height
 * @return Tree node index in level-order storage
 */
static inline size_t chipmunk_tree_leaf_to_tree_index(size_t a_leaf_index, uint32_t a_tree_height) {
    return a_leaf_index + CHIPMUNK_TREE_LEAF_COUNT(a_tree_height) - 1;
} 