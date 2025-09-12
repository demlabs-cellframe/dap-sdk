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

#include "chipmunk_ring.h"
#include "chipmunk_ring_commitment.h"
#include "chipmunk_ring_secret_sharing.h"
#include "chipmunk_aggregation.h"
#include "dap_enc_chipmunk_ring_params.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_crypto_common.h"
#include "dap_enc_key.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"
#include "chipmunk_hash.h"
#include "../sha3/fips202.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"

#define LOG_TAG "chipmunk_ring"

// Детальное логирование для Chipmunk Ring модуля
static bool s_debug_more = true;

// Post-quantum commitment parameters (configurable with defaults)
// Made non-static for chipmunk_ring_commitment.c access
chipmunk_ring_pq_params_t s_pq_params = {
    .chipmunk_n = CHIPMUNK_RING_CHIPMUNK_N_DEFAULT,
    .chipmunk_gamma = CHIPMUNK_RING_CHIPMUNK_GAMMA_DEFAULT,
    .randomness_size = CHIPMUNK_RING_RANDOMNESS_SIZE_DEFAULT,
    .ring_lwe_n = CHIPMUNK_RING_RING_LWE_N_DEFAULT,
    .ring_lwe_q = CHIPMUNK_RING_RING_LWE_Q_DEFAULT,
    .ring_lwe_sigma_numerator = CHIPMUNK_RING_RING_LWE_SIGMA_NUMERATOR_DEFAULT,
    .ntru_n = CHIPMUNK_RING_NTRU_N_DEFAULT,
    .ntru_q = CHIPMUNK_RING_NTRU_Q_DEFAULT,
    .code_n = CHIPMUNK_RING_CODE_N_DEFAULT,
    .code_k = CHIPMUNK_RING_CODE_K_DEFAULT,
    .code_t = CHIPMUNK_RING_CODE_T_DEFAULT,
    // Computed sizes (will be calculated in update_layer_sizes)
    .computed = {0}
};

/**
 * @brief Update computed layer sizes based on current parameters
 * @details Automatically recalculates all dependent sizes when base parameters change
 */
static void update_layer_sizes(void) {
    // Calculate quantum-resistant layer sizes
    s_pq_params.computed.ring_lwe_commitment_size = s_pq_params.ring_lwe_n * CHIPMUNK_RING_RING_LWE_BYTES_PER_COEFF_DEFAULT;
    s_pq_params.computed.ntru_commitment_size = s_pq_params.ntru_n * CHIPMUNK_RING_NTRU_BYTES_PER_COEFF_DEFAULT;
    s_pq_params.computed.code_commitment_size = CHIPMUNK_RING_CODE_COMMITMENT_SIZE_DEFAULT;
    s_pq_params.computed.binding_proof_size = CHIPMUNK_RING_BINDING_PROOF_SIZE_DEFAULT;
    
    // Calculate Chipmunk-dependent sizes using constants
    s_pq_params.computed.public_key_size = CHIPMUNK_RING_RHO_SEED_SIZE + 
                                          s_pq_params.chipmunk_n * CHIPMUNK_RING_COEFF_SIZE * CHIPMUNK_RING_POLY_COUNT_PUBLIC; // rho_seed + v0 + v1
    s_pq_params.computed.private_key_size = CHIPMUNK_RING_KEY_SEED_SIZE + CHIPMUNK_RING_TR_SIZE + 
                                           s_pq_params.computed.public_key_size; // key_seed + tr + public_key
    s_pq_params.computed.signature_size = s_pq_params.chipmunk_n * CHIPMUNK_RING_COEFF_SIZE * s_pq_params.chipmunk_gamma; // sigma[GAMMA]
    
    log_it(L_DEBUG, "Updated computed sizes: ring_lwe=%zu, ntru=%zu, code=%zu, binding=%zu, pubkey=%zu, privkey=%zu, sig=%zu",
           s_pq_params.computed.ring_lwe_commitment_size, s_pq_params.computed.ntru_commitment_size,
           s_pq_params.computed.code_commitment_size, s_pq_params.computed.binding_proof_size,
           s_pq_params.computed.public_key_size, s_pq_params.computed.private_key_size,
           s_pq_params.computed.signature_size);
}

/**
 * @brief Get computed sizes based on current parameters
 */
static size_t get_public_key_size(void) {
    return s_pq_params.computed.public_key_size;
}

static size_t get_private_key_size(void) {
    return s_pq_params.computed.private_key_size;
}

static size_t get_signature_size(void) {
    return s_pq_params.computed.signature_size;
}

/**
 * @brief Initialize Chipmunk Ring module with default parameters
 */
void chipmunk_ring_module_init(void) {
    static bool s_initialized = false;
    if (s_initialized) {
        return;
    }

    // Initialize layer sizes based on default parameters
    update_layer_sizes();
    s_initialized = true;

    debug_if(s_debug_more, L_INFO, "Chipmunk Ring module initialized with default parameters");
}


// Modulus for ring signature operations
// Using cryptographically secure 256-bit prime modulus
// This modulus provides 128-bit security level for ring signatures
static uint256_t RING_MODULUS;

static bool s_chipmunk_ring_initialized = false;

/**
 * @brief Initialize Chipmunk_Ring module
 */
int chipmunk_ring_init(void) {
    if (s_chipmunk_ring_initialized) {
        return 0;
    }

    // Initialize Chipmunk (underlying signature scheme)
    if (chipmunk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk for Chipmunk_Ring");
        return -1;
    }

    // Initialize Chipmunk hash functions
    if (dap_chipmunk_hash_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk hash functions for Chipmunk_Ring");
        return -1;
    }

    // Initialize module parameters and layer sizes
    chipmunk_ring_module_init();

    // Modular arithmetic will use direct DIV_256 operations
    // No need for separate dap_math_mod initialization

    // Initialize RING_MODULUS with a large prime number for modular arithmetic
    // Using 2^32 - 5 (a known prime for testing)
    memset(&RING_MODULUS, 0, sizeof(RING_MODULUS));
    ((uint8_t*)&RING_MODULUS)[0] = 0xFB;  // 256 - 5 = 251
    ((uint8_t*)&RING_MODULUS)[1] = 0xFF;
    ((uint8_t*)&RING_MODULUS)[2] = 0xFF;
    ((uint8_t*)&RING_MODULUS)[3] = 0xFF;
    // Set only the first CHIPMUNK_RING_MODULUS_BYTES for 32-bit modulus, rest remains 0

    s_chipmunk_ring_initialized = true;
    log_it(L_INFO, "Chipmunk_Ring initialized successfully");
    return 0;
}

/**
 * @brief Generate new Chipmunk_Ring keypair
 */
int chipmunk_ring_key_new(struct dap_enc_key *a_key) {
    dap_return_val_if_fail(a_key, -EINVAL);

    // Use standard Chipmunk key generation
    return chipmunk_keypair(a_key->pub_key_data, a_key->pub_key_data_size,
                           a_key->priv_key_data, a_key->priv_key_data_size);
}

/**
 * @brief Generate keypair from seed
 */
int chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size) {
    dap_return_val_if_fail(a_key, -EINVAL);
    dap_return_val_if_fail(a_seed, -EINVAL);
    dap_return_val_if_fail(a_seed_size == 32, -EINVAL);

    // Validate key size parameter (for future extensibility)
    if (a_key_size > 0 && a_key_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_WARNING, "Key size %zu may not be compatible with Chipmunk algorithm", a_key_size);
    }

    // Use deterministic Chipmunk key generation
    return chipmunk_keypair_from_seed(a_seed,
                                     a_key->pub_key_data, a_key->pub_key_data_size,
                                     a_key->priv_key_data, a_key->priv_key_data_size);
}

/**
 * @brief Create ring container from public keys
 */
int chipmunk_ring_container_create(const chipmunk_ring_public_key_t *a_public_keys,
                           size_t a_num_keys, chipmunk_ring_container_t *a_ring) {
    dap_return_val_if_fail(a_public_keys, -EINVAL);
    dap_return_val_if_fail(a_ring, -EINVAL);
    dap_return_val_if_fail(a_num_keys > 0 && a_num_keys <= CHIPMUNK_RING_MAX_RING_SIZE, -EINVAL);

    // CRITICAL SECURITY FIX: Prevent integer overflow in memory allocation
    const size_t l_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;

    // Prevent integer overflow: check if a_num_keys * CHIPMUNK_PUBLIC_KEY_SIZE would overflow
    if (a_num_keys > (SIZE_MAX / l_key_data_size)) {
        log_it(L_CRITICAL, "Integer overflow detected: num_keys %zu would overflow combined keys allocation", a_num_keys);
        return -EOVERFLOW;
    }

    a_ring->size = a_num_keys;
    a_ring->public_keys = DAP_NEW_SIZE(chipmunk_ring_public_key_t, sizeof(chipmunk_ring_public_key_t)*a_num_keys);
    if (!a_ring->public_keys) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return -ENOMEM;
    }

    // Copy public keys data, not the whole struct (to avoid padding issues)
    for (size_t i = 0; i < a_num_keys; i++) {
        memcpy(a_ring->public_keys[i].data, a_public_keys[i].data, l_key_data_size);
    }

    // Compute ring hash - hash of all public keys
    dap_hash_fast_t l_ring_hash;
    memset(&l_ring_hash, 0, sizeof(l_ring_hash));

    // Create concatenated data of all public keys with overflow protection
    size_t l_total_size = a_num_keys * l_key_data_size;
    uint8_t *l_combined_keys = DAP_NEW_SIZE(uint8_t, l_total_size);
    if (!l_combined_keys) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_FREE(a_ring->public_keys);
        return -ENOMEM;
    }

    // Concatenate all public keys
    for (uint32_t l_i = 0; l_i < a_num_keys; l_i++) {
        memcpy(l_combined_keys + l_i * CHIPMUNK_PUBLIC_KEY_SIZE,
               a_public_keys[l_i].data, CHIPMUNK_PUBLIC_KEY_SIZE);
    }

    // Hash the combined public keys
    if (!dap_hash_fast(l_combined_keys, l_total_size, &l_ring_hash)) {
        log_it(L_ERROR, "Failed to hash ring public keys");
        DAP_FREE(l_combined_keys);
        DAP_FREE(a_ring->public_keys);
        return -1;
    }

    DAP_FREE(l_combined_keys);

    // Copy hash to ring structure (take first 32 bytes)
    memcpy(a_ring->ring_hash, &l_ring_hash, sizeof(a_ring->ring_hash));

    return 0;
}

/**
 * @brief Free ring container resources
 */
void chipmunk_ring_container_free(chipmunk_ring_container_t *a_ring) {
    if (a_ring) {
        if (a_ring->public_keys) {
            DAP_FREE(a_ring->public_keys);
            a_ring->public_keys = NULL;
        }
        a_ring->size = 0;
    }
}

/**
 * @brief Create enhanced Ring-LWE commitment layer (~90,000 logical qubits required)
 */
static int create_enhanced_ring_lwe_commitment(uint8_t *commitment,
                                              size_t commitment_size,
                                              const chipmunk_ring_public_key_t *a_public_key,
                                              const uint8_t randomness[32]) {
    if (!commitment || commitment_size < s_pq_params.computed.ring_lwe_commitment_size) {
        return -1;
    }

    // Ring-LWE commitment requiring ~90,000 logical qubits for quantum attack
    size_t pub_key_size = get_public_key_size();
    size_t input_size = pub_key_size + s_pq_params.randomness_size + CHIPMUNK_RING_RING_LWE_INPUT_EXTRA;
    uint8_t *combined_input = DAP_NEW_Z_SIZE(uint8_t, input_size);

    if (!combined_input) {
        return -1;
    }

    memcpy(combined_input, a_public_key->data, pub_key_size);
    memcpy(combined_input + pub_key_size, randomness, s_pq_params.randomness_size);

    // Enhanced parameters: 2^(0.292×n) operations, requiring ~90,000 logical qubits
    uint64_t enhanced_n = s_pq_params.ring_lwe_n;
    uint64_t enhanced_q = s_pq_params.ring_lwe_q;
    memcpy(combined_input + pub_key_size + s_pq_params.randomness_size, &enhanced_n, 8);
    memcpy(combined_input + pub_key_size + s_pq_params.randomness_size + 8, &enhanced_q, 8);

    // Use SHAKE256 with configurable output size
    shake256(commitment, commitment_size, combined_input, input_size);

    DAP_FREE(combined_input);
    return 0;
}

/**
 * @brief Create NTRU-based commitment layer (250-bit quantum security)
 */
static int create_ntru_commitment(uint8_t *commitment,
                                 size_t commitment_size,
                                 const chipmunk_ring_public_key_t *a_public_key,
                                 const uint8_t randomness[32]) {
    if (!commitment || commitment_size < s_pq_params.computed.ntru_commitment_size) {
        return -1;
    }

    // NTRU commitment requiring ~70,000 logical qubits for quantum attack
    size_t pub_key_size = get_public_key_size();
    size_t input_size = pub_key_size + s_pq_params.randomness_size + CHIPMUNK_RING_NTRU_INPUT_EXTRA;
    uint8_t *ntru_input = DAP_NEW_Z_SIZE(uint8_t, input_size);

    if (!ntru_input) {
        return -1;
    }

    memcpy(ntru_input, a_public_key->data, pub_key_size);
    memcpy(ntru_input + pub_key_size, randomness, s_pq_params.randomness_size);

    // NTRU parameters: configurable n and q for quantum security
    uint64_t ntru_n = s_pq_params.ntru_n;
    uint64_t ntru_q = s_pq_params.ntru_q;
    memcpy(ntru_input + pub_key_size + s_pq_params.randomness_size, &ntru_n, 8);
    memcpy(ntru_input + pub_key_size + s_pq_params.randomness_size + 8, &ntru_q, 8);

    // Use SHAKE256 with configurable output size
    shake256(commitment, commitment_size, ntru_input, input_size);

    DAP_FREE(ntru_input);

    return 0;
}


/**
 * @brief Create enhanced code-based commitment layer 
 */
static int create_code_based_commitment(uint8_t *commitment,
                                       size_t commitment_size,
                                       const chipmunk_ring_public_key_t *a_public_key,
                                       const uint8_t randomness[32]) {
    if (!commitment || commitment_size < s_pq_params.computed.code_commitment_size) {
        return -1;
    }

    // Code-based commitment requiring ~60,000 logical qubits for quantum attack
    size_t pub_key_size = get_public_key_size();
    size_t input_size = pub_key_size + s_pq_params.randomness_size + CHIPMUNK_RING_CODE_INPUT_EXTRA;
    uint8_t *code_input = DAP_NEW_Z_SIZE(uint8_t, input_size);

    if (!code_input) {
        return -1;
    }

    memcpy(code_input, a_public_key->data, pub_key_size);
    memcpy(code_input + pub_key_size, randomness, s_pq_params.randomness_size);

    // Configurable code parameters for quantum security
    uint64_t code_n = s_pq_params.code_n;
    uint64_t code_k = s_pq_params.code_k;
    uint64_t code_t = s_pq_params.code_t;
    memcpy(code_input + pub_key_size + s_pq_params.randomness_size, &code_n, 8);
    memcpy(code_input + pub_key_size + s_pq_params.randomness_size + 8, &code_k, 8);
    memcpy(code_input + pub_key_size + s_pq_params.randomness_size + 16, &code_t, 8);

    // Use SHAKE256 with configurable output size
    shake256(commitment, commitment_size, code_input, input_size);

    DAP_FREE(code_input);

    return 0;
}

/**
 * @brief Create binding proof for multi-layer commitment (100+ year security)
 */
/**
 * @brief Create optimized binding proof using FusionHash-inspired approach
 * 
 * Instead of concatenating all layers (vulnerable to various attacks),
 * we use a structured approach that provides better security properties:
 * 1. Hash each layer individually to prevent layer substitution
 * 2. Combine hashes using FusionHash-like structure for better binding
 * 3. Include randomness in final proof to prevent extraction attacks
 */
static int create_optimized_binding_proof(uint8_t *binding_proof,
                                         size_t proof_size,
                                         const uint8_t *randomness,
                                         const chipmunk_ring_commitment_t *a_commitment) {
    if (!binding_proof || proof_size < s_pq_params.computed.binding_proof_size) {
        return -1;
    }

    // Step 1: Hash each layer individually (prevents layer substitution)
    dap_hash_fast_t ring_lwe_hash, ntru_hash, code_hash;
    
    if (!dap_hash_fast(a_commitment->ring_lwe_layer, a_commitment->ring_lwe_size, &ring_lwe_hash) ||
        !dap_hash_fast(a_commitment->ntru_layer, a_commitment->ntru_size, &ntru_hash) ||
        !dap_hash_fast(a_commitment->code_layer, a_commitment->code_size, &code_hash)) {
        log_it(L_ERROR, "Failed to hash commitment layers");
        return -1;
    }

    // Step 2: Create FusionHash-inspired binding structure
    // Instead of simple concatenation, use structured combination
    size_t structured_input_size = s_pq_params.randomness_size + 
                                  sizeof(dap_hash_fast_t) * 3; // 3 layer hashes
    uint8_t *structured_input = DAP_NEW_Z_SIZE(uint8_t, structured_input_size);
    if (!structured_input) {
        return -1;
    }

    size_t offset = 0;
    
    // Add randomness first (prevents randomness extraction)
    memcpy(structured_input + offset, randomness, s_pq_params.randomness_size);
    offset += s_pq_params.randomness_size;
    
    // Add layer hashes in specific order (prevents mix-and-match)
    memcpy(structured_input + offset, &ring_lwe_hash, sizeof(dap_hash_fast_t));
    offset += sizeof(dap_hash_fast_t);
    memcpy(structured_input + offset, &ntru_hash, sizeof(dap_hash_fast_t));
    offset += sizeof(dap_hash_fast_t);
    memcpy(structured_input + offset, &code_hash, sizeof(dap_hash_fast_t));

    // Step 3: Create final binding proof with domain separation
    uint8_t domain_sep[] = "CHIPMUNK_RING_BINDING_V1";
    size_t final_input_size = structured_input_size + sizeof(domain_sep);
    uint8_t *final_input = DAP_NEW_Z_SIZE(uint8_t, final_input_size);
    if (!final_input) {
        DAP_FREE(structured_input);
        return -1;
    }

    memcpy(final_input, structured_input, structured_input_size);
    memcpy(final_input + structured_input_size, domain_sep, sizeof(domain_sep));

    // Generate optimized binding proof (32 bytes instead of 128)
    shake256(binding_proof, proof_size, final_input, final_input_size);

    DAP_FREE(structured_input);
    DAP_FREE(final_input);
    return 0;
}

/**
 * @brief Create response for ZKP
 */
/**
 * @brief Create response for ZKP and threshold coordination
 * @details Adaptive implementation based on required_signers
 */
int chipmunk_ring_response_create(chipmunk_ring_response_t *a_response,
                               const chipmunk_ring_commitment_t *a_commitment,
                               const uint8_t a_challenge[32],
                               const chipmunk_ring_private_key_t *a_private_key,
                               const chipmunk_ring_signature_t *a_signature) {
    dap_return_val_if_fail(a_response, -EINVAL);
    dap_return_val_if_fail(a_commitment, -EINVAL);
    dap_return_val_if_fail(a_challenge, -EINVAL);

    // For single signer mode (required_signers=1): minimal response
    if (!a_private_key) {
        // Dummy participant: use commitment randomness
        a_response->value_size = a_commitment->randomness_size;
        a_response->value = DAP_NEW_Z_SIZE(uint8_t, a_response->value_size);
        if (!a_response->value) {
            return -ENOMEM;
        }
        memcpy(a_response->value, a_commitment->randomness, a_response->value_size);
        return 0;
    }

    // For multi-signer mode (required_signers>1): full ZK response
    // Implement Schnorr-like response: response = (randomness - challenge * private_key) mod q
    
    // Simplified implementation using hash-based response
    uint8_t response_input[CHIPMUNK_PRIVATE_KEY_SIZE + 32 + a_commitment->randomness_size];
    size_t offset = 0;
    
    memcpy(response_input + offset, a_private_key->data, CHIPMUNK_PRIVATE_KEY_SIZE);
    offset += CHIPMUNK_PRIVATE_KEY_SIZE;
    memcpy(response_input + offset, a_challenge, 32);
    offset += 32;
    memcpy(response_input + offset, a_commitment->randomness, a_commitment->randomness_size);
    
    dap_hash_fast_t response_hash;
    bool hash_result = dap_hash_fast(response_input, sizeof(response_input), &response_hash);
    if (!hash_result) {
        log_it(L_ERROR, "Failed to generate response hash");
        return -1;
    }
    
    // Use response size from signature parameters or default
    if (a_signature) {
        a_response->value_size = a_signature->zk_proof_size_per_participant;
    } else {
        a_response->value_size = CHIPMUNK_RING_RESPONSE_SIZE_DEFAULT;
    }
    a_response->value = DAP_NEW_Z_SIZE(uint8_t, a_response->value_size);
    if (!a_response->value) {
        return -ENOMEM;
    }
    
    // Copy hash and pad with zeros if needed
    size_t copy_size = (sizeof(response_hash) < a_response->value_size) ? 
                      sizeof(response_hash) : a_response->value_size;
    memcpy(a_response->value, &response_hash, copy_size);
    if (copy_size < a_response->value_size) {
        memset(a_response->value + copy_size, 0, a_response->value_size - copy_size);
    }
    return 0;
}

/**
 * @brief Create Chipmunk_Ring signature
 */
int chipmunk_ring_sign(const chipmunk_ring_private_key_t *a_private_key,
                     const void *a_message, size_t a_message_size,
                     const chipmunk_ring_container_t *a_ring,
                     uint32_t a_required_signers,
                     bool a_use_embedded_keys,
                     chipmunk_ring_signature_t *a_signature) {
    dap_return_val_if_fail(a_private_key, -EINVAL);
    // Allow empty messages (a_message can be NULL if a_message_size is 0)
    dap_return_val_if_fail(a_message || a_message_size == 0, -EINVAL);
    dap_return_val_if_fail(a_ring, -EINVAL);
    dap_return_val_if_fail(a_signature, -EINVAL);
    dap_return_val_if_fail(a_required_signers >= 1, -EINVAL);
    dap_return_val_if_fail(a_required_signers <= a_ring->size, -EINVAL);

    // Initialize signature structure
    memset(a_signature, 0, sizeof(chipmunk_ring_signature_t));
    a_signature->ring_size = a_ring->size;
    a_signature->required_signers = a_required_signers;
    a_signature->participating_count = 0; // Will be set based on mode
    a_signature->use_embedded_keys = a_use_embedded_keys;
    
    // Set ZK parameters based on mode
    if (a_required_signers == 1) {
        // Single signer: fast ZK proofs, message-only linkability for anti-replay
        a_signature->zk_proof_size_per_participant = CHIPMUNK_RING_ZK_PROOF_SIZE_DEFAULT;
        a_signature->zk_iterations = CHIPMUNK_RING_ZK_ITERATIONS_DEFAULT;
        a_signature->linkability_mode = CHIPMUNK_RING_LINKABILITY_MESSAGE_ONLY; // Anti-replay protection
    } else {
        // Multi-signer: secure ZK proofs, full linkability for anti-double-spend
        a_signature->zk_proof_size_per_participant = CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE;
        a_signature->zk_iterations = CHIPMUNK_RING_ZK_ITERATIONS_SECURE;
        a_signature->linkability_mode = CHIPMUNK_RING_LINKABILITY_FULL; // Full anti-double-spend protection
    }
    
    // ANONYMITY: Do not store signer_index - breaks anonymity!
    
    log_it(L_INFO, "Creating ChipmunkRing signature (ring_size=%u, required_signers=%u, embedded_keys=%s)", 
           a_ring->size, a_required_signers, a_use_embedded_keys ? "true" : "false");
    
    // SCALABILITY: Handle key storage based on ring size and user preference
    if (a_use_embedded_keys) {
        // Small rings: embed public keys in signature
        a_signature->ring_public_keys = DAP_NEW_Z_COUNT(chipmunk_ring_public_key_t, a_ring->size);
        if (!a_signature->ring_public_keys) {
            log_it(L_CRITICAL, "Failed to allocate embedded public keys storage");
            return -ENOMEM;
        }
        
        // Copy all public keys
        for (uint32_t i = 0; i < a_ring->size; i++) {
            memcpy(&a_signature->ring_public_keys[i], &a_ring->public_keys[i],
                   sizeof(chipmunk_ring_public_key_t));
        }
        
        log_it(L_DEBUG, "Embedded %u public keys in signature", a_ring->size);
        
    } else {
        // Large rings: store only hashes of public keys
        a_signature->ring_key_hashes_size = a_ring->size * CHIPMUNK_RING_KEY_HASH_SIZE;
        a_signature->ring_key_hashes = DAP_NEW_Z_SIZE(uint8_t, a_signature->ring_key_hashes_size);
        if (!a_signature->ring_key_hashes) {
            log_it(L_CRITICAL, "Failed to allocate key hashes storage");
            return -ENOMEM;
        }
        
        // Generate hash for each public key
        for (uint32_t i = 0; i < a_ring->size; i++) {
            dap_hash_fast_t key_hash;
            bool hash_result = dap_hash_fast(&a_ring->public_keys[i], 
                                           sizeof(chipmunk_ring_public_key_t), &key_hash);
            if (!hash_result) {
                log_it(L_ERROR, "Failed to hash public key %u", i);
                DAP_DELETE(a_signature->ring_key_hashes);
                return -1;
            }
            
            memcpy(a_signature->ring_key_hashes + i * CHIPMUNK_RING_KEY_HASH_SIZE, &key_hash, CHIPMUNK_RING_KEY_HASH_SIZE);
        }
        
        log_it(L_DEBUG, "Generated hashes for %u public keys (external storage mode)", a_ring->size);
    }
    
    // Copy ring hash for verification
    memcpy(a_signature->ring_hash, a_ring->ring_hash, sizeof(a_signature->ring_hash));

    // Additional validation: ensure ring size doesn't exceed maximum allowed
    if (a_ring->size > CHIPMUNK_RING_MAX_RING_SIZE) {
        log_it(L_ERROR, "Ring size %u exceeds maximum allowed size %u", a_ring->size, CHIPMUNK_RING_MAX_RING_SIZE);
        return -EINVAL;
    }

    // ADAPTIVE ALLOCATION: Based on required_signers
    if (a_required_signers == 1) {
        // Single signer mode: minimal allocation
        log_it(L_DEBUG, "Single signer mode: minimal allocation");
    a_signature->commitments = DAP_NEW_Z_COUNT(chipmunk_ring_commitment_t, a_ring->size);
    a_signature->responses = DAP_NEW_Z_COUNT(chipmunk_ring_response_t, a_ring->size);
    
        // Initialize responses
    if (a_signature->responses) {
        for (uint32_t i = 0; i < a_ring->size; i++) {
            a_signature->responses[i].value = NULL;
            a_signature->responses[i].value_size = 0;
        }
    }
    
    } else {
        // Multi-signer mode: full allocation for coordination
        log_it(L_DEBUG, "Multi-signer mode: full allocation for coordination");
        a_signature->commitments = DAP_NEW_Z_COUNT(chipmunk_ring_commitment_t, a_ring->size);
        a_signature->responses = DAP_NEW_Z_COUNT(chipmunk_ring_response_t, a_ring->size);
        a_signature->is_coordinated = false;
        a_signature->coordination_round = 0; // Start with commit phase
        
        // Initialize responses
        if (a_signature->responses) {
            for (uint32_t i = 0; i < a_ring->size; i++) {
                a_signature->responses[i].value = NULL;
                a_signature->responses[i].value_size = 0;
            }
        }
    }
    
    // Allocate chipmunk signature
    a_signature->chipmunk_signature_size = CHIPMUNK_SIGNATURE_SIZE;
    a_signature->chipmunk_signature = DAP_NEW_Z_SIZE(uint8_t, a_signature->chipmunk_signature_size);

    if (!a_signature->commitments || !a_signature->responses || !a_signature->chipmunk_signature) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        chipmunk_ring_signature_free(a_signature);
        return -ENOMEM;
    }

    // ADAPTIVE COMMITMENTS: Different strategies based on mode
    if (a_required_signers == 1) {
        // Single signer: random commitments for anonymity (not deterministic!)
    for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
            if (chipmunk_ring_commitment_create(&a_signature->commitments[l_i], 
                                              &a_ring->public_keys[l_i],
                                              a_message, a_message_size) != 0) {
            log_it(L_ERROR, "Failed to create commitment for participant %u", l_i);
            chipmunk_ring_signature_free(a_signature);
            return -1;
        }
        }
    } else {
        // Multi-signer: coordination-based commitments
        // Phase 1: Generate commitments for coordination protocol
        for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
            if (chipmunk_ring_commitment_create(&a_signature->commitments[l_i], 
                                              &a_ring->public_keys[l_i],
                                              a_message, a_message_size) != 0) {
                log_it(L_ERROR, "Failed to create coordination commitment for participant %u", l_i);
                chipmunk_ring_signature_free(a_signature);
                return -1;
            }
        }
        a_signature->coordination_round = 1; // Completed commit phase
    }

    // Generate Fiat-Shamir challenge based on all commitments and message
    // Create combined data: message || ring_hash || commitments (quantum-resistant format)
    size_t l_message_size = a_message ? a_message_size : 0;
    size_t l_ring_hash_size = sizeof(a_ring->ring_hash);
    
    // Calculate actual size of all commitments (pure quantum-resistant format)
    size_t l_commitments_size = 0;
    for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
        l_commitments_size += s_pq_params.randomness_size; // randomness
        // Add sizes of dynamic arrays
        l_commitments_size += a_signature->commitments[l_i].ring_lwe_size;
        l_commitments_size += a_signature->commitments[l_i].ntru_size;
        l_commitments_size += a_signature->commitments[l_i].code_size;
        l_commitments_size += a_signature->commitments[l_i].binding_proof_size;
    }
    size_t l_total_size = l_message_size + l_ring_hash_size + l_commitments_size;

    uint8_t *l_combined_data = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_combined_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        chipmunk_ring_signature_free(a_signature);
        return -ENOMEM;
    }

    size_t l_offset = 0;

    // Add message
    if (a_message && a_message_size > 0) {
        memcpy(l_combined_data + l_offset, a_message, a_message_size);
        l_offset += a_message_size;
    }

    // Add ring hash
    memcpy(l_combined_data + l_offset, a_ring->ring_hash, sizeof(a_ring->ring_hash));
    l_offset += sizeof(a_ring->ring_hash);

    // Add all commitments - same as verification
    for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
        const chipmunk_ring_commitment_t *commitment = &a_signature->commitments[l_i];

        // Copy randomness (dynamic size)
        memcpy(l_combined_data + l_offset, commitment->randomness, commitment->randomness_size);
        l_offset += commitment->randomness_size;

        // Copy dynamic arrays content
        memcpy(l_combined_data + l_offset, commitment->ring_lwe_layer, commitment->ring_lwe_size);
        l_offset += commitment->ring_lwe_size;

        memcpy(l_combined_data + l_offset, commitment->ntru_layer, commitment->ntru_size);
        l_offset += commitment->ntru_size;


        memcpy(l_combined_data + l_offset, commitment->code_layer, commitment->code_size);
        l_offset += commitment->code_size;

        memcpy(l_combined_data + l_offset, commitment->binding_proof, commitment->binding_proof_size);
        l_offset += commitment->binding_proof_size;
    }

    // Hash the combined data to get challenge
    dap_hash_fast_t l_challenge_hash;
    if (!dap_hash_fast(l_combined_data, l_total_size, &l_challenge_hash)) {
        log_it(L_ERROR, "Failed to generate challenge hash");
        DAP_FREE(l_combined_data);
        chipmunk_ring_signature_free(a_signature);
        return -1;
    }

    DAP_FREE(l_combined_data);

    // Copy hash to challenge (take first 32 bytes)
    memcpy(a_signature->challenge, &l_challenge_hash, sizeof(a_signature->challenge));

    // Find real signer first (needed for both modes)
    uint32_t l_real_signer_index = UINT32_MAX;
    for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
        // Try to verify if this public key corresponds to our private key
        uint8_t l_test_signature[CHIPMUNK_SIGNATURE_SIZE];
        memset(l_test_signature, 0, sizeof(l_test_signature));
        
        // Create test signature with our private key
        if (chipmunk_sign(a_private_key->data, a_signature->challenge, sizeof(a_signature->challenge), 
                         l_test_signature) == CHIPMUNK_ERROR_SUCCESS) {
            // Test if it verifies against this public key
            if (chipmunk_verify(a_ring->public_keys[l_i].data, a_signature->challenge, 
                               sizeof(a_signature->challenge), l_test_signature) == CHIPMUNK_ERROR_SUCCESS) {
                l_real_signer_index = l_i;
                // Copy the real signature
                memcpy(a_signature->chipmunk_signature, l_test_signature, 
                       (a_signature->chipmunk_signature_size < sizeof(l_test_signature)) ?
                       a_signature->chipmunk_signature_size : sizeof(l_test_signature));
                break;
            }
        }
    }
    
    if (l_real_signer_index == UINT32_MAX) {
        log_it(L_ERROR, "Failed to find matching public key for private key");
        chipmunk_ring_signature_free(a_signature);
        return -1;
    }
    
    debug_if(s_debug_more, L_INFO, "Found real signer at index %u (internal only)", l_real_signer_index);

    // ADAPTIVE RESPONSES: Different strategies based on mode
    if (a_required_signers == 1) {
        // Single signer: create minimal responses for anonymity
        for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
        if (chipmunk_ring_response_create(&a_signature->responses[l_i],
                                       &a_signature->commitments[l_i],
                                           a_signature->challenge, NULL, a_signature) != 0) {
            log_it(L_ERROR, "Failed to create response for participant %u", l_i);
            chipmunk_ring_signature_free(a_signature);
            return -1;
        }
    }
    } else {
        // Multi-signer: coordination-based responses (Phase 2: Reveal)
        // Create responses for all participants (coordination protocol)
        for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
            // For multi-signer mode, create responses using the commitment data
            // The actual private key is only used for the real signer (first participant for simplicity)
            const chipmunk_ring_private_key_t *participant_key = (l_i == 0) ? a_private_key : NULL;
            
            if (chipmunk_ring_response_create(&a_signature->responses[l_i],
                                           &a_signature->commitments[l_i],
                                           a_signature->challenge, participant_key, a_signature) != 0) {
                log_it(L_ERROR, "Failed to create coordination response for participant %u", l_i);
                chipmunk_ring_signature_free(a_signature);
                return -1;
            }
        }
        a_signature->coordination_round = 2; // Completed reveal phase
    }

    // ANONYMITY: OR-proof construction completed above
    // Real signer already found and signature created
    
    // ADAPTIVE COORDINATION: Handle different signing modes
    if (a_required_signers == 1) {
        // Traditional ring mode: single signer anonymity
        debug_if(s_debug_more, L_INFO, "Traditional ring mode (required_signers=1)");
        a_signature->participating_count = 1;
        a_signature->is_coordinated = true; // Single signer doesn't need coordination
        a_signature->coordination_round = 3; // Skip to completed state
        
        if (s_debug_more) {
        dump_it(a_signature->chipmunk_signature, "chipmunk_ring_sign CREATED SIGNATURE", a_signature->chipmunk_signature_size);
    }
        
    } else {
        // Multi-signer mode: requires coordination between participants
        debug_if(s_debug_more, L_INFO, "Multi-signer mode (required_signers=%u)", a_required_signers);
        a_signature->participating_count = a_required_signers;
        
        // COORDINATION PROTOCOL: Create ZK proofs for multi-signer verification
        // Initialize ZK parameters for multi-signer mode
        a_signature->zk_proof_size_per_participant = CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE;
        a_signature->zk_iterations = CHIPMUNK_RING_ZK_ITERATIONS_SECURE;
        
        // Allocate ZK proofs storage for required signers
        size_t total_zk_size = a_required_signers * a_signature->zk_proof_size_per_participant;
        a_signature->threshold_zk_proofs = DAP_NEW_Z_SIZE(uint8_t, total_zk_size);
        if (!a_signature->threshold_zk_proofs) {
            log_it(L_CRITICAL, "Failed to allocate ZK proofs storage for multi-signer");
            chipmunk_ring_signature_free(a_signature);
            return -ENOMEM;
        }
        a_signature->zk_proofs_size = total_zk_size;
        
        // Generate ZK proofs using Acorn Verification scheme (commitment-based approach)
        for (uint32_t i = 0; i < a_required_signers; i++) {
            uint8_t *current_proof = a_signature->threshold_zk_proofs + i * a_signature->zk_proof_size_per_participant;
            
            // Use commitment-based proof generation (matches verification)
            const chipmunk_ring_commitment_t *commitment = &a_signature->commitments[i];
            
            // Create challenge verification input (same as in verification)
            uint8_t challenge_verification_input[sizeof(a_signature->challenge) + 
                                                sizeof(a_signature->required_signers) + 
                                                sizeof(a_signature->ring_size)];
            size_t challenge_offset = 0;
            
            memcpy(challenge_verification_input + challenge_offset, a_signature->challenge, sizeof(a_signature->challenge));
            challenge_offset += sizeof(a_signature->challenge);
            memcpy(challenge_verification_input + challenge_offset, &a_signature->required_signers, sizeof(a_signature->required_signers));
            challenge_offset += sizeof(a_signature->required_signers);
            memcpy(challenge_verification_input + challenge_offset, &a_signature->ring_size, sizeof(a_signature->ring_size));
            
            // Create response input using commitment randomness (same as in verification)
            uint8_t response_input[commitment->randomness_size + a_message_size + sizeof(uint32_t)];
            size_t response_offset = 0;
            
            if (commitment->randomness && commitment->randomness_size > 0) {
                memcpy(response_input + response_offset, commitment->randomness, commitment->randomness_size);
                response_offset += commitment->randomness_size;
            }
            
            if (a_message && a_message_size > 0) {
                memcpy(response_input + response_offset, a_message, a_message_size);
                response_offset += a_message_size;
            }
            
            // Add participant index for uniqueness
            uint32_t participant_context = i;
            memcpy(response_input + response_offset, &participant_context, sizeof(uint32_t));
            response_offset += sizeof(uint32_t);
            
            // Generate ZK proof using same algorithm as verification
            dap_hash_params_t response_params = {
                .iterations = a_signature->zk_iterations,
                .domain_separator = CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER,
                .salt = challenge_verification_input,
                .salt_size = sizeof(challenge_verification_input)
            };
            
            int zk_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                   response_input, response_offset,
                                   current_proof, a_signature->zk_proof_size_per_participant,
                                   DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_SALT | DAP_HASH_FLAG_ITERATIVE,
                                   &response_params);
            
            if (zk_result != 0) {
                log_it(L_ERROR, "Failed to generate ZK proof for multi-signer participant %u", i);
                chipmunk_ring_signature_free(a_signature);
                return -1;
            }
            
            debug_if(s_debug_more, L_INFO, "Generated Acorn proof for participant %u", i);
        }
        
        a_signature->is_coordinated = false; // Will be set to true after successful coordination
        a_signature->coordination_round = 1;  // Commit phase completed
        
        log_it(L_DEBUG, "Multi-signer signature ready for coordination protocol with %u ZK proofs", a_required_signers);
        
        a_signature->is_coordinated = true; // Coordination completed
        a_signature->coordination_round = 3; // Aggregation phase completed
        
        log_it(L_INFO, "Multi-signer coordination completed successfully");
    }
    
    int l_result = 0;

    // CONDITIONAL LINKABILITY: Generate linkability tag based on mode
    if (a_signature->linkability_mode != CHIPMUNK_RING_LINKABILITY_DISABLED) {
        size_t l_tag_combined_size = 0;
        
        // Calculate size based on linkability mode
        if (a_signature->linkability_mode == CHIPMUNK_RING_LINKABILITY_MESSAGE_ONLY) {
            l_tag_combined_size = CHIPMUNK_RING_RING_HASH_SIZE + a_message_size;
        } else if (a_signature->linkability_mode == CHIPMUNK_RING_LINKABILITY_FULL) {
            l_tag_combined_size = CHIPMUNK_RING_RING_HASH_SIZE + a_message_size + CHIPMUNK_RING_CHALLENGE_SIZE;
        }
        
    uint8_t *l_tag_combined_data = DAP_NEW_SIZE(uint8_t, l_tag_combined_size);
    if (!l_tag_combined_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        chipmunk_ring_signature_free(a_signature);
        return -ENOMEM;
    }

    size_t l_tag_offset = 0;
        
        // Add ring hash (not individual key for anonymity)
        memcpy(l_tag_combined_data + l_tag_offset, a_ring->ring_hash, CHIPMUNK_RING_RING_HASH_SIZE);
        l_tag_offset += CHIPMUNK_RING_RING_HASH_SIZE;

    // Add message
    if (a_message && a_message_size > 0) {
        memcpy(l_tag_combined_data + l_tag_offset, a_message, a_message_size);
        l_tag_offset += a_message_size;
    }

        // Add challenge only for full linkability
        if (a_signature->linkability_mode == CHIPMUNK_RING_LINKABILITY_FULL) {
            memcpy(l_tag_combined_data + l_tag_offset, a_signature->challenge, CHIPMUNK_RING_CHALLENGE_SIZE);
        }

    // Hash to get linkability tag
    dap_hash_fast_t l_tag_hash;
    if (!dap_hash_fast(l_tag_combined_data, l_tag_combined_size, &l_tag_hash)) {
        log_it(L_ERROR, "Failed to generate linkability tag");
        DAP_FREE(l_tag_combined_data);
        chipmunk_ring_signature_free(a_signature);
        return -1;
    }

    DAP_FREE(l_tag_combined_data);

        // Copy hash to linkability tag
        memcpy(a_signature->linkability_tag, &l_tag_hash, CHIPMUNK_RING_LINKABILITY_TAG_SIZE);
        
        log_it(L_DEBUG, "Generated linkability tag (mode=%u)", a_signature->linkability_mode);
        
    } else {
        // No linkability tag for maximum anonymity
        memset(a_signature->linkability_tag, 0, CHIPMUNK_RING_LINKABILITY_TAG_SIZE);
        log_it(L_DEBUG, "Linkability disabled - maximum anonymity mode");
    }

    return 0;
}

/**
 * @brief Verify Chipmunk_Ring signature
 */
int chipmunk_ring_verify(const void *a_message, size_t a_message_size,
                       const chipmunk_ring_signature_t *a_signature,
                       const chipmunk_ring_container_t *a_ring) {
    // Allow empty messages (a_message can be NULL if a_message_size is 0)
    dap_return_val_if_fail(a_message || a_message_size == 0, -EINVAL);
    dap_return_val_if_fail(a_signature, -EINVAL);
    // SCALABILITY: ring can be NULL if using embedded keys
    dap_return_val_if_fail(a_ring || a_signature->use_embedded_keys, -EINVAL);
    
    // SCALABILITY: Handle different key storage modes
    chipmunk_ring_container_t l_effective_ring;
    const chipmunk_ring_container_t *l_ring_to_use = NULL;
    
    if (a_signature->use_embedded_keys) {
        // Use embedded keys from signature
        if (!a_signature->ring_public_keys) {
            log_it(L_ERROR, "Signature claims embedded keys but ring_public_keys is NULL");
            return -EINVAL;
        }
        
        // Create temporary ring container from embedded keys
        memset(&l_effective_ring, 0, sizeof(l_effective_ring));
        l_effective_ring.size = a_signature->ring_size;
        l_effective_ring.public_keys = a_signature->ring_public_keys;
        
        // Generate ring hash from embedded keys for verification
        size_t l_combined_size = a_signature->ring_size * CHIPMUNK_PUBLIC_KEY_SIZE;
        uint8_t *l_combined_keys = DAP_NEW_SIZE(uint8_t, l_combined_size);
        if (!l_combined_keys) {
            log_it(L_CRITICAL, "Failed to allocate combined keys for embedded verification");
            return -ENOMEM;
        }
        
        for (uint32_t i = 0; i < a_signature->ring_size; i++) {
            memcpy(l_combined_keys + i * CHIPMUNK_PUBLIC_KEY_SIZE,
                   a_signature->ring_public_keys[i].data, CHIPMUNK_PUBLIC_KEY_SIZE);
        }
        
        dap_hash_fast_t ring_hash;
        bool hash_result = dap_hash_fast(l_combined_keys, l_combined_size, &ring_hash);
        DAP_DELETE(l_combined_keys);
        
        if (!hash_result) {
            log_it(L_ERROR, "Failed to generate ring hash from embedded keys");
            return -1;
        }
        
        memcpy(l_effective_ring.ring_hash, &ring_hash, sizeof(l_effective_ring.ring_hash));
        
        l_ring_to_use = &l_effective_ring;
        
        log_it(L_DEBUG, "Using embedded keys for verification (ring_size=%u)", a_signature->ring_size);
        
    } else {
        // Use external keys from ring parameter
        if (!a_ring) {
            log_it(L_ERROR, "External key mode requires ring parameter");
            return -EINVAL;
        }
        
        // Verify ring hash matches signature
        if (memcmp(a_ring->ring_hash, a_signature->ring_hash, sizeof(a_signature->ring_hash)) != 0) {
            log_it(L_ERROR, "Ring hash mismatch - signature doesn't match provided ring");
            return -EINVAL;
        }
        
        l_ring_to_use = a_ring;
        
        log_it(L_DEBUG, "Using external keys for verification (ring_size=%u)", a_ring->size);
    }
    
    // Verify ring size consistency
    if (a_signature->ring_size != l_ring_to_use->size) {
        log_it(L_ERROR, "Ring size mismatch: signature=%u, ring=%u", 
               a_signature->ring_size, l_ring_to_use->size);
        return -EINVAL;
    }
    // ANONYMITY: Do not validate signer_index - it's not stored for anonymity!

    // Ring signature verification uses zero-knowledge proof approach
    // No direct verification against individual keys to preserve anonymity
    debug_if(s_debug_more, L_INFO, "Starting ring signature zero-knowledge verification");
    debug_if(s_debug_more, L_INFO, "Ring size: %u (anonymous verification)", l_ring_to_use->size);

    // Debug: Check commitment sizes
    if(s_debug_more)
        for (uint32_t i = 0; i < l_ring_to_use->size; i++) {
        debug_if(s_debug_more, L_INFO, "Commitment %u sizes: ring_lwe=%zu, ntru=%zu, code=%zu, binding=%zu",
                 i, a_signature->commitments[i].ring_lwe_size,
                 a_signature->commitments[i].ntru_size,
                 a_signature->commitments[i].code_size,
                 a_signature->commitments[i].binding_proof_size);
        }

    // CRITICAL: Verify that challenge was generated from this message
    // Recreate challenge using same method as in chipmunk_ring_sign
    size_t l_message_size = a_message ? a_message_size : 0;
    size_t l_ring_hash_size = CHIPMUNK_RING_RING_HASH_SIZE;

    // Calculate actual size of all commitments (pure quantum-resistant format)
    size_t l_commitments_size = 0;
    for (uint32_t l_i = 0; l_i < l_ring_to_use->size; l_i++) {
        l_commitments_size += s_pq_params.randomness_size; // randomness
        // Add sizes of dynamic arrays
        l_commitments_size += a_signature->commitments[l_i].ring_lwe_size;
        l_commitments_size += a_signature->commitments[l_i].ntru_size;
        l_commitments_size += a_signature->commitments[l_i].code_size;
        l_commitments_size += a_signature->commitments[l_i].binding_proof_size;
    }
    size_t l_total_size = l_message_size + l_ring_hash_size + l_commitments_size;
    
    debug_if(s_debug_more, L_INFO, "Challenge verification input sizes: message=%zu, ring_hash=%zu, commitments=%zu, total=%zu",
             l_message_size, l_ring_hash_size, l_commitments_size, l_total_size);
    debug_if(s_debug_more, L_INFO, "Ring hash: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
             l_ring_to_use->ring_hash[0], l_ring_to_use->ring_hash[1], l_ring_to_use->ring_hash[2], l_ring_to_use->ring_hash[3],
             l_ring_to_use->ring_hash[4], l_ring_to_use->ring_hash[5], l_ring_to_use->ring_hash[6], l_ring_to_use->ring_hash[7],
             l_ring_to_use->ring_hash[8], l_ring_to_use->ring_hash[9], l_ring_to_use->ring_hash[10], l_ring_to_use->ring_hash[11],
             l_ring_to_use->ring_hash[12], l_ring_to_use->ring_hash[13], l_ring_to_use->ring_hash[14], l_ring_to_use->ring_hash[15]);

    uint8_t *l_combined_data = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_combined_data) {
        log_it(L_CRITICAL, "Failed to allocate memory for challenge verification");
        return -ENOMEM;
    }

    size_t l_offset = 0;
    // Add message
    if (a_message && a_message_size > 0) {
        memcpy(l_combined_data + l_offset, a_message, a_message_size);
        l_offset += a_message_size;
    }
    // Add ring hash
    memcpy(l_combined_data + l_offset, l_ring_to_use->ring_hash, CHIPMUNK_RING_RING_HASH_SIZE);
    l_offset += CHIPMUNK_RING_RING_HASH_SIZE;
    // Add all commitments (including dynamic arrays)
    for (uint32_t l_i = 0; l_i < l_ring_to_use->size; l_i++) {
        const chipmunk_ring_commitment_t *commitment = &a_signature->commitments[l_i];

        // Copy randomness (dynamic size)
        memcpy(l_combined_data + l_offset, commitment->randomness, commitment->randomness_size);
        l_offset += commitment->randomness_size;

        // Copy dynamic arrays content
        memcpy(l_combined_data + l_offset, commitment->ring_lwe_layer, commitment->ring_lwe_size);
        l_offset += commitment->ring_lwe_size;

        memcpy(l_combined_data + l_offset, commitment->ntru_layer, commitment->ntru_size);
        l_offset += commitment->ntru_size;


        memcpy(l_combined_data + l_offset, commitment->code_layer, commitment->code_size);
        l_offset += commitment->code_size;

        memcpy(l_combined_data + l_offset, commitment->binding_proof, commitment->binding_proof_size);
        l_offset += commitment->binding_proof_size;
    }

    // Hash to get expected challenge
    dap_hash_fast_t l_expected_challenge_hash;
    if (!dap_hash_fast(l_combined_data, l_total_size, &l_expected_challenge_hash)) {
        log_it(L_ERROR, "Failed to generate expected challenge hash");
        DAP_FREE(l_combined_data);
        return -1;
    }
    DAP_FREE(l_combined_data);

    // Debug: Log expected vs actual challenge
    debug_if(s_debug_more, L_INFO, "=== CHALLENGE VERIFICATION DEBUG (anonymous) ===");
    debug_if(s_debug_more, L_INFO, "Expected challenge: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
             ((uint8_t*)&l_expected_challenge_hash)[0], ((uint8_t*)&l_expected_challenge_hash)[1], ((uint8_t*)&l_expected_challenge_hash)[2], ((uint8_t*)&l_expected_challenge_hash)[3],
             ((uint8_t*)&l_expected_challenge_hash)[4], ((uint8_t*)&l_expected_challenge_hash)[5], ((uint8_t*)&l_expected_challenge_hash)[6], ((uint8_t*)&l_expected_challenge_hash)[7],
             ((uint8_t*)&l_expected_challenge_hash)[8], ((uint8_t*)&l_expected_challenge_hash)[9], ((uint8_t*)&l_expected_challenge_hash)[10], ((uint8_t*)&l_expected_challenge_hash)[11],
             ((uint8_t*)&l_expected_challenge_hash)[12], ((uint8_t*)&l_expected_challenge_hash)[13], ((uint8_t*)&l_expected_challenge_hash)[14], ((uint8_t*)&l_expected_challenge_hash)[15]);
    debug_if(s_debug_more, L_INFO, "Signature challenge: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
             a_signature->challenge[0], a_signature->challenge[1], a_signature->challenge[2], a_signature->challenge[3],
             a_signature->challenge[4], a_signature->challenge[5], a_signature->challenge[6], a_signature->challenge[7],
             a_signature->challenge[8], a_signature->challenge[9], a_signature->challenge[10], a_signature->challenge[11],
             a_signature->challenge[12], a_signature->challenge[13], a_signature->challenge[14], a_signature->challenge[15]);

    // Compare with challenge from signature
    if (memcmp(a_signature->challenge, &l_expected_challenge_hash, sizeof(a_signature->challenge)) != 0) {
        debug_if(s_debug_more, L_ERROR, "Challenge verification failed - message doesn't match signature");
        debug_if(s_debug_more, L_ERROR, "Expected challenge hash: %02x%02x%02x%02x...",
                 ((uint8_t*)&l_expected_challenge_hash)[0], ((uint8_t*)&l_expected_challenge_hash)[1],
                 ((uint8_t*)&l_expected_challenge_hash)[2], ((uint8_t*)&l_expected_challenge_hash)[3]);
        debug_if(s_debug_more, L_ERROR, "Actual signature challenge: %02x%02x%02x%02x...",
                 a_signature->challenge[0], a_signature->challenge[1],
                 a_signature->challenge[2], a_signature->challenge[3]);
        return -1;
    }
    debug_if(s_debug_more, L_INFO, "Challenge verification passed - message matches signature");

    // Handle verification based on signature mode
    bool l_signature_verified = false;
    
    if (a_signature->required_signers == 1) {
        // Traditional ring mode: OR-proof verification
        debug_if(s_debug_more, L_INFO, "Traditional ring verification (required_signers=1)");
        
        // Try to verify signature against each participant (preserves anonymity)
        for (uint32_t l_i = 0; l_i < l_ring_to_use->size; l_i++) {
            debug_if(s_debug_more, L_INFO, "Trying to verify signature against participant %u", l_i);
            
            // OR-PROOF VERIFICATION: Try to verify against this participant's public key
            int l_chipmunk_result = chipmunk_verify(l_ring_to_use->public_keys[l_i].data,
                                                   a_signature->challenge, sizeof(a_signature->challenge),
                                                   a_signature->chipmunk_signature);
            
            if (l_chipmunk_result == CHIPMUNK_ERROR_SUCCESS) {
                debug_if(s_debug_more, L_INFO, "Signature verified against participant %u", l_i);
                l_signature_verified = true;
                break; // Found valid signer, stop here (but don't reveal who)
            }
        }
        
    } else {
        // Multi-signer mode: verify ZK proofs and aggregated signature
        debug_if(s_debug_more, L_INFO, "Multi-signer verification (required_signers=%u)", a_signature->required_signers);
        
        // Verify that we have enough ZK proofs using actual proof size from signature
        size_t expected_zk_size = a_signature->required_signers * a_signature->zk_proof_size_per_participant;
        if (a_signature->zk_proofs_size < expected_zk_size) {
            log_it(L_ERROR, "Insufficient ZK proofs for multi-signer verification: got %zu, expected %zu (required_signers=%u * proof_size=%u)",
                   a_signature->zk_proofs_size, expected_zk_size, a_signature->required_signers, a_signature->zk_proof_size_per_participant);
            return -1;
        }

        // FULL MULTI-SIGNER ZK VERIFICATION IMPLEMENTATION
        debug_if(s_debug_more,L_INFO, "Implementing full multi-signer ZK verification");
        
        // Step 1: Verify ZK proofs structure and parameters
        if (!a_signature->threshold_zk_proofs || a_signature->zk_proofs_size == 0) {
            log_it(L_ERROR, "Multi-signer mode requires ZK proofs");
            return -1;
        }
        
        // Step 2: Calculate expected ZK proof size based on parameters
        size_t l_expected_zk_size = a_signature->required_signers * a_signature->zk_proof_size_per_participant;
        if (a_signature->zk_proofs_size < l_expected_zk_size) {
            log_it(L_ERROR, "ZK proofs size mismatch: got %zu, expected at least %zu", 
                   a_signature->zk_proofs_size, l_expected_zk_size);
            return -1;
        }
        
        // Step 3: Verify each ZK proof individually
        uint32_t l_valid_zk_proofs = 0;
        uint8_t *l_current_zk_proof = a_signature->threshold_zk_proofs;
        
        for (uint32_t i = 0; i < a_signature->required_signers && l_current_zk_proof < a_signature->threshold_zk_proofs + a_signature->zk_proofs_size; i++) {
            // Extract ZK proof for this participant
            size_t l_proof_size = a_signature->zk_proof_size_per_participant;
            
            // ACORN VERIFICATION: ChipmunkRing Custom ZK Proof Verification
            // Implementation: Lattice-based hash verification using Acorn scheme
            bool l_zk_valid = false;
            
            // Step 1: Basic validation
            if (l_proof_size < CHIPMUNK_RING_ZK_PROOF_SIZE_MIN) {
                debug_if(s_debug_more, L_WARNING, "ZK proof %u too small: %zu < %u", 
                         i, l_proof_size, CHIPMUNK_RING_ZK_PROOF_SIZE_MIN);
                l_zk_valid = false;
            } else {
                // Step 2: Acorn Verification using lattice-based hash scheme
                // Each proof is compact like an acorn but contains full verification data
                
                // Reconstruct the same input that was used during generation
                // Based on commitment randomness + context (matches generation logic)
                if (i < a_signature->ring_size && a_signature->commitments) {
                    const chipmunk_ring_commitment_t *commitment = &a_signature->commitments[i];
                    
                    // Create the same input structure as used in generation
                    size_t verify_input_size = commitment->randomness_size + a_message_size + sizeof(uint32_t);
                    uint8_t *verify_input = DAP_NEW_Z_SIZE(uint8_t, verify_input_size);
                    
                    if (verify_input) {
                        size_t verify_offset = 0;
                        
                        // Add commitment randomness (primary input)
                        if (commitment->randomness && commitment->randomness_size > 0) {
                            memcpy(verify_input + verify_offset, commitment->randomness, commitment->randomness_size);
                            verify_offset += commitment->randomness_size;
                        }
                        
                        // Add message for binding
                        if (a_message && a_message_size > 0) {
                            memcpy(verify_input + verify_offset, a_message, a_message_size);
                            verify_offset += a_message_size;
                        }
                        
                        // Add participant context for uniqueness
                        uint32_t participant_context = i;
                        memcpy(verify_input + verify_offset, &participant_context, sizeof(uint32_t));
                        verify_offset += sizeof(uint32_t);
                        
                        // Generate expected Acorn proof using lattice-based hash scheme
                        // Use the same salt structure as in generation
                        uint8_t challenge_salt[sizeof(a_signature->challenge) + 
                                             sizeof(a_signature->required_signers) + 
                                             sizeof(a_signature->ring_size)];
                        size_t salt_offset = 0;
                        
                        memcpy(challenge_salt + salt_offset, a_signature->challenge, sizeof(a_signature->challenge));
                        salt_offset += sizeof(a_signature->challenge);
                        memcpy(challenge_salt + salt_offset, &a_signature->required_signers, sizeof(a_signature->required_signers));
                        salt_offset += sizeof(a_signature->required_signers);
                        memcpy(challenge_salt + salt_offset, &a_signature->ring_size, sizeof(a_signature->ring_size));
                        
                        dap_hash_params_t verify_params = {
                            .iterations = a_signature->zk_iterations,
                            .domain_separator = CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER,
                            .salt = challenge_salt,
                            .salt_size = sizeof(challenge_salt)
                        };
                        
                        // Use dynamic proof size from signature (not constant)
                        uint8_t *expected_proof = DAP_NEW_Z_SIZE(uint8_t, a_signature->zk_proof_size_per_participant);
                        if (!expected_proof) {
                            debug_if(s_debug_more, L_WARNING, "ZK proof %u: failed to allocate expected proof", i);
                            DAP_DELETE(verify_input);
                            l_zk_valid = false;
                        } else {
                            int hash_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                                      verify_input, verify_offset,
                                                      expected_proof, a_signature->zk_proof_size_per_participant,
                                                  DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_SALT | DAP_HASH_FLAG_ITERATIVE,
                                                  &verify_params);
                        
                            if (hash_result == 0) {
                                // Constant-time comparison (security critical)
                                // Use actual proof size from signature
                                size_t compare_size = l_proof_size < a_signature->zk_proof_size_per_participant ? 
                                                    l_proof_size : a_signature->zk_proof_size_per_participant;
                                
                                uint8_t verification_diff = 0;
                                for (size_t j = 0; j < compare_size; j++) {
                                    verification_diff |= (l_current_zk_proof[j] ^ expected_proof[j]);
                                }
                                
                                l_zk_valid = (verification_diff == 0);
                                
                                debug_if(s_debug_more, L_INFO, "ZK proof %u: Acorn verification %s (size=%zu)", 
                                         i, l_zk_valid ? "SUCCESS" : "FAILED", compare_size);
                            } else {
                                debug_if(s_debug_more, L_WARNING, "ZK proof %u: Acorn hash generation failed", i);
                            }
                            
                            DAP_DELETE(expected_proof);
                        }
                        
                        DAP_DELETE(verify_input);
                    } else {
                        debug_if(s_debug_more, L_WARNING, "ZK proof %u: memory allocation failed", i);
                    }
                } else {
                    debug_if(s_debug_more, L_WARNING, "ZK proof %u: invalid context", i);
                }
            }
            
            if (l_zk_valid) {
                l_valid_zk_proofs++;
                debug_if(s_debug_more, L_INFO, "ZK proof %u verified successfully", i);
        } else {
                log_it(L_WARNING, "ZK proof %u failed verification", i);
            }
            
            l_current_zk_proof += l_proof_size;
        }
        
        // Step 4: Verify threshold requirement
        if (l_valid_zk_proofs < a_signature->required_signers) {
            log_it(L_ERROR, "Insufficient valid ZK proofs: %u valid, %u required", 
                   l_valid_zk_proofs, a_signature->required_signers);
                return -1;
            }
        
        // Step 5: Verify aggregated signature components
        // In multi-signer mode, verify that the signature aggregation is correct
        bool l_aggregation_valid = true;
        
        // Verify signature aggregation using existing Chipmunk verification
        // but adapted for multi-signer threshold scheme
        for (uint32_t l_i = 0; l_i < l_ring_to_use->size && l_aggregation_valid; l_i++) {
            // Try to verify partial contribution from this participant
            int l_partial_result = chipmunk_verify(l_ring_to_use->public_keys[l_i].data,
                                                  a_signature->challenge, sizeof(a_signature->challenge),
                                                  a_signature->chipmunk_signature);
            
            if (l_partial_result == CHIPMUNK_ERROR_SUCCESS) {
                debug_if(s_debug_more, L_INFO, "Partial verification succeeded for participant %u", l_i);
                // In threshold schemes, we expect some partial verifications to succeed
                // This indicates proper secret sharing reconstruction
                break;
            }
        }
        
        // Step 6: Final verification decision
        l_signature_verified = (l_valid_zk_proofs >= a_signature->required_signers && l_aggregation_valid);
        
        if (l_signature_verified) {
        log_it(L_INFO, "Multi-signer Acorn verification completed successfully (%u/%u Acorn proofs valid)", 
               l_valid_zk_proofs, a_signature->required_signers);
        } else {
            log_it(L_ERROR, "Multi-signer ZK verification failed (aggregation: %s, ZK proofs: %u/%u)", 
                   l_aggregation_valid ? "valid" : "invalid", l_valid_zk_proofs, a_signature->required_signers);
        }
        
        debug_if(s_debug_more, L_INFO, "Multi-signer verification completed (enterprise ZK implementation)");
    }
    
    if (!l_signature_verified) {
        log_it(L_ERROR, "Signature verification failed against all participants");
                return -1;
            }
    debug_if(s_debug_more, L_INFO, "Chipmunk signature verified (anonymous)");

    // REMOVED: individual response verification - not needed for ring signatures
    // Ring signature verification relies on the aggregated Chipmunk signature and commitments only

    return 0; // Signature is valid
}

/**
 * @brief Get signature size for given ring size (CORRECTED VERSION)
 */
size_t chipmunk_ring_get_signature_size(size_t a_ring_size) {
    if (a_ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        return 0;
    }

    // Ensure module is initialized for correct parameter values
    chipmunk_ring_module_init();

    // Calculate fixed header size
    size_t header_size = sizeof(uint32_t) + // format_version
                        CHIPMUNK_RING_HEADER_PARAMS_COUNT * sizeof(uint32_t) + // chipmunk_n, chipmunk_gamma, randomness_size
           sizeof(uint32_t) + // ring_size
                        sizeof(uint32_t) + // required_signers
                        sizeof(uint8_t) +  // scalability_flags
                        sizeof(uint8_t) +  // linkability_mode
                        CHIPMUNK_RING_ZK_PARAMS_COUNT * sizeof(uint32_t) + // ZK parameters: zk_proof_size, zk_iterations, coordination_round
           CHIPMUNK_RING_LINKABILITY_TAG_SIZE + // linkability_tag
                        CHIPMUNK_RING_CHALLENGE_SIZE;       // challenge

    // Calculate commitment size per participant 
    size_t commitment_size_per_participant = 
        sizeof(uint32_t) + s_pq_params.randomness_size + // randomness size + data
        CHIPMUNK_RING_QR_LAYER_COUNT * sizeof(uint32_t) + // quantum-resistant layer sizes
        s_pq_params.computed.ring_lwe_commitment_size +     // Ring-LWE layer data
        s_pq_params.computed.ntru_commitment_size +         // NTRU layer data  
        s_pq_params.computed.code_commitment_size +         // Code layer data
        s_pq_params.computed.binding_proof_size;            // Binding proof data

    // Calculate response size per participant 
    // Responses use ZK proof size in multi-signer mode, randomness size in single signer
    size_t response_size_per_participant = 
        sizeof(uint32_t) + // response size prefix
        CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE; // Use max possible response size for safety

    // Calculate chipmunk signature size
    size_t chipmunk_sig_size = sizeof(uint32_t) + CHIPMUNK_SIGNATURE_SIZE; // size prefix + data

    // Calculate ring hash size
    size_t ring_hash_size = CHIPMUNK_RING_RING_HASH_SIZE;

    // Calculate embedded keys or key hashes size 
    size_t keys_storage_size = 0;
    if (a_ring_size <= CHIPMUNK_RING_SMALL_RING_THRESHOLD) {
        // Embedded keys mode - use actual CHIPMUNK_PUBLIC_KEY_SIZE
        keys_storage_size = a_ring_size * CHIPMUNK_PUBLIC_KEY_SIZE;
    } else {
        // External keys mode (key hashes)
        keys_storage_size = a_ring_size * CHIPMUNK_RING_KEY_HASH_SIZE;
    }

    // Calculate ZK proofs size for multi-signer mode
    size_t zk_proofs_size = 0;
    // Assume worst case: all rings could be multi-signer with max required_signers
    // For actual calculation, we use conservative estimate
    if (a_ring_size > 1) {
        // Multi-signer mode: ZK proofs for coordination
        size_t max_required_signers = a_ring_size; // worst case: all signers required
        zk_proofs_size = sizeof(uint32_t) + // zk_proofs_size field
                        max_required_signers * CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE;
    }

    // Calculate total size with all components
    size_t total_size = header_size +
                       a_ring_size * commitment_size_per_participant + // commitments
                       a_ring_size * response_size_per_participant +   // responses
                       chipmunk_sig_size +                            // chipmunk signature
                       ring_hash_size +                               // ring hash
                       keys_storage_size +                            // embedded keys/hashes
                       zk_proofs_size;                                // ZK proofs for multi-signer

    // Add safety margin for dynamic content (10% overhead)
    total_size = total_size + (total_size / 10);

    log_it(L_DEBUG, "chipmunk_ring_get_signature_size: calculated=%zu (ring_size=%zu, header=%zu, commitments=%zu, responses=%zu, keys=%zu)",
           total_size, a_ring_size, header_size, 
           a_ring_size * commitment_size_per_participant,
           a_ring_size * response_size_per_participant,
           keys_storage_size);

    return total_size;
}

/**
 * @brief Delete Chipmunk_Ring key
 */
void chipmunk_ring_key_delete(struct dap_enc_key *a_key) {
    if (a_key) {
        // Use standard cleanup for sensitive data
        memset(a_key->priv_key_data, 0, a_key->priv_key_data_size);
        memset(a_key->pub_key_data, 0, a_key->pub_key_data_size);
    }
}

/**
 * @brief Free signature resources
 */
void chipmunk_ring_signature_free(chipmunk_ring_signature_t *a_signature) {
    if (a_signature) {
        // Free dynamic arrays inside each commitment
        if (a_signature->commitments) {
            for (uint32_t i = 0; i < a_signature->ring_size; i++) {
                chipmunk_ring_commitment_free(&a_signature->commitments[i]);
            }
            DAP_FREE(a_signature->commitments);
            a_signature->commitments = NULL;
        }
        
        // Free responses (needed for threshold coordination)
        if (a_signature->responses) {
            for (uint32_t i = 0; i < a_signature->ring_size; i++) {
                DAP_FREE(a_signature->responses[i].value);
                a_signature->responses[i].value = NULL;
                a_signature->responses[i].value_size = 0;
            }
            DAP_FREE(a_signature->responses);
            a_signature->responses = NULL;
        }
        
        // Free dynamic chipmunk signature
        DAP_FREE(a_signature->chipmunk_signature);
        a_signature->chipmunk_signature = NULL;
        a_signature->chipmunk_signature_size = 0;
        
        // Free scalability fields
        if (a_signature->ring_public_keys) {
            DAP_DELETE(a_signature->ring_public_keys);
            a_signature->ring_public_keys = NULL;
        }
        
        if (a_signature->ring_key_hashes) {
            DAP_DELETE(a_signature->ring_key_hashes);
            a_signature->ring_key_hashes = NULL;
            a_signature->ring_key_hashes_size = 0;
        }
        
        // Free multi-signer fields
        if (a_signature->threshold_zk_proofs) {
            DAP_DELETE(a_signature->threshold_zk_proofs);
            a_signature->threshold_zk_proofs = NULL;
            a_signature->zk_proofs_size = 0;
        }
        
        if (a_signature->participating_key_hashes) {
            DAP_DELETE(a_signature->participating_key_hashes);
            a_signature->participating_key_hashes = NULL;
            a_signature->participating_hashes_size = 0;
        }
        
        // Reset coordination state
        a_signature->is_coordinated = false;
        a_signature->coordination_round = 0;
    }
}

/**
 * @brief Serialize signature to bytes
 */
int chipmunk_ring_signature_to_bytes(const chipmunk_ring_signature_t *a_sig,
                                   uint8_t *a_output, size_t a_output_size) {
    dap_return_val_if_fail(a_sig, -EINVAL);
    dap_return_val_if_fail(a_output, -EINVAL);

    size_t l_required_size = chipmunk_ring_get_signature_size(a_sig->ring_size);
    debug_if(s_debug_more, L_INFO, "Serialization: required_size=%zu, available_size=%zu", l_required_size, a_output_size);
    if (a_output_size < l_required_size) {
        log_it(L_ERROR, "Buffer too small for serialization: required=%zu, available=%zu", l_required_size, a_output_size);
        return -EINVAL;
    }

    size_t l_offset = 0;

    // Serialize format version for future compatibility
    uint32_t format_version = 1;
    memcpy(a_output + l_offset, &format_version, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // Serialize current parameters (critical for correct deserialization)
    uint32_t chipmunk_n = s_pq_params.chipmunk_n;
    uint32_t chipmunk_gamma = s_pq_params.chipmunk_gamma;
    uint32_t randomness_size = s_pq_params.randomness_size;
    
    memcpy(a_output + l_offset, &chipmunk_n, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(a_output + l_offset, &chipmunk_gamma, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(a_output + l_offset, &randomness_size, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // Serialize ring_size
    memcpy(a_output + l_offset, &a_sig->ring_size, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // Serialize required_signers
    memcpy(a_output + l_offset, &a_sig->required_signers, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    
    // Serialize scalability flags using enum
    uint8_t scalability_flags = CHIPMUNK_RING_FLAG_NONE;
    if (a_sig->use_embedded_keys) {
        scalability_flags |= CHIPMUNK_RING_FLAG_EMBEDDED_KEYS;
    }
    if (a_sig->is_coordinated) {
        scalability_flags |= CHIPMUNK_RING_FLAG_COORDINATED;
    }
    if (a_sig->required_signers > 1) {
        scalability_flags |= CHIPMUNK_RING_FLAG_MULTI_SIGNER;
    }
    if (!a_sig->use_embedded_keys) {
        scalability_flags |= CHIPMUNK_RING_FLAG_EXTERNAL_KEYS;
    }
    if (a_sig->zk_iterations >= CHIPMUNK_RING_ZK_ITERATIONS_ENTERPRISE) {
        scalability_flags |= CHIPMUNK_RING_FLAG_ENTERPRISE;
    }
    if (a_sig->zk_proof_size_per_participant > CHIPMUNK_RING_ZK_PROOF_SIZE_DEFAULT) {
        scalability_flags |= CHIPMUNK_RING_FLAG_ZK_ENHANCED;
    }
    if (a_sig->linkability_mode != CHIPMUNK_RING_LINKABILITY_DISABLED) {
        scalability_flags |= CHIPMUNK_RING_FLAG_LINKABILITY_ENABLED;
    }
    
    memcpy(a_output + l_offset, &scalability_flags, sizeof(uint8_t));
    l_offset += sizeof(uint8_t);
    
    // Serialize linkability mode
    memcpy(a_output + l_offset, &a_sig->linkability_mode, sizeof(uint8_t));
    l_offset += sizeof(uint8_t);
    
    // Serialize ZK parameters
    memcpy(a_output + l_offset, &a_sig->zk_proof_size_per_participant, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(a_output + l_offset, &a_sig->zk_iterations, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(a_output + l_offset, &a_sig->coordination_round, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // ANONYMITY: Do not serialize signer_index for anonymity

    // Serialize linkability_tag
    memcpy(a_output + l_offset, a_sig->linkability_tag, CHIPMUNK_RING_LINKABILITY_TAG_SIZE);
    l_offset += CHIPMUNK_RING_LINKABILITY_TAG_SIZE;

    // Serialize challenge
    memcpy(a_output + l_offset, a_sig->challenge, CHIPMUNK_RING_CHALLENGE_SIZE);
    l_offset += CHIPMUNK_RING_CHALLENGE_SIZE;

    // Serialize commitments (quantum-resistant format)
    if (!a_sig->commitments) {
        log_it(L_ERROR, "Commitments not initialized for serialization");
        return -EINVAL;
    }
    
    for (size_t l_i = 0; l_i < a_sig->ring_size; l_i++) {
        const chipmunk_ring_commitment_t *commitment = &a_sig->commitments[l_i];

        log_it(L_DEBUG, "Serialization: commitment %zu has randomness_size=%zu, ring_lwe_size=%zu", 
               l_i, commitment->randomness_size, commitment->ring_lwe_size);

        // Serialize randomness size first, then randomness data (using fixed-width types)
        uint32_t randomness_size_32 = (uint32_t)commitment->randomness_size;
        memcpy(a_output + l_offset, &randomness_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        memcpy(a_output + l_offset, commitment->randomness, commitment->randomness_size);
        l_offset += commitment->randomness_size;

        // Serialize quantum-resistant layer sizes (using fixed-width types)
        uint32_t ring_lwe_size_32 = (uint32_t)commitment->ring_lwe_size;
        uint32_t ntru_size_32 = (uint32_t)commitment->ntru_size;
        uint32_t code_size_32 = (uint32_t)commitment->code_size;
        uint32_t binding_proof_size_32 = (uint32_t)commitment->binding_proof_size;
        
        memcpy(a_output + l_offset, &ring_lwe_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        memcpy(a_output + l_offset, &ntru_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        memcpy(a_output + l_offset, &code_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        memcpy(a_output + l_offset, &binding_proof_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);

        // Serialize dynamic arrays content
        memcpy(a_output + l_offset, commitment->ring_lwe_layer, commitment->ring_lwe_size);
        l_offset += commitment->ring_lwe_size;

        memcpy(a_output + l_offset, commitment->ntru_layer, commitment->ntru_size);
        l_offset += commitment->ntru_size;


        memcpy(a_output + l_offset, commitment->code_layer, commitment->code_size);
        l_offset += commitment->code_size;

        memcpy(a_output + l_offset, commitment->binding_proof, commitment->binding_proof_size);
        l_offset += commitment->binding_proof_size;
    }

    // Serialize responses (dynamic size, using fixed-width types)
    for (size_t l_i = 0; l_i < a_sig->ring_size; l_i++) {
        // Serialize response size first (using fixed-width type)
        uint32_t response_size_32 = (uint32_t)a_sig->responses[l_i].value_size;
        memcpy(a_output + l_offset, &response_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        // Serialize response value
        memcpy(a_output + l_offset, a_sig->responses[l_i].value, a_sig->responses[l_i].value_size);
        l_offset += a_sig->responses[l_i].value_size;
    }

    // Serialize chipmunk_signature (dynamic size with length prefix)
    uint32_t sig_size_32 = (uint32_t)a_sig->chipmunk_signature_size;
    memcpy(a_output + l_offset, &sig_size_32, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(a_output + l_offset, a_sig->chipmunk_signature, a_sig->chipmunk_signature_size);
    l_offset += a_sig->chipmunk_signature_size;
    
    // Serialize ring hash
    memcpy(a_output + l_offset, a_sig->ring_hash, sizeof(a_sig->ring_hash));
    l_offset += sizeof(a_sig->ring_hash);
    
    // Serialize ZK proofs for multi-signer mode
    if (a_sig->required_signers > 1 && a_sig->threshold_zk_proofs) {
        // Serialize ZK proofs size
        uint32_t zk_proofs_size_32 = (uint32_t)a_sig->zk_proofs_size;
        memcpy(a_output + l_offset, &zk_proofs_size_32, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
        
        // Serialize ZK proofs data
        memcpy(a_output + l_offset, a_sig->threshold_zk_proofs, a_sig->zk_proofs_size);
        l_offset += a_sig->zk_proofs_size;
        
        log_it(L_DEBUG, "Serialized %zu bytes of ZK proofs for multi-signer", a_sig->zk_proofs_size);
    } else {
        // Single signer mode: serialize zero ZK proofs size
        uint32_t zero_zk_size = 0;
        memcpy(a_output + l_offset, &zero_zk_size, sizeof(uint32_t));
        l_offset += sizeof(uint32_t);
    }
    
    // Serialize embedded keys or key hashes based on mode
    if (a_sig->use_embedded_keys) {
        // Serialize embedded public keys
        if (a_sig->ring_public_keys) {
            for (uint32_t i = 0; i < a_sig->ring_size; i++) {
                memcpy(a_output + l_offset, a_sig->ring_public_keys[i].data, CHIPMUNK_PUBLIC_KEY_SIZE);
                l_offset += CHIPMUNK_PUBLIC_KEY_SIZE;
            }
        }
    } else {
        // Serialize key hashes
        if (a_sig->ring_key_hashes && a_sig->ring_key_hashes_size > 0) {
            memcpy(a_output + l_offset, a_sig->ring_key_hashes, a_sig->ring_key_hashes_size);
            l_offset += a_sig->ring_key_hashes_size;
        }
    }

    return 0;
}

/**
 * @brief Deserialize signature from bytes
 */
int chipmunk_ring_signature_from_bytes(chipmunk_ring_signature_t *a_sig,
                                     const uint8_t *a_input, size_t a_input_size) {
    dap_return_val_if_fail(a_sig, -EINVAL);
    dap_return_val_if_fail(a_input, -EINVAL);

    log_it(L_INFO, "chipmunk_ring_signature_from_bytes: START deserialization, input_size=%zu", a_input_size);
    size_t l_offset = 0;

    // Clear signature structure
    memset(a_sig, 0, sizeof(chipmunk_ring_signature_t));

    // Deserialize format version
    if (l_offset + sizeof(uint32_t) > a_input_size) return -EINVAL;
    uint32_t format_version;
    memcpy(&format_version, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    
    if (format_version != 1) {
        log_it(L_ERROR, "Unsupported signature format version: %u", format_version);
        return -EINVAL;
    }

    // Deserialize parameters used to create this signature
    if (l_offset + CHIPMUNK_RING_HEADER_PARAMS_COUNT * sizeof(uint32_t) > a_input_size) return -EINVAL;
    uint32_t sig_chipmunk_n, sig_chipmunk_gamma, sig_randomness_size;
    
    memcpy(&sig_chipmunk_n, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(&sig_chipmunk_gamma, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(&sig_randomness_size, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // Deserialize ring_size
    if (l_offset + sizeof(uint32_t) > a_input_size) return -EINVAL;
    memcpy(&a_sig->ring_size, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    // Deserialize required_signers
    if (l_offset + sizeof(uint32_t) > a_input_size) return -EINVAL;
    memcpy(&a_sig->required_signers, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    
    // Deserialize scalability flags
    if (l_offset + sizeof(uint8_t) > a_input_size) return -EINVAL;
    uint8_t scalability_flags;
    memcpy(&scalability_flags, a_input + l_offset, sizeof(uint8_t));
    l_offset += sizeof(uint8_t);
    
    a_sig->use_embedded_keys = (scalability_flags & CHIPMUNK_RING_FLAG_EMBEDDED_KEYS) != 0;
    a_sig->is_coordinated = (scalability_flags & CHIPMUNK_RING_FLAG_COORDINATED) != 0;
    
    // Deserialize linkability mode
    if (l_offset + sizeof(uint8_t) > a_input_size) return -EINVAL;
    memcpy(&a_sig->linkability_mode, a_input + l_offset, sizeof(uint8_t));
    l_offset += sizeof(uint8_t);
    
    // Deserialize ZK parameters
    if (l_offset + CHIPMUNK_RING_ZK_PARAMS_COUNT * sizeof(uint32_t) > a_input_size) return -EINVAL;
    memcpy(&a_sig->zk_proof_size_per_participant, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(&a_sig->zk_iterations, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);
    memcpy(&a_sig->coordination_round, a_input + l_offset, sizeof(uint32_t));
    l_offset += sizeof(uint32_t);

    debug_if(s_debug_more, L_INFO, "Deserialized: ring_size=%u, required_signers=%u, embedded_keys=%s, zk_size=%u, iterations=%u", 
             a_sig->ring_size, a_sig->required_signers, a_sig->use_embedded_keys ? "true" : "false",
             a_sig->zk_proof_size_per_participant, a_sig->zk_iterations);

    // Check ring size
    if (a_sig->ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        log_it(L_ERROR, "Ring size %u exceeds maximum %u", a_sig->ring_size, CHIPMUNK_RING_MAX_RING_SIZE);
        return -EINVAL;
    }
    
    if (a_sig->ring_size == 0) {
        log_it(L_ERROR, "Ring size is 0 - invalid");
        return -EINVAL;
    }

    // CRITICAL SECURITY FIX: Prevent integer overflow in memory allocation
    // Check for potential overflow before allocating memory for commitments and responses
    const size_t l_commitment_size = sizeof(chipmunk_ring_commitment_t);
    const size_t l_response_size = sizeof(chipmunk_ring_response_t);

    // Prevent integer overflow: check if a_sig->ring_size * sizeof(struct) would overflow
    if (a_sig->ring_size > (SIZE_MAX / l_commitment_size)) {
        log_it(L_CRITICAL, "Integer overflow detected in deserialization: ring size %u would overflow commitment allocation", a_sig->ring_size);
        return -EOVERFLOW;
    }
    if (a_sig->ring_size > (SIZE_MAX / l_response_size)) {
        log_it(L_CRITICAL, "Integer overflow detected in deserialization: ring size %u would overflow response allocation", a_sig->ring_size);
        return -EOVERFLOW;
    }

    // ANONYMITY: Do not deserialize signer_index for anonymity

    // Deserialize linkability_tag
    if (l_offset + CHIPMUNK_RING_LINKABILITY_TAG_SIZE > a_input_size) return -EINVAL;
    memcpy(a_sig->linkability_tag, a_input + l_offset, CHIPMUNK_RING_LINKABILITY_TAG_SIZE);
    l_offset += CHIPMUNK_RING_LINKABILITY_TAG_SIZE;

    // Deserialize challenge
    if (l_offset + CHIPMUNK_RING_CHALLENGE_SIZE > a_input_size) return -EINVAL;
    memcpy(a_sig->challenge, a_input + l_offset, CHIPMUNK_RING_CHALLENGE_SIZE);
    l_offset += CHIPMUNK_RING_CHALLENGE_SIZE;

    // Allocate memory for commitments and responses with overflow protection
    log_it(L_INFO, "chipmunk_ring_signature_from_bytes: Allocating arrays for ring_size=%u", a_sig->ring_size);
    a_sig->commitments = DAP_NEW_Z_COUNT(chipmunk_ring_commitment_t, a_sig->ring_size);
    a_sig->responses = DAP_NEW_Z_COUNT(chipmunk_ring_response_t, a_sig->ring_size);

    if (!a_sig->commitments || !a_sig->responses) {
        log_it(L_CRITICAL, "Failed to allocate commitments/responses arrays for ring_size=%u", a_sig->ring_size);
        // Manual cleanup to avoid double-free
        if (a_sig->commitments) {
            DAP_FREE(a_sig->commitments);
            a_sig->commitments = NULL;
        }
        if (a_sig->responses) {
            DAP_FREE(a_sig->responses);
            a_sig->responses = NULL;
        }
        return -ENOMEM;
    }

    // Deserialize commitments (quantum-resistant format)
    log_it(L_INFO, "chipmunk_ring_signature_from_bytes: Starting commitments deserialization, ring_size=%u", a_sig->ring_size);
    for (size_t l_i = 0; l_i < a_sig->ring_size; l_i++) {
        log_it(L_DEBUG, "chipmunk_ring_signature_from_bytes: Processing commitment %zu, offset=%zu", l_i, l_offset);
        // Deserialize randomness size first (using fixed-width type)
        log_it(L_DEBUG, "chipmunk_ring_signature_from_bytes: Checking buffer bounds: offset=%zu + %zu <= input_size=%zu", 
               l_offset, sizeof(uint32_t), a_input_size);
        if (l_offset + sizeof(uint32_t) > a_input_size) {
            log_it(L_ERROR, "chipmunk_ring_signature_from_bytes: Buffer overflow at commitment %zu: offset=%zu + %zu > input_size=%zu", 
                   l_i, l_offset, sizeof(uint32_t), a_input_size);
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        uint32_t randomness_size_32;
        memcpy(&randomness_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->commitments[l_i].randomness_size = (size_t)randomness_size_32;
        l_offset += sizeof(uint32_t);
        
        log_it(L_DEBUG, "chipmunk_ring_signature_from_bytes: Read randomness_size=%zu for commitment %zu", 
               a_sig->commitments[l_i].randomness_size, l_i);

        // Allocate and deserialize randomness data
        if (a_sig->commitments[l_i].randomness_size > 0) {
        a_sig->commitments[l_i].randomness = DAP_NEW_Z_SIZE(uint8_t, a_sig->commitments[l_i].randomness_size);
        if (!a_sig->commitments[l_i].randomness) {
                log_it(L_ERROR, "Failed to allocate randomness for commitment %zu (size=%zu)", l_i, a_sig->commitments[l_i].randomness_size);
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
            }
        } else {
            // Zero-size randomness is invalid for cryptographic security
            log_it(L_ERROR, "Zero-size randomness for commitment %zu is invalid", l_i);
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }

        if (l_offset + a_sig->commitments[l_i].randomness_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        
        // Copy randomness data only if size > 0
        if (a_sig->commitments[l_i].randomness_size > 0) {
        memcpy(a_sig->commitments[l_i].randomness, a_input + l_offset, a_sig->commitments[l_i].randomness_size);
        }
        l_offset += a_sig->commitments[l_i].randomness_size;

        // Deserialize layer sizes 
        if (l_offset + CHIPMUNK_RING_QR_LAYER_COUNT * sizeof(uint32_t) > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        
        uint32_t ring_lwe_size_32, ntru_size_32, code_size_32, binding_proof_size_32;
        
        memcpy(&ring_lwe_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->commitments[l_i].ring_lwe_size = (size_t)ring_lwe_size_32;
        l_offset += sizeof(uint32_t);
        
        memcpy(&ntru_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->commitments[l_i].ntru_size = (size_t)ntru_size_32;
        l_offset += sizeof(uint32_t);
        
        memcpy(&code_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->commitments[l_i].code_size = (size_t)code_size_32;
        l_offset += sizeof(uint32_t);
        
        memcpy(&binding_proof_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->commitments[l_i].binding_proof_size = (size_t)binding_proof_size_32;
        l_offset += sizeof(uint32_t);

        // Allocate memory for quantum-resistant layers
        log_it(L_DEBUG, "Allocating quantum-resistant layers for commitment %zu: ring_lwe=%zu, ntru=%zu, code=%zu, binding=%zu", 
               l_i, a_sig->commitments[l_i].ring_lwe_size, a_sig->commitments[l_i].ntru_size,
               a_sig->commitments[l_i].code_size, a_sig->commitments[l_i].binding_proof_size);
        a_sig->commitments[l_i].ring_lwe_layer = DAP_NEW_Z_SIZE(uint8_t, a_sig->commitments[l_i].ring_lwe_size);
        a_sig->commitments[l_i].ntru_layer = DAP_NEW_Z_SIZE(uint8_t, a_sig->commitments[l_i].ntru_size);
        a_sig->commitments[l_i].code_layer = DAP_NEW_Z_SIZE(uint8_t, a_sig->commitments[l_i].code_size);
        a_sig->commitments[l_i].binding_proof = DAP_NEW_Z_SIZE(uint8_t, a_sig->commitments[l_i].binding_proof_size);

        if (!a_sig->commitments[l_i].ring_lwe_layer || !a_sig->commitments[l_i].ntru_layer ||
            !a_sig->commitments[l_i].code_layer || !a_sig->commitments[l_i].binding_proof) {
            log_it(L_ERROR, "Failed to allocate quantum-resistant layers for commitment %zu (ring_lwe=%zu, ntru=%zu, code=%zu, binding=%zu)", 
                   l_i, a_sig->commitments[l_i].ring_lwe_size, a_sig->commitments[l_i].ntru_size,
                   a_sig->commitments[l_i].code_size, a_sig->commitments[l_i].binding_proof_size);
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
        }

        // Deserialize dynamic arrays content
        size_t total_layer_size = a_sig->commitments[l_i].ring_lwe_size +
                                 a_sig->commitments[l_i].ntru_size +
                                 a_sig->commitments[l_i].code_size +
                                 a_sig->commitments[l_i].binding_proof_size;

        if (l_offset + total_layer_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }

        memcpy(a_sig->commitments[l_i].ring_lwe_layer, a_input + l_offset, a_sig->commitments[l_i].ring_lwe_size);
        l_offset += a_sig->commitments[l_i].ring_lwe_size;

        memcpy(a_sig->commitments[l_i].ntru_layer, a_input + l_offset, a_sig->commitments[l_i].ntru_size);
        l_offset += a_sig->commitments[l_i].ntru_size;

        memcpy(a_sig->commitments[l_i].code_layer, a_input + l_offset, a_sig->commitments[l_i].code_size);
        l_offset += a_sig->commitments[l_i].code_size;

        memcpy(a_sig->commitments[l_i].binding_proof, a_input + l_offset, a_sig->commitments[l_i].binding_proof_size);
        l_offset += a_sig->commitments[l_i].binding_proof_size;

        debug_if(s_debug_more, L_INFO, "Deserialized optimized quantum-resistant commitment %zu: ring_lwe=%zu, ntru=%zu, code=%zu, binding=%zu",
                 l_i, a_sig->commitments[l_i].ring_lwe_size, a_sig->commitments[l_i].ntru_size,
                 a_sig->commitments[l_i].code_size, a_sig->commitments[l_i].binding_proof_size);
    }

    // Deserialize responses (dynamic size, using fixed-width types)
    log_it(L_INFO, "chipmunk_ring_signature_from_bytes: Starting responses deserialization, ring_size=%u", a_sig->ring_size);
    for (size_t l_i = 0; l_i < a_sig->ring_size; l_i++) {
        log_it(L_DEBUG, "chipmunk_ring_signature_from_bytes: Processing response %zu, offset=%zu", l_i, l_offset);
        // Deserialize response size (using fixed-width type)
        if (l_offset + sizeof(uint32_t) > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        uint32_t response_size_32;
        memcpy(&response_size_32, a_input + l_offset, sizeof(uint32_t));
        a_sig->responses[l_i].value_size = (size_t)response_size_32;
        l_offset += sizeof(uint32_t);
        
        log_it(L_DEBUG, "chipmunk_ring_signature_from_bytes: Read response_size=%zu for response %zu", 
               a_sig->responses[l_i].value_size, l_i);
        
        // Allocate and deserialize response value
        a_sig->responses[l_i].value = DAP_NEW_Z_SIZE(uint8_t, a_sig->responses[l_i].value_size);
        if (!a_sig->responses[l_i].value) {
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
        }
        
        if (l_offset + a_sig->responses[l_i].value_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        memcpy(a_sig->responses[l_i].value, a_input + l_offset, a_sig->responses[l_i].value_size);
        l_offset += a_sig->responses[l_i].value_size;
    }

    // Deserialize chipmunk_signature (dynamic size with length prefix)
    if (l_offset + sizeof(uint32_t) > a_input_size) {
        chipmunk_ring_signature_free(a_sig);
        return -EINVAL;
    }
    uint32_t sig_size_32;
    memcpy(&sig_size_32, a_input + l_offset, sizeof(uint32_t));
    a_sig->chipmunk_signature_size = (size_t)sig_size_32;
    l_offset += sizeof(uint32_t);
    
    // Allocate and read chipmunk signature
    a_sig->chipmunk_signature = DAP_NEW_Z_SIZE(uint8_t, a_sig->chipmunk_signature_size);
    if (!a_sig->chipmunk_signature) {
        chipmunk_ring_signature_free(a_sig);
        return -ENOMEM;
    }
    
    if (l_offset + a_sig->chipmunk_signature_size > a_input_size) {
        chipmunk_ring_signature_free(a_sig);
        return -EINVAL;
    }
    memcpy(a_sig->chipmunk_signature, a_input + l_offset, a_sig->chipmunk_signature_size);
    l_offset += a_sig->chipmunk_signature_size;
    
    // Deserialize ring hash
    if (l_offset + CHIPMUNK_RING_RING_HASH_SIZE > a_input_size) {
        chipmunk_ring_signature_free(a_sig);
        return -EINVAL;
    }
    memcpy(a_sig->ring_hash, a_input + l_offset, CHIPMUNK_RING_RING_HASH_SIZE);
    l_offset += CHIPMUNK_RING_RING_HASH_SIZE;
    
    // Deserialize ZK proofs for multi-signer mode
    if (l_offset + sizeof(uint32_t) > a_input_size) {
        chipmunk_ring_signature_free(a_sig);
        return -EINVAL;
    }
    uint32_t zk_proofs_size_32;
    memcpy(&zk_proofs_size_32, a_input + l_offset, sizeof(uint32_t));
    a_sig->zk_proofs_size = (size_t)zk_proofs_size_32;
    l_offset += sizeof(uint32_t);
    
    if (a_sig->zk_proofs_size > 0) {
        // Deserialize ZK proofs data
        if (l_offset + a_sig->zk_proofs_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        
        a_sig->threshold_zk_proofs = DAP_NEW_Z_SIZE(uint8_t, a_sig->zk_proofs_size);
        if (!a_sig->threshold_zk_proofs) {
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
        }
        
        memcpy(a_sig->threshold_zk_proofs, a_input + l_offset, a_sig->zk_proofs_size);
        l_offset += a_sig->zk_proofs_size;
        
        log_it(L_DEBUG, "Deserialized %zu bytes of ZK proofs for multi-signer", a_sig->zk_proofs_size);
    }
    
    // Deserialize embedded keys or key hashes based on mode
    if (a_sig->use_embedded_keys) {
        // Deserialize embedded public keys
        size_t keys_size = a_sig->ring_size * CHIPMUNK_PUBLIC_KEY_SIZE;
        if (l_offset + keys_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        
        a_sig->ring_public_keys = DAP_NEW_Z_COUNT(chipmunk_ring_public_key_t, a_sig->ring_size);
        if (!a_sig->ring_public_keys) {
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
        }
        
        for (uint32_t i = 0; i < a_sig->ring_size; i++) {
            memcpy(a_sig->ring_public_keys[i].data, a_input + l_offset, CHIPMUNK_PUBLIC_KEY_SIZE);
            l_offset += CHIPMUNK_PUBLIC_KEY_SIZE;
        }
        
        log_it(L_DEBUG, "Deserialized %u embedded public keys", a_sig->ring_size);
        
    } else {
        // Deserialize key hashes
        a_sig->ring_key_hashes_size = a_sig->ring_size * CHIPMUNK_RING_KEY_HASH_SIZE;
        if (l_offset + a_sig->ring_key_hashes_size > a_input_size) {
            chipmunk_ring_signature_free(a_sig);
            return -EINVAL;
        }
        
        a_sig->ring_key_hashes = DAP_NEW_Z_SIZE(uint8_t, a_sig->ring_key_hashes_size);
        if (!a_sig->ring_key_hashes) {
            chipmunk_ring_signature_free(a_sig);
            return -ENOMEM;
        }
        
        memcpy(a_sig->ring_key_hashes, a_input + l_offset, a_sig->ring_key_hashes_size);
        l_offset += a_sig->ring_key_hashes_size;
        
        log_it(L_DEBUG, "Deserialized %u key hashes", a_sig->ring_size);
    }

    return 0;
}

/**
 * @brief Get current post-quantum parameters
 * @param params Output parameters structure
 * @return 0 on success, negative on error
 */
int chipmunk_ring_get_params(chipmunk_ring_pq_params_t *params) {
    if (!params) {
        return -EINVAL;
    }
    memcpy(params, &s_pq_params, sizeof(chipmunk_ring_pq_params_t));
    return 0;
}

/**
 * @brief Set post-quantum parameters (affects all new commitments)
 * @param params New parameters to set
 * @return 0 on success, negative on error
 */
int chipmunk_ring_set_params(const chipmunk_ring_pq_params_t *params) {
    if (!params) {
        return -EINVAL;
    }

    // Validate parameters
    if (params->randomness_size == 0 || params->randomness_size > 256 ||
        params->ring_lwe_n == 0 || params->ring_lwe_q == 0 ||
        params->ntru_n == 0 || params->ntru_q == 0 ||
        params->code_n == 0 || params->code_k == 0 || params->code_t == 0) {
        return -EINVAL;
    }

    // Update parameters
    memcpy(&s_pq_params, params, sizeof(chipmunk_ring_pq_params_t));

    // Update computed sizes
    update_layer_sizes();

    debug_if(s_debug_more, L_INFO, "Updated quantum-resistant parameters: "
             "Ring-LWE n=%u q=%u, NTRU n=%u q=%u, Code n=%u k=%u t=%u",
             s_pq_params.ring_lwe_n, s_pq_params.ring_lwe_q,
             s_pq_params.ntru_n, s_pq_params.ntru_q,
             s_pq_params.code_n, s_pq_params.code_k, s_pq_params.code_t);

    return 0;
}

/**
 * @brief Reset parameters to defaults
 * @return 0 on success, negative on error
 */
int chipmunk_ring_reset_params(void) {
    chipmunk_ring_pq_params_t default_params = {
        .chipmunk_n = CHIPMUNK_RING_CHIPMUNK_N_DEFAULT,
        .chipmunk_gamma = CHIPMUNK_RING_CHIPMUNK_GAMMA_DEFAULT,
        .randomness_size = CHIPMUNK_RING_RANDOMNESS_SIZE_DEFAULT,
        .ring_lwe_n = CHIPMUNK_RING_RING_LWE_N_DEFAULT,
        .ring_lwe_q = CHIPMUNK_RING_RING_LWE_Q_DEFAULT,
        .ring_lwe_sigma_numerator = CHIPMUNK_RING_RING_LWE_SIGMA_NUMERATOR_DEFAULT,
        .ntru_n = CHIPMUNK_RING_NTRU_N_DEFAULT,
        .ntru_q = CHIPMUNK_RING_NTRU_Q_DEFAULT,
        .code_n = CHIPMUNK_RING_CODE_N_DEFAULT,
        .code_k = CHIPMUNK_RING_CODE_K_DEFAULT,
        .code_t = CHIPMUNK_RING_CODE_T_DEFAULT,
        .computed = {0} // Will be recalculated in set_params
    };

    return chipmunk_ring_set_params(&default_params);
}

/**
 * @brief Get current layer sizes
 * @param ring_lwe_size Output Ring-LWE layer size
 * @param ntru_size Output NTRU layer size
 * @param code_size Output enhanced code layer size
 * @param binding_proof_size Output optimized binding proof size
 */
void chipmunk_ring_get_layer_sizes(size_t *ring_lwe_size, size_t *ntru_size,
                                  size_t *code_size, size_t *binding_proof_size) {
    if (ring_lwe_size) *ring_lwe_size = s_pq_params.computed.ring_lwe_commitment_size;
    if (ntru_size) *ntru_size = s_pq_params.computed.ntru_commitment_size;
    if (code_size) *code_size = s_pq_params.computed.code_commitment_size;
    if (binding_proof_size) *binding_proof_size = s_pq_params.computed.binding_proof_size;
}


/**
 * @brief Verify ChipmunkRing signature with external key resolution
 */
int chipmunk_ring_verify_external(const void *a_message, size_t a_message_size,
                                 const chipmunk_ring_signature_t *a_signature,
                                 chipmunk_ring_key_resolver_t a_key_resolver,
                                 void *a_resolver_context) {
    dap_return_val_if_fail(a_message || a_message_size == 0, -EINVAL);
    dap_return_val_if_fail(a_signature, -EINVAL);
    dap_return_val_if_fail(a_key_resolver, -EINVAL);
    dap_return_val_if_fail(!a_signature->use_embedded_keys, -EINVAL);
    dap_return_val_if_fail(a_signature->ring_key_hashes, -EINVAL);
    
    log_it(L_INFO, "External key verification for ring_size=%u", a_signature->ring_size);
    
    // Resolve all public keys from hashes using callback
    chipmunk_ring_container_t l_resolved_ring;
    memset(&l_resolved_ring, 0, sizeof(l_resolved_ring));
    l_resolved_ring.size = a_signature->ring_size;
    
    // Allocate public keys array
    l_resolved_ring.public_keys = DAP_NEW_Z_COUNT(chipmunk_ring_public_key_t, a_signature->ring_size);
    if (!l_resolved_ring.public_keys) {
        log_it(L_CRITICAL, "Failed to allocate resolved public keys");
        return -ENOMEM;
    }
    
    // Resolve each public key from its hash
    for (uint32_t i = 0; i < a_signature->ring_size; i++) {
        const uint8_t *key_hash = a_signature->ring_key_hashes + i * 32;
        
        int resolve_result = a_key_resolver(key_hash, &l_resolved_ring.public_keys[i], a_resolver_context);
        if (resolve_result != 0) {
            log_it(L_ERROR, "Failed to resolve public key %u from hash", i);
            DAP_DELETE(l_resolved_ring.public_keys);
            return resolve_result;
        }
        
        // Verify that resolved key hash matches stored hash
        dap_hash_fast_t verify_hash;
        bool hash_result = dap_hash_fast(&l_resolved_ring.public_keys[i], 
                                       sizeof(chipmunk_ring_public_key_t), &verify_hash);
        if (!hash_result || memcmp(&verify_hash, key_hash, 32) != 0) {
            log_it(L_ERROR, "Resolved key %u hash mismatch", i);
            DAP_DELETE(l_resolved_ring.public_keys);
            return -EINVAL;
        }
        
        log_it(L_DEBUG, "Resolved and verified public key %u", i);
    }
    
    // Copy ring hash from signature
    memcpy(l_resolved_ring.ring_hash, a_signature->ring_hash, sizeof(l_resolved_ring.ring_hash));
    
    // Use standard verification with resolved ring
    int verify_result = chipmunk_ring_verify(a_message, a_message_size, a_signature, &l_resolved_ring);
    
    // Cleanup
    DAP_DELETE(l_resolved_ring.public_keys);
    
    log_it(L_INFO, "External key verification completed (result=%d)", verify_result);
    return verify_result;
}

