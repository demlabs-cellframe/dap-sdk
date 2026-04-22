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
#include "dap_rand.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"

#include <errno.h>

#define LOG_TAG "chipmunk_ring_acorn"

static bool s_debug_more = false;

/**
 * @brief Internal helper for domain-separated SHA3-256 hashing with XOF-like expansion
 * @details For outputs > 32 bytes, uses counter mode expansion (HKDF-Expand style).
 */
static int s_domain_hash(const char *a_domain, 
                        const void *a_salt, size_t a_salt_size,
                        const void *a_input, size_t a_input_size,
                        void *a_output, size_t a_output_size,
                        uint32_t a_iterations)
{
    if (!a_domain || !a_input || !a_output || a_input_size == 0 || a_output_size == 0)
        return -1;
    
    size_t domain_len = strlen(a_domain);
    
    // Phase 1: Create PRK from input
    size_t prk_input_size = domain_len + a_salt_size + a_input_size;
    uint8_t *prk_input = DAP_NEW_SIZE(uint8_t, prk_input_size);
    if (!prk_input) return -ENOMEM;
    
    size_t offset = 0;
    memcpy(prk_input + offset, a_domain, domain_len);
    offset += domain_len;
    if (a_salt && a_salt_size > 0) {
        memcpy(prk_input + offset, a_salt, a_salt_size);
        offset += a_salt_size;
    }
    memcpy(prk_input + offset, a_input, a_input_size);
    
    uint8_t prk[32];
    if (!dap_hash(DAP_HASH_TYPE_SHA3_256, prk_input, prk_input_size, prk, sizeof(prk))) {
        DAP_DELETE(prk_input);
        return -1;
    }
    DAP_DELETE(prk_input);
    
    // Phase 2: Key stretching
    uint32_t iterations = a_iterations > 0 ? a_iterations : 1;
    for (uint32_t i = 1; i < iterations; i++) {
        dap_hash(DAP_HASH_TYPE_SHA3_256, prk, sizeof(prk), prk, sizeof(prk));
    }
    
    // Phase 3: Counter mode expansion
    uint8_t *out = (uint8_t *)a_output;
    size_t remaining = a_output_size;
    uint8_t counter = 1;
    uint8_t prev_block[32] = {0};
    
    while (remaining > 0) {
        uint8_t expand_input[32 + 32 + 1];
        size_t expand_len = 0;
        
        memcpy(expand_input + expand_len, prk, 32);
        expand_len += 32;
        
        if (counter > 1) {
            memcpy(expand_input + expand_len, prev_block, 32);
            expand_len += 32;
        }
        
        expand_input[expand_len] = counter;
        expand_len += 1;
        
        uint8_t block[32];
        if (!dap_hash(DAP_HASH_TYPE_SHA3_256, expand_input, expand_len, block, sizeof(block))) {
            memset(prk, 0, sizeof(prk));
            return -1;
        }
        
        size_t to_copy = remaining < 32 ? remaining : 32;
        memcpy(out, block, to_copy);
        memcpy(prev_block, block, 32);
        
        out += to_copy;
        remaining -= to_copy;
        counter++;
        
        if (counter == 0) {
            memset(prk, 0, sizeof(prk));
            return -1;
        }
    }
    
    memset(prk, 0, sizeof(prk));
    return 0;
}


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
    dap_random_bytes(random_bytes, sizeof(random_bytes));
    
    // Combine public key pointer, message size and random bytes for unique seed
    snprintf((char*)participant_seed, sizeof(participant_seed), "acorn_%p_%zu_", 
             (void*)a_public_key, a_message_size);
    size_t prefix_len = strlen((char*)participant_seed);
    if (prefix_len + sizeof(random_bytes) <= sizeof(participant_seed)) {
        memcpy(participant_seed + prefix_len, random_bytes, sizeof(random_bytes));
    }
    
    // Generate randomness of exact required size using domain-separated SHA3-256
    // Use full seed length including random bytes
    size_t seed_len = (prefix_len + sizeof(random_bytes) <= sizeof(participant_seed)) ? 
                      prefix_len + sizeof(random_bytes) : sizeof(participant_seed);
    int randomness_result = s_domain_hash(CHIPMUNK_RING_DOMAIN_ACORN_RANDOMNESS,
                                          NULL, 0,
                                          participant_seed, seed_len,
                                          a_acorn->randomness, a_acorn->randomness_size,
                                          1);
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
    memcpy(l_acorn_input_data.public_key, a_public_key->data, CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    
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
    int l_acorn_result = s_domain_hash(CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT,
                                       NULL, 0,
                                       acorn_input, acorn_input_size,
                                       a_acorn->acorn_proof, a_acorn->acorn_proof_size,
                                       a_zk_iterations);
    DAP_DELETE(acorn_input);
    
    if (l_acorn_result != 0) {
        log_it(L_ERROR, "Failed to generate Acorn proof for commitment");
        chipmunk_ring_acorn_free(a_acorn);
        return -1;
    }
    
    /* CR-D8 fix (Round-3): the previous per-acorn "linkability tag" was
     * SHA3-256(CHIPMUNK_RING_DOMAIN_ACORN_LINKABILITY || pk_i) — i.e. a
     * deterministic function of the ring slot's public key.  Every
     * observer could recompute that value from the ring alone and then
     * recognise the same ring slot across signatures, which is the exact
     * opposite of the unlinkability an anonymous ring signature should
     * provide.  It was also never verified.
     *
     * Producing a proper linkability tag requires a sigma-protocol bound
     * to the signer's secret (e.g. tag = H(sk_signer || session_ctx) with
     * a ZK proof of knowledge); that redesign is tracked under CR-11.
     * Until then we zero the slot so it carries no information and leaks
     * nothing about ring position; wire layout is preserved (size and
     * offset unchanged) so this fix does not rev the signature format
     * beyond what CR-D* already break on the feature/chipmunk-ring branch.
     */
    if (a_acorn->linkability_tag && a_acorn->linkability_tag_size > 0) {
        memset(a_acorn->linkability_tag, 0, a_acorn->linkability_tag_size);
    }

    debug_if(false, L_INFO, "Acorn created successfully ");
    return 0;
}