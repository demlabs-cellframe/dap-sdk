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
        
        // Clear linkability tag
        memset(a_acorn->linkability_tag, 0, a_acorn->linkability_tag_size);
        
        // Reset sizes to zero
        a_acorn->randomness_size = 0;
        a_acorn->acorn_proof_size = 0;
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
                               size_t a_linkability_tag_size) {
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
    // Use deterministic randomness for participant identification
    uint8_t participant_seed[64];
    snprintf((char*)participant_seed, sizeof(participant_seed), "acorn_participant_%p_%zu", 
             (void*)a_public_key, a_message_size);
    
    dap_hash_fast_t randomness_hash;
    if (!dap_hash_fast(participant_seed, strlen((char*)participant_seed), &randomness_hash)) {
        log_it(L_ERROR, "Failed to generate participant randomness");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    memcpy(a_acorn->randomness, &randomness_hash, a_acorn->randomness_size);

    // PURE ACORN COMMITMENT GENERATION
    // Generate Acorn proof for this participant
    
    // Prepare input for Acorn proof: public_key || message || randomness
    size_t acorn_input_size = CHIPMUNK_PUBLIC_KEY_SIZE + 
                             (a_message ? a_message_size : 0) + 
                             a_acorn->randomness_size;
    uint8_t *acorn_input = DAP_NEW_SIZE(uint8_t, acorn_input_size);
    if (!acorn_input) {
        log_it(L_ERROR, "Failed to allocate Acorn input buffer");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    size_t offset = 0;
    memcpy(acorn_input + offset, a_public_key->data, CHIPMUNK_PUBLIC_KEY_SIZE);
    offset += CHIPMUNK_PUBLIC_KEY_SIZE;
    
    if (a_message && a_message_size > 0) {
        memcpy(acorn_input + offset, a_message, a_message_size);
        offset += a_message_size;
    }
    
    memcpy(acorn_input + offset, a_acorn->randomness, a_acorn->randomness_size);
    
    // Generate Acorn proof using parameterized iterations 
    dap_hash_params_t l_acorn_params = {
        .iterations = CHIPMUNK_RING_ZK_ITERATIONS_MAX,
        .domain_separator = "ACORN_COMMITMENT_V1"
    };
    
    int l_acorn_result = dap_hash(DAP_HASH_TYPE_SHAKE256, acorn_input, acorn_input_size,
                                 a_acorn->acorn_proof, a_acorn->acorn_proof_size,
                                 DAP_HASH_FLAG_ITERATIVE, &l_acorn_params);
    DAP_DELETE(acorn_input);
    
    if (l_acorn_result != 0) {
        log_it(L_ERROR, "Failed to generate Acorn proof for commitment");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    // Generate linkability tag for replay protection
    dap_hash_fast_t linkability_hash;
    if (dap_hash_fast(a_public_key->data, CHIPMUNK_PUBLIC_KEY_SIZE, &linkability_hash)) {
        memcpy(a_acorn->linkability_tag, &linkability_hash, 
               (sizeof(linkability_hash) < a_acorn->linkability_tag_size) ? 
               sizeof(linkability_hash) : a_acorn->linkability_tag_size);
    } else {
        log_it(L_ERROR, "Failed to generate linkability tag");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }

    debug_if(false, L_INFO, "Quantum-resistant commitment created successfully (deterministic)");
    return 0;
}