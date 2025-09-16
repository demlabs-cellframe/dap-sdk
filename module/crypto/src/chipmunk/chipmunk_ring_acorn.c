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

#include "chipmunk_ring_acorn.h"
#include "chipmunk_ring.h"
#include "chipmunk_ring_serialize_schema.h"
#include "dap_common.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"

#include <errno.h>

#define LOG_TAG "chipmunk_ring_acorn"



bool s_debug_more = false;


/**
 * @brief Free memory allocated for commitment dynamic arrays
 */
void chipmunk_ring_acorn_free(chipmunk_ring_acorn_t *a_acorn) {
    if (a_acorn) {
        // Free Acorn proof with secure cleanup
        if (a_acorn->acorn_proof) {
            memset(a_acorn->acorn_proof, 0, a_acorn->acorn_proof_size);
            DAP_FREE(a_acorn->acorn_proof);
            a_acorn->acorn_proof = NULL;
        }
        
        // Free randomness with secure cleanup
        if (a_acorn->randomness) {
            memset(a_acorn->randomness, 0, a_acorn->randomness_size);
            DAP_FREE(a_acorn->randomness);
            a_acorn->randomness = NULL;
        }
        
        // Free linkability tag with secure cleanup
        if (a_acorn->linkability_tag) {
            memset(a_acorn->linkability_tag, 0, a_acorn->linkability_tag_size);
            DAP_FREE(a_acorn->linkability_tag);
            a_acorn->linkability_tag = NULL;
        }
        
        // Reset sizes to zero
        a_acorn->randomness_size = 0;
        a_acorn->acorn_proof_size = 0;
        a_acorn->linkability_tag_size = 0;
    }
}

/**
 * @brief Create quantum-resistant commitment for ZKP (always deterministic for anonymity)
 */
int chipmunk_ring_acorn_create(chipmunk_ring_acorn_t *a_acorn,
                               const chipmunk_ring_public_key_t *a_public_key,
                               const void *a_message, size_t a_message_size,
                               size_t a_randomness_size,
                               size_t a_acorn_proof_size,
                               size_t a_linkability_tag_size,
                               uint32_t a_zk_iterations) {
    dap_return_val_if_fail(a_acorn, -EINVAL);
    dap_return_val_if_fail(a_public_key, -EINVAL);

    debug_if(s_debug_more, L_DEBUG, "chipmunk_ring_acorn_create: Using parameters - randomness_size=%zu, acorn_proof_size=%zu", 
           a_randomness_size, a_acorn_proof_size);

    // PURE ACORN STRUCTURE: Use passed parameters from context
    a_acorn->randomness_size = a_randomness_size;
    a_acorn->acorn_proof_size = a_acorn_proof_size;
    a_acorn->linkability_tag_size = a_linkability_tag_size;
    
    // Allocate memory for Acorn verification components
    a_acorn->randomness = DAP_NEW_Z_SIZE(uint8_t, a_acorn->randomness_size);
    a_acorn->acorn_proof = DAP_NEW_Z_SIZE(uint8_t, a_acorn->acorn_proof_size);
    a_acorn->linkability_tag = DAP_NEW_Z_SIZE(uint8_t, a_acorn->linkability_tag_size);
    
    if (!a_acorn->randomness || !a_acorn->acorn_proof || !a_acorn->linkability_tag) {
        log_it(L_CRITICAL, "Failed to allocate memory for Acorn commitment");
        chipmunk_ring_acorn_free(a_acorn);
        return -ENOMEM;
    }

    // ACORN PROOF GENERATION: Generate Acorn proof in ring_lwe_layer
    // Generate random seed for true randomness
    uint8_t participant_seed[64];
    uint8_t random_bytes[32];
    randombytes(random_bytes, sizeof(random_bytes));
    
    // Combine public key pointer, message size and random bytes for unique seed
    snprintf((char*)participant_seed, sizeof(participant_seed), "acorn_%p_%zu_", 
             (void*)a_public_key, a_message_size);
    size_t prefix_len = strlen((char*)participant_seed);
    if (prefix_len + sizeof(random_bytes) <= sizeof(participant_seed)) {
        memcpy(participant_seed + prefix_len, random_bytes, sizeof(random_bytes));
    }
    
    // Generate randomness of exact required size using dap_hash with domain separation
    dap_hash_params_t randomness_params = {
        .domain_separator = CHIPMUNK_RING_DOMAIN_ACORN_RANDOMNESS
    };
    // Use full seed length including random bytes
    size_t seed_len = (prefix_len + sizeof(random_bytes) <= sizeof(participant_seed)) ? 
                      prefix_len + sizeof(random_bytes) : sizeof(participant_seed);
    int randomness_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                    participant_seed, seed_len,
                                    a_acorn->randomness, a_acorn->randomness_size,
                                    DAP_HASH_FLAG_DOMAIN_SEPARATION, &randomness_params);
    if (randomness_result != 0) {
        log_it(L_ERROR, "Failed to generate participant randomness: %d", randomness_result);
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }

    // PURE ACORN COMMITMENT GENERATION
    // Generate Acorn proof for this participant
    
    // Prepare input for Acorn proof using universal serializer
    chipmunk_ring_acorn_input_t l_acorn_input_data = {
        .message = (uint8_t*)a_message,
        .message_size = a_message ? a_message_size : 0,
        .randomness = a_acorn->randomness,
        .randomness_size = a_acorn->randomness_size
    };
    memcpy(l_acorn_input_data.public_key, a_public_key->data, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    size_t acorn_input_size = dap_serialize_calc_size(&chipmunk_ring_acorn_input_schema, NULL, &l_acorn_input_data, NULL);
    uint8_t *acorn_input = DAP_NEW_SIZE(uint8_t, acorn_input_size);
    if (!acorn_input) {
        log_it(L_ERROR, "Failed to allocate Acorn input buffer");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    dap_serialize_result_t l_input_result = dap_serialize_to_buffer(&chipmunk_ring_acorn_input_schema, &l_acorn_input_data, acorn_input, acorn_input_size, NULL);
    if (l_input_result.error_code != DAP_SERIALIZE_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize Acorn input: %s", l_input_result.error_message);
        DAP_DELETE(acorn_input);
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    acorn_input_size = l_input_result.bytes_written;
    
    // Generate Acorn proof using parameterized iterations from signature context
    dap_hash_params_t l_acorn_params = {
        .iterations = a_zk_iterations, // Use context parameter instead of hardcoded maximum
        .domain_separator = CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT
    };
    
    int l_acorn_result = dap_hash(DAP_HASH_TYPE_SHAKE256, acorn_input, acorn_input_size,
                                 a_acorn->acorn_proof, a_acorn->acorn_proof_size,
                                 DAP_HASH_FLAG_ITERATIVE | DAP_HASH_FLAG_DOMAIN_SEPARATION, &l_acorn_params);
    DAP_DELETE(acorn_input);
    
    if (l_acorn_result != 0) {
        log_it(L_ERROR, "Failed to generate Acorn proof for commitment");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    // Generate linkability tag of exact required size using dap_hash with domain separation
    dap_hash_params_t linkability_params = {
        .domain_separator = CHIPMUNK_RING_DOMAIN_ACORN_LINKABILITY
    };
    int linkability_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                     a_public_key->data, CHIPMUNK_PUBLIC_KEY_SIZE,
                                     a_acorn->linkability_tag, a_acorn->linkability_tag_size,
                                     DAP_HASH_FLAG_DOMAIN_SEPARATION, &linkability_params);
    if (linkability_result != 0) {
        log_it(L_ERROR, "Failed to generate linkability tag: %d", linkability_result);
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }

    debug_if(false, L_INFO, "Acorn created successfully ");
    return 0;
}