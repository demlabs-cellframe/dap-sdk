/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "dap_common.h"
#include "dap_enc_key.h"
#include "chipmunk.h"
#include "dap_enc_chipmunk_ring.h"

/**
 * @brief Chipmunk_Ring ring signature parameters
 */
#define CHIPMUNK_RING_MAX_RING_SIZE 1024
// Note: ZK proof parameters moved to dap_enc_chipmunk_ring_params.h
// Note: CHIPMUNK_RING_SIGNATURE_SIZE is now defined in dap_enc_chipmunk_ring_params.h
// with parametric form: CHIPMUNK_RING_SIGNATURE_SIZE(chipmunk_n, chipmunk_gamma)


/**
 * @brief Chipmunk_Ring public key structure
 */
typedef struct chipmunk_ring_public_key {
    uint8_t data[CHIPMUNK_PUBLIC_KEY_SIZE];  ///< Public key data from Chipmunk
} chipmunk_ring_public_key_t;

/**
 * @brief Chipmunk_Ring private key structure
 */
typedef struct chipmunk_ring_private_key {
    uint8_t data[CHIPMUNK_PRIVATE_KEY_SIZE];  ///< Private key data from Chipmunk
} chipmunk_ring_private_key_t;

/**
 * @brief Ring container for public keys
 */
typedef struct chipmunk_ring_container {
    uint32_t size;                                      ///< Number of keys in ring
    chipmunk_ring_public_key_t *public_keys;             ///< Array of public keys
    uint8_t ring_hash[32];                              ///< Hash of all public keys for verification
} chipmunk_ring_container_t;

/**
 * @brief Quantum-resistant commitment for ZKP (Zero-Knowledge Proof)
 */
typedef struct chipmunk_ring_commitment {
    // Dynamic randomness (configurable size)
    uint8_t *randomness;             ///< Randomness used in commitment (dynamic size)
    size_t randomness_size;          ///< Size of randomness in bytes

    // Quantum-resistant commitment layers (dynamic sizes based on parameters)
    uint8_t *ring_lwe_layer;         ///< Ring-LWE commitment (~90,000 qubits required)
    uint8_t *ntru_layer;             ///< NTRU commitment (~70,000 qubits required)
    uint8_t *code_layer;             ///< Code commitment (~80,000 qubits required, strengthened)
    uint8_t *binding_proof;          ///< Binding proof 

    // Layer sizes for memory management
    size_t ring_lwe_size;
    size_t ntru_size;
    size_t code_size;
    size_t binding_proof_size;
} chipmunk_ring_commitment_t;

/**
 * @brief Response for ZKP and threshold coordination
 */
typedef struct chipmunk_ring_response {
    uint8_t *value;                         ///< Response value (dynamic size)
    size_t value_size;                      ///< Size of response value
} chipmunk_ring_response_t;

/**
 * @brief Chipmunk_Ring signature structure (now supports threshold)
 * @details Unified structure for both traditional ring (t=1) and threshold ring (t>1)
 */
typedef struct chipmunk_ring_signature {
    uint32_t ring_size;                                ///< Number of participants in ring
    uint32_t required_signers;                         ///< Required signers (1 = single, >1 = multi-signer)
    // REMOVED: uint32_t signer_index - breaks anonymity!
    
    // ZK Components (needed for threshold coordination and ZK schemes)
    uint8_t challenge[32];                             ///< Fiat-Shamir challenge (needed for ZK)
    chipmunk_ring_commitment_t *commitments;           ///< ZK commitments (needed for threshold coordination)
    chipmunk_ring_response_t *responses;               ///< ZK responses (needed for threshold reconstruction)
    
    // Core signature
    uint8_t *chipmunk_signature;                       ///< Real Chipmunk signature (dynamic size)
    size_t chipmunk_signature_size;                    ///< Size of Chipmunk signature
    
    // Ring public keys storage (scalability optimization)
    bool use_embedded_keys;                            ///< True = keys in signature, False = external storage
    chipmunk_ring_public_key_t *ring_public_keys;      ///< Embedded public keys (NULL if external)
    uint8_t *ring_key_hashes;                          ///< Hashes of public keys (for external storage)
    size_t ring_key_hashes_size;                       ///< Size of key hashes array
    uint8_t ring_hash[32];                             ///< Hash of all ring public keys
    
    // Multi-signer extensions (only used when required_signers > 1)
    uint8_t *threshold_zk_proofs;                      ///< ZK proofs from participating signers (dynamic)
    size_t zk_proofs_size;                             ///< Total size of all ZK proofs data
    uint32_t *zk_proof_lengths;                        ///< Length of each individual ZK proof (dynamic array)
    uint32_t participating_count;                      ///< Actual number of participants who signed
    
    // Participating signer identification (for multi-signer verification)
    uint8_t *participating_key_hashes;                 ///< Hashes of participating signers' keys
    size_t participating_hashes_size;                  ///< Size of participating hashes array
    
    // Coordination state (for threshold schemes)
    bool is_coordinated;                               ///< True if threshold coordination completed
    uint32_t coordination_round;                       ///< Current coordination round (0=commit, 1=reveal, 2=aggregate)
    uint32_t zk_proof_size_per_participant;            ///< Configurable ZK proof size (default: 64)
    uint32_t zk_iterations;                            ///< Number of SHAKE-256 iterations for ZK proofs
    
    // Linkability control (configurable for anonymity vs double-spend protection)
    uint8_t linkability_mode;                          ///< Linkability mode (0=disabled, 1=message-only, 2=full)
    uint8_t linkability_tag[32];                       ///< Optional linkability tag (empty if disabled)
} chipmunk_ring_signature_t;

/**
 * @brief Initialize Chipmunk_Ring module
 * @return 0 on success, negative on error
 */
int chipmunk_ring_init(void);

/**
 * @brief Generate Chipmunk_Ring keypair (same as Chipmunk)
 * @param key Output key structure
 * @return 0 on success, negative on error
 */
int chipmunk_ring_key_new(struct dap_enc_key *a_key);

/**
 * @brief Generate keypair from seed
 * @param key Output key structure
 * @param seed Seed for deterministic generation
 * @param seed_size Seed size
 * @param key_size Key size (unused, kept for compatibility)
 * @return 0 on success, negative on error
 */
int chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size);

/**
 * @brief Create ring from public keys
 * @param public_keys Array of public keys
 * @param num_keys Number of keys in ring
 * @param ring Output ring structure
 * @return 0 on success, negative on error
 */
int chipmunk_ring_container_create(const chipmunk_ring_public_key_t *a_public_keys,
                           size_t a_num_keys, chipmunk_ring_container_t *a_ring);

/**
 * @brief Free ring container resources
 * @param ring Ring container to free
 */
void chipmunk_ring_container_free(chipmunk_ring_container_t *a_ring);

/**
 * @brief Create ChipmunkRing signature with scalability support
 * @param private_key Signer's private key
 * @param message Message to sign
 * @param message_size Message size
 * @param ring Ring containing public keys
 * @param a_required_signers Required signers (1 = traditional ring, >1 = multi-signer)
 * @param a_use_embedded_keys True = embed keys in signature, False = use external storage
 * @param signature Output signature
 * @return 0 on success, negative on error
 */
int chipmunk_ring_sign(const chipmunk_ring_private_key_t *a_private_key,
                     const void *a_message, size_t a_message_size,
                     const chipmunk_ring_container_t *a_ring,
                     uint32_t a_required_signers,
                     bool a_use_embedded_keys,
                     chipmunk_ring_signature_t *a_signature);


/**
 * @brief Verify ChipmunkRing signature with scalability support
 * @param message Signed message
 * @param message_size Message size
 * @param signature Signature to verify
 * @param ring Ring containing public keys (NULL if using embedded keys)
 * @return 0 if valid, negative on error or invalid signature
 */
int chipmunk_ring_verify(const void *a_message, size_t a_message_size,
                       const chipmunk_ring_signature_t *a_signature,
                       const chipmunk_ring_container_t *a_ring);

/**
 * @brief Verify ChipmunkRing signature with external key resolution
 * @details For large rings where keys are stored externally (blockchain/database)
 * 
 * @param message Signed message
 * @param message_size Message size
 * @param signature Signature to verify (contains key hashes)
 * @param key_resolver Function to resolve public key from hash
 * @param resolver_context Context for key resolver function
 * @return 0 if valid, negative on error
 */
typedef int (*chipmunk_ring_key_resolver_t)(const uint8_t *key_hash, 
                                           chipmunk_ring_public_key_t *public_key, 
                                           void *context);

int chipmunk_ring_verify_external(const void *a_message, size_t a_message_size,
                                 const chipmunk_ring_signature_t *a_signature,
                                 chipmunk_ring_key_resolver_t a_key_resolver,
                                 void *a_resolver_context);

/**
 * @brief Get signature size for given ring size
 * @param ring_size Number of participants
 * @return Required signature buffer size
 */
size_t chipmunk_ring_get_signature_size(size_t a_ring_size);

/**
 * @brief Delete Chipmunk_Ring key
 * @param key Key to delete
 */
void chipmunk_ring_key_delete(struct dap_enc_key *a_key);


/**
 * @brief Create response for ZKP
 * @param response Output response
 * @param commitment Commitment used
 * @param challenge Challenge value
 * @param private_key Private key (NULL for dummy participants)
 * @return 0 on success, negative on error
 */
/**
 * @brief Create response for ZKP and threshold coordination
 * @details Uses signature parameters for adaptive response size (signature can be NULL for defaults)
 */
int chipmunk_ring_response_create(chipmunk_ring_response_t *a_response,
                               const chipmunk_ring_commitment_t *a_commitment,
                               const uint8_t a_challenge[32],
                               const chipmunk_ring_private_key_t *a_private_key,
                               const chipmunk_ring_signature_t *a_signature);

/**
 * @brief Free signature resources
 * @param signature Signature to free
 */
void chipmunk_ring_signature_free(chipmunk_ring_signature_t *a_signature);

/**
 * @brief Serialize signature to bytes
 * @param sig Signature structure
 * @param output Output buffer
 * @param output_size Output buffer size
 * @return 0 on success, negative on error
 */
int chipmunk_ring_signature_to_bytes(const chipmunk_ring_signature_t *a_sig,
                                   uint8_t *a_output, size_t a_output_size);

/**
 * @brief Deserialize signature from bytes
 * @param sig Output signature structure
 * @param input Input buffer
 * @param input_size Input buffer size
 * @return 0 on success, negative on error
 */
int chipmunk_ring_signature_from_bytes(chipmunk_ring_signature_t *a_sig,
                                     const uint8_t *a_input, size_t a_input_size);

/**
 * @brief Get current post-quantum parameters
 */
int chipmunk_ring_get_params(chipmunk_ring_pq_params_t *params);

/**
 * @brief Set post-quantum parameters
 */
int chipmunk_ring_set_params(const chipmunk_ring_pq_params_t *params);

/**
 * @brief Get current layer sizes
 */
void chipmunk_ring_get_layer_sizes(size_t *ring_lwe_size, size_t *ntru_size,
                                  size_t *code_size, size_t *binding_proof_size);

/**
 * @brief Reset parameters to defaults
 */
int chipmunk_ring_reset_params(void);
