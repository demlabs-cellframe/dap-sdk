#include "chipmunk_aggregation.h"
#include "chipmunk_poly.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "dap_common.h"
#include "dap_hash.h"

#define LOG_TAG "chipmunk_aggregation"

// Если OpenSSL недоступен, определяем константу
#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

// Статические переменные для отладки
static bool s_debug_more = false;

// Вспомогательная функция для редукции коэффициента по модулю q
static inline int32_t chipmunk_poly_reduce_coeff(int32_t coeff) {
    int32_t t = coeff % CHIPMUNK_Q;
    if (t > CHIPMUNK_Q_OVER_TWO) {
        t -= CHIPMUNK_Q;
    } else if (t < -CHIPMUNK_Q_OVER_TWO) {
        t += CHIPMUNK_Q;
    }
    return t;
}

// === Randomizer Functions ===

/**
 * Generate randomizers from public key roots
 * Based on SHA256 hash of concatenated roots
 */
int chipmunk_randomizers_from_pks(const chipmunk_hvc_poly_t *roots, 
                                  size_t count,
                                  chipmunk_randomizers_t *randomizers) {
    if (!roots || !randomizers || count == 0) {
        return -1;
    }

    // Allocate randomizers
    randomizers->randomizers = calloc(count, sizeof(chipmunk_randomizer_t));
    if (!randomizers->randomizers) {
        return -1;
    }
    randomizers->count = count;
    
    // Concatenate all roots into single buffer
    size_t input_size = count * sizeof(chipmunk_hvc_poly_t);
    uint8_t *hash_input = malloc(input_size + sizeof(uint32_t));
    if (!hash_input) {
        free(randomizers->randomizers);
        randomizers->randomizers = NULL;
        return -1;
    }
    
    memcpy(hash_input, roots, input_size);
    
    // Generate randomizers using dap_hash_fast expansion
    for (size_t i = 0; i < count; i++) {
        // Add counter to input for domain separation
        uint32_t counter = (uint32_t)i;
        memcpy(hash_input + input_size, &counter, sizeof(counter));
        
        // Hash to get random bits
        dap_hash_fast_t hash;
        dap_hash_fast(hash_input, input_size + sizeof(uint32_t), &hash);
        
        // Convert hash bits to ternary coefficients
        for (size_t j = 0; j < CHIPMUNK_N && j < DAP_HASH_FAST_SIZE * 4; j++) {
            uint8_t byte_idx = (j * 2) % DAP_HASH_FAST_SIZE;
            uint8_t bit_idx = (j * 2) / DAP_HASH_FAST_SIZE;
            
            // Use 2 bits per coefficient to get 4 values, map to {-1, 0, 1}
            uint8_t bits = (hash.raw[byte_idx] >> (bit_idx * 2)) & 0x03;
            
            switch (bits) {
                case 0: randomizers->randomizers[i].coeffs[j] = 0; break;
                case 1: randomizers->randomizers[i].coeffs[j] = 1; break;
                case 2: randomizers->randomizers[i].coeffs[j] = -1; break;
                case 3: randomizers->randomizers[i].coeffs[j] = 0; break; // Map 3 to 0 for balance
            }
        }
    }

    free(hash_input);
    return 0;
}

/**
 * Generate random randomizers for testing/non-deterministic use
 */
int chipmunk_randomizers_generate_random(size_t count, 
                                         chipmunk_randomizers_t *randomizers) {
    if (!randomizers || count == 0) {
        return -1;
    }

    randomizers->randomizers = calloc(count, sizeof(chipmunk_randomizer_t));
    if (!randomizers->randomizers) {
        return -2;
    }
    randomizers->count = count;

    for (size_t i = 0; i < count; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            // Generate random ternary coefficient
            int rand_val = rand() % 3;
            randomizers->randomizers[i].coeffs[j] = (int8_t)(rand_val - 1); // {-1, 0, 1}
        }
    }

    return 0;
}

/**
 * Free randomizers structure
 */
void chipmunk_randomizers_free(chipmunk_randomizers_t *randomizers) {
    if (randomizers && randomizers->randomizers) {
        free(randomizers->randomizers);
        randomizers->randomizers = NULL;
        randomizers->count = 0;
    }
}

// === HOTS Signature Aggregation ===

/**
 * Randomize a HOTS signature with given randomizer
 */
int chipmunk_hots_sig_randomize(const chipmunk_hots_signature_t *sig,
                                const chipmunk_randomizer_t *randomizer,
                                chipmunk_hots_signature_t *randomized_sig) {
    if (!sig || !randomizer || !randomized_sig) {
        return -1;
    }

    // Copy original signature
    memcpy(randomized_sig, sig, sizeof(chipmunk_hots_signature_t));

    // Randomize each polynomial in the signature
    for (int i = 0; i < CHIPMUNK_W; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            // Multiply signature coefficient by randomizer coefficient
            int32_t coeff = randomized_sig->sigma[i].coeffs[j];
            int8_t rand_coeff = randomizer->coeffs[j];
            
            // Ternary multiplication: coeff * {-1, 0, 1}
            if (rand_coeff == -1) {
                randomized_sig->sigma[i].coeffs[j] = -coeff;
            } else if (rand_coeff == 0) {
                randomized_sig->sigma[i].coeffs[j] = 0;
            }
            // If rand_coeff == 1, coefficient stays the same
            
            // Reduce modulo q
            randomized_sig->sigma[i].coeffs[j] = 
                chipmunk_poly_reduce_coeff(randomized_sig->sigma[i].coeffs[j]);
        }
    }

    return 0;
}

/**
 * Aggregate multiple randomized HOTS signatures
 */
int chipmunk_hots_aggregate_with_randomizers(const chipmunk_hots_signature_t *signatures,
                                             const chipmunk_randomizer_t *randomizers,
                                             size_t count,
                                             chipmunk_aggregated_hots_sig_t *aggregated) {
    if (!signatures || !randomizers || !aggregated || count == 0) {
        return -1;
    }

    // Initialize aggregated signature to zero
    memset(aggregated, 0, sizeof(chipmunk_aggregated_hots_sig_t));
    aggregated->is_randomized = true;

    // Aggregate all randomized signatures
    for (size_t sig_idx = 0; sig_idx < count; sig_idx++) {
        chipmunk_hots_signature_t randomized_sig;
        
        // Randomize current signature
        int ret = chipmunk_hots_sig_randomize(&signatures[sig_idx], 
                                              &randomizers[sig_idx], 
                                              &randomized_sig);
        if (ret != 0) {
            return ret;
        }

        // Add randomized signature to aggregated result
        for (int i = 0; i < CHIPMUNK_W; i++) {
            for (int j = 0; j < CHIPMUNK_N; j++) {
                aggregated->sigma[i].coeffs[j] += randomized_sig.sigma[i].coeffs[j];
                aggregated->sigma[i].coeffs[j] = 
                    chipmunk_poly_reduce_coeff(aggregated->sigma[i].coeffs[j]);
            }
        }
    }

    return 0;
}

// === Multi-Signature Functions ===

/**
 * Create individual signature with Merkle proof
 */
int chipmunk_create_individual_signature(const uint8_t *message,
                                         size_t message_len,
                                         const chipmunk_hots_secret_key_t *secret_key,
                                         const chipmunk_hots_public_key_t *public_key,
                                         const chipmunk_tree_t *tree,
                                         uint32_t leaf_index,
                                         chipmunk_individual_sig_t *individual_sig) {
    if (!message || !secret_key || !public_key || !tree || !individual_sig) {
        return -1;
    }

    // Validate leaf index
    if (leaf_index >= CHIPMUNK_TREE_LEAVES) {
        return -2;
    }

    // Generate HOTS signature
    debug_if(s_debug_more, L_DEBUG, "Generating HOTS signature for leaf index %u", leaf_index);
    int ret = chipmunk_hots_sign((const chipmunk_hots_sk_t*)secret_key, message, message_len, &individual_sig->hots_sig);
    if (ret != 0) {
        return ret;
    }

    // Copy public key
    memcpy(&individual_sig->hots_pk, public_key, sizeof(chipmunk_hots_pk_t));

    // Generate Merkle proof
    ret = chipmunk_tree_gen_proof(tree, leaf_index, &individual_sig->proof);
    if (ret != 0) {
        return ret;
    }

    individual_sig->leaf_index = leaf_index;

    return 0;
}

/**
 * Aggregate multiple individual signatures into multi-signature
 */
int chipmunk_aggregate_signatures(const chipmunk_individual_sig_t *individual_sigs,
                                  size_t count,
                                  const uint8_t *message,
                                  size_t message_len,
                                  chipmunk_multi_signature_t *multi_sig) {
    if (!individual_sigs || !message || !multi_sig || count == 0) {
        return -1;
    }

    // Allocate arrays for multi-signature
    multi_sig->public_key_roots = calloc(count, sizeof(chipmunk_hvc_poly_t));
    multi_sig->proofs = calloc(count, sizeof(chipmunk_path_t));
    multi_sig->leaf_indices = calloc(count, sizeof(uint32_t));
    
    if (!multi_sig->public_key_roots || !multi_sig->proofs || !multi_sig->leaf_indices) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    multi_sig->signer_count = count;

    // Hash the message
    dap_hash_fast_t message_hash;
    dap_hash_fast(message, message_len, &message_hash);
    memcpy(multi_sig->message_hash, message_hash.raw, DAP_HASH_FAST_SIZE);

    // Extract HOTS signatures and create randomizers
    chipmunk_hots_signature_t *hots_sigs = calloc(count, sizeof(chipmunk_hots_signature_t));
    if (!hots_sigs) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    // Collect public key roots for randomizer generation
    for (size_t i = 0; i < count; i++) {
        // Copy HOTS signature
        memcpy(&hots_sigs[i], &individual_sigs[i].hots_sig, sizeof(chipmunk_hots_signature_t));
        
        // Copy proof and leaf index
        memcpy(&multi_sig->proofs[i], &individual_sigs[i].proof, sizeof(chipmunk_path_t));
        multi_sig->leaf_indices[i] = individual_sigs[i].leaf_index;
        
        // Convert HOTS public key to HVC polynomial
        chipmunk_hots_pk_to_hvc_poly((const chipmunk_public_key_t*)&individual_sigs[i].hots_pk, 
                                     &multi_sig->public_key_roots[i]);
    }

    // Generate randomizers from public key roots
    chipmunk_randomizers_t randomizers;
    int ret = chipmunk_randomizers_from_pks(multi_sig->public_key_roots, count, &randomizers);
    if (ret != 0) {
        free(hots_sigs);
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    // Aggregate HOTS signatures with randomizers
    ret = chipmunk_hots_aggregate_with_randomizers(hots_sigs, randomizers.randomizers, 
                                                   count, &multi_sig->aggregated_hots);
    
    // Cleanup
    free(hots_sigs);
    chipmunk_randomizers_free(&randomizers);

    if (ret != 0) {
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    return 0;
}

/**
 * Aggregate multiple individual signatures into multi-signature with tree
 */
int chipmunk_aggregate_signatures_with_tree(const chipmunk_individual_sig_t *individual_sigs,
                                            size_t count,
                                            const uint8_t *message,
                                            size_t message_len,
                                            const chipmunk_tree_t *tree,
                                            chipmunk_multi_signature_t *multi_sig) {
    if (!individual_sigs || !message || !multi_sig || !tree || count == 0) {
        return -1;
    }

    // Allocate arrays for multi-signature
    multi_sig->public_key_roots = calloc(count, sizeof(chipmunk_hvc_poly_t));
    multi_sig->proofs = calloc(count, sizeof(chipmunk_path_t));
    multi_sig->leaf_indices = calloc(count, sizeof(uint32_t));
    
    if (!multi_sig->public_key_roots || !multi_sig->proofs || !multi_sig->leaf_indices) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    multi_sig->signer_count = count;

    // Save tree root
    const chipmunk_hvc_poly_t *tree_root = chipmunk_tree_root(tree);
    if (!tree_root) {
        chipmunk_multi_signature_free(multi_sig);
        return -3;
    }
    memcpy(&multi_sig->tree_root, tree_root, sizeof(chipmunk_hvc_poly_t));

    // Hash the message
    dap_hash_fast_t message_hash;
    dap_hash_fast(message, message_len, &message_hash);
    memcpy(multi_sig->message_hash, message_hash.raw, DAP_HASH_FAST_SIZE);

    // Extract HOTS signatures and create randomizers
    chipmunk_hots_signature_t *hots_sigs = calloc(count, sizeof(chipmunk_hots_signature_t));
    if (!hots_sigs) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    // Collect public key roots for randomizer generation
    for (size_t i = 0; i < count; i++) {
        // Copy HOTS signature
        memcpy(&hots_sigs[i], &individual_sigs[i].hots_sig, sizeof(chipmunk_hots_signature_t));
        
        // Copy proof and leaf index
        memcpy(&multi_sig->proofs[i], &individual_sigs[i].proof, sizeof(chipmunk_path_t));
        multi_sig->leaf_indices[i] = individual_sigs[i].leaf_index;
        
        // Convert HOTS public key to HVC polynomial
        chipmunk_hots_pk_to_hvc_poly((const chipmunk_public_key_t*)&individual_sigs[i].hots_pk, 
                                     &multi_sig->public_key_roots[i]);
    }

    // Generate randomizers from public key roots
    chipmunk_randomizers_t randomizers;
    int ret = chipmunk_randomizers_from_pks(multi_sig->public_key_roots, count, &randomizers);
    if (ret != 0) {
        free(hots_sigs);
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    // Aggregate HOTS signatures with randomizers
    ret = chipmunk_hots_aggregate_with_randomizers(hots_sigs, randomizers.randomizers, 
                                                   count, &multi_sig->aggregated_hots);
    
    // Cleanup
    free(hots_sigs);
    chipmunk_randomizers_free(&randomizers);

    if (ret != 0) {
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    return 0;
}

/**
 * Verify aggregated multi-signature
 */
int chipmunk_verify_multi_signature(const chipmunk_multi_signature_t *multi_sig,
                                    const uint8_t *message,
                                    size_t message_len) {
    if (!multi_sig || !message || multi_sig->signer_count == 0) {
        return -1;
    }

    // Verify message hash
    dap_hash_fast_t computed_hash;
    dap_hash_fast(message, message_len, &computed_hash);
    if (memcmp(computed_hash.raw, multi_sig->message_hash, DAP_HASH_FAST_SIZE) != 0) {
        return 0;  // Message mismatch
    }

    // Create hasher for verification (using same seed as in aggregation)
    chipmunk_hvc_hasher_t hasher;
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int hasher_ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (hasher_ret != 0) {
        return hasher_ret;
    }

    // Verify HOTS public keys against tree root
    for (size_t i = 0; i < multi_sig->signer_count; i++) {
        // Verify proof against the tree root
        bool verify_ret = chipmunk_path_verify(&multi_sig->proofs[i], 
                                              &multi_sig->tree_root,
                                              &hasher);
        if (!verify_ret) {
            return 0;  // Invalid proof
        }
    }

    // Generate randomizers for verification
    chipmunk_randomizers_t randomizers;
    int ret = chipmunk_randomizers_from_pks(multi_sig->public_key_roots, 
                                            multi_sig->signer_count, &randomizers);
    if (ret != 0) {
        return ret;
    }

    // TODO: Implement aggregated signature verification
    // This requires reconstructing the aggregated public key and verifying
    // the aggregated signature against the message
    
    // For now, return success if proofs are valid
    chipmunk_randomizers_free(&randomizers);
    return 1;
}

// === Memory Management ===

/**
 * Free multi-signature structure
 */
void chipmunk_multi_signature_free(chipmunk_multi_signature_t *multi_sig) {
    if (multi_sig) {
        if (multi_sig->public_key_roots) {
            free(multi_sig->public_key_roots);
            multi_sig->public_key_roots = NULL;
        }
        if (multi_sig->proofs) {
            free(multi_sig->proofs);
            multi_sig->proofs = NULL;
        }
        if (multi_sig->leaf_indices) {
            free(multi_sig->leaf_indices);
            multi_sig->leaf_indices = NULL;
        }
        multi_sig->signer_count = 0;
    }
}

/**
 * Free individual signature structure
 */
void chipmunk_individual_signature_free(chipmunk_individual_sig_t *individual_sig) {
    if (individual_sig) {
        // Individual signature doesn't allocate dynamic memory,
        // just clear the structure
        memset(individual_sig, 0, sizeof(chipmunk_individual_sig_t));
    }
}

// === Batch Verification (Placeholder Implementation) ===

/**
 * Initialize batch verification context
 */
int chipmunk_batch_context_init(chipmunk_batch_context_t *context,
                                size_t max_signatures) {
    if (!context || max_signatures == 0) {
        return -1;
    }

    context->signatures = calloc(max_signatures, sizeof(chipmunk_multi_signature_t));
    context->messages = calloc(max_signatures, sizeof(uint8_t*));
    
    if (!context->signatures || !context->messages) {
        chipmunk_batch_context_free(context);
        return -2;
    }

    context->signature_count = 0;
    return 0;
}

/**
 * Add signature to batch verification context
 */
int chipmunk_batch_add_signature(chipmunk_batch_context_t *context,
                                 const chipmunk_multi_signature_t *multi_sig,
                                 const uint8_t *message,
                                 size_t message_len) {
    if (!context || !multi_sig || !message) {
        return -1;
    }

    // Copy signature (shallow copy for now)
    memcpy(&context->signatures[context->signature_count], multi_sig, 
           sizeof(chipmunk_multi_signature_t));
    
    // Store message pointer (caller must ensure message remains valid)
    context->messages[context->signature_count] = (uint8_t*)message;
    
    context->signature_count++;
    return 0;
}

/**
 * Perform batch verification of all signatures in context
 */
int chipmunk_batch_verify(const chipmunk_batch_context_t *context) {
    if (!context || context->signature_count == 0) {
        return -1;
    }

    // For now, verify each signature individually
    for (size_t i = 0; i < context->signature_count; i++) {
        int ret = chipmunk_verify_multi_signature(&context->signatures[i],
                                                  context->messages[i],
                                                  strlen((char*)context->messages[i]));
        if (ret != 1) {
            return 0; // At least one signature is invalid
        }
    }

    return 1; // All signatures valid
}

/**
 * Free batch verification context
 */
void chipmunk_batch_context_free(chipmunk_batch_context_t *context) {
    if (context) {
        if (context->signatures) {
            free(context->signatures);
            context->signatures = NULL;
        }
        if (context->messages) {
            free(context->messages);
            context->messages = NULL;
        }
        context->signature_count = 0;
    }
} 