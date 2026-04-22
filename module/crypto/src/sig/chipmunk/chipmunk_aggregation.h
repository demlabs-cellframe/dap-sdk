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

// Aggregated HOTS signature structure.
//
// CR-D6/D7 fix (Round-4): the aggregated signature preserves the full GAMMA
// polynomial dimension of the underlying HOTS signatures.  The earlier
// definition used CHIPMUNK_W (= CHIPMUNK_WIDTH = 4) instead of CHIPMUNK_GAMMA
// (= 6), which dropped two coefficient vectors and made the aggregate verify
// equation underdetermined; a buffer-overflow also occurred in every call site
// that copied `sizeof(chipmunk_signature_t::sigma)` (GAMMA × N × 4 bytes) into
// `aggregated_hots.sigma` sized for W × N × 4 bytes.
typedef struct {
    chipmunk_poly_t sigma[CHIPMUNK_GAMMA];  // Aggregated signature polynomials (time domain)
    bool is_randomized;                 // Flag indicating if signature is randomized
} chipmunk_aggregated_hots_sig_t;

// Individual signature with proof for aggregation.
//
// CR-D6/D7 (Round-4): `rho_seed` is the per-signer seed used to re-derive the
// public HOTS matrix A via dap_chipmunk_hash_sample_matrix().  Without it the
// verifier cannot recompute A and therefore cannot check the aggregate HOTS
// identity; every individual signer contributes their own A, so a single
// global copy is NOT acceptable for true multi-signer aggregation.
typedef struct {
    chipmunk_hots_signature_t hots_sig;     // HOTS signature
    chipmunk_hots_public_key_t hots_pk;     // HOTS public key (v0, v1)
    uint8_t rho_seed[32];                   // Per-signer A-matrix seed
    chipmunk_path_t proof;                  // Merkle tree proof
    uint32_t leaf_index;                    // Index in the tree
} chipmunk_individual_sig_t;

// Aggregated multi-signature structure.
//
// CR-D6/D7 fix (Round-4): the verifier needs access to each signer's full
// HOTS public key (v0_j, v1_j) in order to reconstruct the aggregated right-
// hand side `H(m)·Σ_j r_j·v0_j + Σ_j r_j·v1_j` of the HOTS identity.  The
// earlier representation stored only a lossy HVC-hash `public_key_roots[j]`
// (which is a SHA3-derived small-q polynomial and cannot be inverted into the
// large-q HOTS public key), so the verifier was forced to substitute bogus
// values and fall back to a ±10 % slack tolerance check — trivially forgeable.
// We retain `public_key_roots` for tree-leaf and randomizer-derivation
// compatibility; it is now always recomputed from `hots_pks` on the verify
// path and checked for equality with the stored copy to pin the link between
// the Merkle tree and the full HOTS PKs.
typedef struct chipmunk_multi_signature {
    chipmunk_aggregated_hots_sig_t aggregated_hots;  // Aggregated HOTS signatures
    chipmunk_hvc_poly_t tree_root;                   // Root of the Merkle tree
    chipmunk_hvc_poly_t *public_key_roots;           // HVC polynomials for each signer's public key (tree leaves)
    chipmunk_hots_public_key_t *hots_pks;            // Full HOTS public keys per signer (for aggregate verify)
    uint8_t (*rho_seeds)[32];                        // Per-signer A-matrix seeds (CR-D6/D7)
    chipmunk_path_t *proofs;                         // Merkle proofs for each signer
    uint32_t *leaf_indices;                          // Leaf indices for each signer
    size_t signer_count;                             // Number of signers
    uint8_t message_hash[32];                        // Hash of the signed message

    // CR-D15.A (Round-5): seed used to derive the HVC hasher matrix A that
    // built the Merkle tree.  Signers and the verifier must agree on the
    // same matrix, otherwise path verification is meaningless (previously
    // the verifier ran the tree with a hard-coded {1,2,...,32} seed, which
    // was vacuously "correct" only because every signer was using the same
    // rigged hasher).  The aggregator fills this field from the caller-
    // supplied hasher when the multi-signature is built; verifiers rebuild
    // the hasher locally from the same 32-byte seed.
    uint8_t hvc_hasher_seed[32];
} chipmunk_multi_signature_t;

// Batch verification context for multiple aggregated signatures.
// NOTE: messages_lengths is REQUIRED — messages are arbitrary binary buffers,
// NOT NUL-terminated C strings, so strlen() must never be used on them.
typedef struct {
    chipmunk_multi_signature_t *signatures;
    const uint8_t **messages;
    size_t *message_lengths;
    size_t signature_count;
    size_t capacity;
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
                                         const uint8_t a_rho_seed[32],
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