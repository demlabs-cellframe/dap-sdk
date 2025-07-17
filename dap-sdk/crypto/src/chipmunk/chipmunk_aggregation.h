#ifndef DAP_CHIPMUNK_AGGREGATION_H
#define DAP_CHIPMUNK_AGGREGATION_H

#include "chipmunk_hots.h"
#include "chipmunk_tree.h"
#include <stdint.h>
#include <stdbool.h>

// Добавляем недостающие определения
#define CHIPMUNK_W CHIPMUNK_WIDTH               // Use WIDTH from chipmunk.h
#define CHIPMUNK_TREE_LEAVES CHIPMUNK_TREE_LEAF_COUNT_DEFAULT  // Use default leaf count

// Типы для совместимости (используем существующие типы из chipmunk_hots.h)
typedef chipmunk_hots_sk_t chipmunk_hots_secret_key_t;
typedef chipmunk_hots_pk_t chipmunk_hots_public_key_t;

#ifdef __cplusplus
extern "C" {
#endif

// Randomizer polynomial for signature aggregation
// Uses ternary coefficients {-1, 0, 1} for efficient operations
typedef struct {
    int8_t coeffs[CHIPMUNK_N];  // Ternary coefficients: -1, 0, 1
} chipmunk_randomizer_t;

// Collection of randomizers for multiple signatures
typedef struct {
    chipmunk_randomizer_t *randomizers;
    size_t count;
} chipmunk_randomizers_t;

// Aggregated HOTS signature structure
typedef struct {
    chipmunk_poly_t sigma[CHIPMUNK_W];  // Aggregated signature polynomials
    bool is_randomized;                 // Flag indicating if signature is randomized
} chipmunk_aggregated_hots_sig_t;

// Individual signature with proof for aggregation
typedef struct {
    chipmunk_hots_signature_t hots_sig;     // HOTS signature
    chipmunk_hots_public_key_t hots_pk;     // HOTS public key
    chipmunk_path_t proof;                  // Merkle tree proof
    uint32_t leaf_index;                    // Index in the tree
} chipmunk_individual_sig_t;

// Aggregated multi-signature structure
typedef struct {
    chipmunk_aggregated_hots_sig_t aggregated_hots;  // Aggregated HOTS signatures
    chipmunk_hvc_poly_t tree_root;                   // Root of the Merkle tree
    chipmunk_hvc_poly_t *public_key_roots;           // HVC polynomials for each signer's public key
    chipmunk_path_t *proofs;                         // Merkle proofs for each signer
    uint32_t *leaf_indices;                          // Leaf indices for each signer
    size_t signer_count;                             // Number of signers
    uint8_t message_hash[32];                        // Hash of the signed message
} chipmunk_multi_signature_t;

// Batch verification context for multiple aggregated signatures
typedef struct {
    chipmunk_multi_signature_t *signatures;
    uint8_t **messages;
    size_t signature_count;
} chipmunk_batch_context_t;

// === Randomizer Functions ===

/**
 * Generate randomizers from public key roots using deterministic method
 * @param roots Array of HVC polynomial roots (tree roots)
 * @param count Number of roots
 * @param randomizers Output randomizers structure
 * @return 0 on success, negative on error
 */
int chipmunk_randomizers_from_pks(const chipmunk_hvc_poly_t *roots, 
                                  size_t count,
                                  chipmunk_randomizers_t *randomizers);

/**
 * Generate random randomizers for testing/non-deterministic use
 * @param count Number of randomizers to generate
 * @param randomizers Output randomizers structure
 * @return 0 on success, negative on error
 */
int chipmunk_randomizers_generate_random(size_t count, 
                                         chipmunk_randomizers_t *randomizers);

/**
 * Free randomizers structure
 * @param randomizers Randomizers to free
 */
void chipmunk_randomizers_free(chipmunk_randomizers_t *randomizers);

// === HOTS Signature Aggregation ===

/**
 * Randomize a HOTS signature with given randomizer
 * @param sig Input HOTS signature
 * @param randomizer Randomizer polynomial
 * @param randomized_sig Output randomized signature
 * @return 0 on success, negative on error
 */
int chipmunk_hots_sig_randomize(const chipmunk_hots_signature_t *sig,
                                const chipmunk_randomizer_t *randomizer,
                                chipmunk_hots_signature_t *randomized_sig);

/**
 * Aggregate multiple randomized HOTS signatures
 * @param signatures Array of HOTS signatures
 * @param randomizers Array of randomizers
 * @param count Number of signatures
 * @param aggregated Output aggregated signature
 * @return 0 on success, negative on error
 */
int chipmunk_hots_aggregate_with_randomizers(const chipmunk_hots_signature_t *signatures,
                                             const chipmunk_randomizer_t *randomizers,
                                             size_t count,
                                             chipmunk_aggregated_hots_sig_t *aggregated);

// === Multi-Signature Functions ===

/**
 * Create individual signature with Merkle proof
 * @param message Message to sign
 * @param message_len Length of message
 * @param secret_key HOTS secret key
 * @param public_key HOTS public key
 * @param tree Merkle tree containing the public key
 * @param leaf_index Index of the public key in the tree
 * @param individual_sig Output individual signature
 * @return 0 on success, negative on error
 */
int chipmunk_create_individual_signature(const uint8_t *message,
                                         size_t message_len,
                                         const chipmunk_hots_secret_key_t *secret_key,
                                         const chipmunk_hots_public_key_t *public_key,
                                         const chipmunk_tree_t *tree,
                                         uint32_t leaf_index,
                                         chipmunk_individual_sig_t *individual_sig);

/**
 * Aggregate multiple individual signatures into multi-signature
 * @param individual_sigs Array of individual signatures
 * @param count Number of signatures
 * @param message Message that was signed
 * @param message_len Length of message
 * @param multi_sig Output multi-signature
 * @return 0 on success, negative on error
 */
int chipmunk_aggregate_signatures(const chipmunk_individual_sig_t *individual_sigs,
                                  size_t count,
                                  const uint8_t *message,
                                  size_t message_len,
                                  chipmunk_multi_signature_t *multi_sig);

/**
 * Aggregate multiple individual signatures into multi-signature with tree
 * @param individual_sigs Array of individual signatures
 * @param count Number of signatures
 * @param message Message that was signed
 * @param message_len Length of message
 * @param tree The Merkle tree containing all signers
 * @param multi_sig Output multi-signature
 * @return 0 on success, negative on error
 */
int chipmunk_aggregate_signatures_with_tree(const chipmunk_individual_sig_t *individual_sigs,
                                            size_t count,
                                            const uint8_t *message,
                                            size_t message_len,
                                            const chipmunk_tree_t *tree,
                                            chipmunk_multi_signature_t *multi_sig);

/**
 * Verify aggregated multi-signature
 * @param multi_sig Multi-signature to verify
 * @param message Original message
 * @param message_len Length of message
 * @return 1 if valid, 0 if invalid, negative on error
 */
int chipmunk_verify_multi_signature(const chipmunk_multi_signature_t *multi_sig,
                                    const uint8_t *message,
                                    size_t message_len);

// === Batch Verification ===

/**
 * Initialize batch verification context
 * @param context Batch context to initialize
 * @param max_signatures Maximum number of signatures in batch
 * @return 0 on success, negative on error
 */
int chipmunk_batch_context_init(chipmunk_batch_context_t *context,
                                size_t max_signatures);

/**
 * Add signature to batch verification context
 * @param context Batch context
 * @param multi_sig Multi-signature to add
 * @param message Message for this signature
 * @param message_len Length of message
 * @return 0 on success, negative on error
 */
int chipmunk_batch_add_signature(chipmunk_batch_context_t *context,
                                 const chipmunk_multi_signature_t *multi_sig,
                                 const uint8_t *message,
                                 size_t message_len);

/**
 * Perform batch verification of all signatures in context
 * @param context Batch context with signatures
 * @return 1 if all valid, 0 if any invalid, negative on error
 */
int chipmunk_batch_verify(const chipmunk_batch_context_t *context);

/**
 * Free batch verification context
 * @param context Batch context to free
 */
void chipmunk_batch_context_free(chipmunk_batch_context_t *context);

// === Memory Management ===

/**
 * Free multi-signature structure
 * @param multi_sig Multi-signature to free
 */
void chipmunk_multi_signature_free(chipmunk_multi_signature_t *multi_sig);

/**
 * Free individual signature structure
 * @param individual_sig Individual signature to free
 */
void chipmunk_individual_signature_free(chipmunk_individual_sig_t *individual_sig);

#ifdef __cplusplus
}
#endif

#endif // DAP_CHIPMUNK_AGGREGATION_H 