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

/**
 * @brief Chipmunk_Ring ring signature parameters
 */
#define CHIPMUNK_RING_MAX_RING_SIZE 1024
#define CHIPMUNK_RING_SIGNATURE_SIZE (sizeof(uint32_t) + CHIPMUNK_SIGNATURE_SIZE + CHIPMUNK_RING_MAX_RING_SIZE * 32)


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
    uint8_t randomness[32];                                    ///< Randomness used in commitment
    
    // Post-quantum commitment layers (60,000-90,000 logical qubits required for attack)
    uint8_t ring_lwe_layer[128];     ///< Ring-LWE commitment (~90,000 qubits)
    uint8_t ntru_layer[64];          ///< NTRU commitment (~70,000 qubits)
    uint8_t hash_layer[128];         ///< Hash commitment (~512 qubits, vulnerable ~2030)
    uint8_t code_layer[64];          ///< Code commitment (~60,000 qubits)
    uint8_t binding_proof[128];      ///< Multi-layer binding proof
} chipmunk_ring_commitment_t;

/**
 * @brief Response for ZKP
 */
typedef struct chipmunk_ring_response {
    uint8_t value[32];                      ///< Response value
} chipmunk_ring_response_t;

/**
 * @brief Chipmunk_Ring signature structure
 */
typedef struct chipmunk_ring_signature {
    uint32_t ring_size;                                ///< Number of participants in ring
    uint32_t signer_index;                             ///< Index of actual signer (for verification)
    uint8_t linkability_tag[32];                       ///< H(PK_signer) for optional linkability
    uint8_t challenge[32];                             ///< Fiat-Shamir challenge
    chipmunk_ring_commitment_t *commitments;            ///< Commitments for each participant
    chipmunk_ring_response_t *responses;                ///< Responses for each participant
    uint8_t chipmunk_signature[CHIPMUNK_SIGNATURE_SIZE]; ///< Real Chipmunk signature
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
 * @brief Create Chipmunk_Ring signature
 * @param private_key Signer's private key
 * @param message Message to sign
 * @param message_size Message size
 * @param ring Ring containing public keys
 * @param signer_index Index of signer in ring
 * @param signature Output signature
 * @return 0 on success, negative on error
 */
int chipmunk_ring_sign(const chipmunk_ring_private_key_t *a_private_key,
                     const void *a_message, size_t a_message_size,
                     const chipmunk_ring_container_t *a_ring, uint32_t a_signer_index,
                     chipmunk_ring_signature_t *a_signature);

/**
 * @brief Verify Chipmunk_Ring signature
 * @param message Signed message
 * @param message_size Message size
 * @param signature Signature to verify
 * @param ring Ring containing public keys
 * @return 0 if valid, negative on error or invalid signature
 */
int chipmunk_ring_verify(const void *a_message, size_t a_message_size,
                       const chipmunk_ring_signature_t *a_signature,
                       const chipmunk_ring_container_t *a_ring);

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
 * @brief Create commitment for ZKP
 * @param commitment Output commitment
 * @param public_key Public key to commit to
 * @return 0 on success, negative on error
 */
int chipmunk_ring_commitment_create(chipmunk_ring_commitment_t *a_commitment,
                                 const chipmunk_ring_public_key_t *a_public_key);

/**
 * @brief Create response for ZKP
 * @param response Output response
 * @param commitment Commitment used
 * @param challenge Challenge value
 * @param private_key Private key (NULL for dummy participants)
 * @return 0 on success, negative on error
 */
int chipmunk_ring_response_create(chipmunk_ring_response_t *a_response,
                               const chipmunk_ring_commitment_t *a_commitment,
                               const uint8_t a_challenge[32],
                               const chipmunk_ring_private_key_t *a_private_key);

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
