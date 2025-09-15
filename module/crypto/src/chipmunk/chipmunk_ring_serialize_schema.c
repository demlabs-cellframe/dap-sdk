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

#include "chipmunk_ring_serialize_schema.h"

#define LOG_TAG "chipmunk_ring_serialize"

// Field definitions for Acorn verification structure
static const dap_serialize_field_t s_chipmunk_ring_acorn_fields[] = {
    // Acorn proof (dynamic size)
    {
        .name = "acorn_proof",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_acorn_t, acorn_proof),
        .size_offset = offsetof(chipmunk_ring_acorn_t, acorn_proof_size)
    },
    
    // Randomness (dynamic size)
    {
        .name = "randomness", 
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_acorn_t, randomness),
        .size_offset = offsetof(chipmunk_ring_acorn_t, randomness_size)
    },
    
    // Linkability tag (dynamic size)
    {
        .name = "linkability_tag",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_acorn_t, linkability_tag),
        .size_offset = offsetof(chipmunk_ring_acorn_t, linkability_tag_size)
    }
};

// Field definitions for ChipmunkRing signature
static const dap_serialize_field_t s_chipmunk_ring_signature_fields[] = {
    // Format version for compatibility
    {
        .name = "format_version",
        .type = DAP_SERIALIZE_TYPE_VERSION,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .size = sizeof(uint32_t)
    },
    
    // Ring size
    {
        .name = "ring_size",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .size = sizeof(uint32_t)
    },
    
    // Required signers (threshold)
    {
        .name = "required_signers",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, required_signers),
        .size = sizeof(uint32_t)
    },
    
    // Embedded keys flag
    {
        .name = "use_embedded_keys",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, use_embedded_keys),
        .size = sizeof(uint8_t)
    },
    
    // Challenge (dynamic size)
    {
        .name = "challenge",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, challenge),
        .size_offset = offsetof(chipmunk_ring_signature_t, challenge_size)
    },
    
    // Ring hash (dynamic size)
    {
        .name = "ring_hash",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, ring_hash),
        .size_offset = offsetof(chipmunk_ring_signature_t, ring_hash_size)
    },
    
    // Core signature (dynamic size)
    {
        .name = "signature",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, signature),
        .size_offset = offsetof(chipmunk_ring_signature_t, signature_size)
    },
    
    // Acorn proofs array (dynamic count) - DISABLED: causes use-after-free in serializer
    /*{
        .name = "acorn_proofs",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, acorn_proofs),
        .count_offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .nested_schema = &chipmunk_ring_acorn_schema
    },*/
    
    // Linkability tag (dynamic size)
    {
        .name = "linkability_tag",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, linkability_tag),
        .size_offset = offsetof(chipmunk_ring_signature_t, linkability_tag_size)
    },
    
    // ZK proof parameters
    {
        .name = "zk_iterations",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, zk_iterations),
        .size = sizeof(uint32_t)
    },
    
    {
        .name = "zk_proof_size_per_participant",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, zk_proof_size_per_participant),
        .size = sizeof(uint64_t)
    }
};

// Global schema definitions for serialization
DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_acorn_schema, 
                           chipmunk_ring_acorn_t, 
                           s_chipmunk_ring_acorn_fields);

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_signature_schema, 
                           chipmunk_ring_signature_t, 
                           s_chipmunk_ring_signature_fields);

/**
 * @brief Check if signature uses embedded keys
 * @param a_object Pointer to chipmunk_ring_signature_t structure
 * @param a_context User context (unused)
 * @return true if embedded keys are used, false otherwise
 */
bool chipmunk_ring_has_embedded_keys(const void *a_object, void *a_context) {
    UNUSED(a_context);
    dap_return_val_if_fail(a_object, false);
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t*)a_object;
    return l_signature->use_embedded_keys;
}

/**
 * @brief Check if signature is threshold (multi-signer)
 * @param a_object Pointer to chipmunk_ring_signature_t structure
 * @param a_context User context (unused)
 * @return true if threshold signature, false for single signer
 */
bool chipmunk_ring_is_threshold_signature(const void *a_object, void *a_context) {
    UNUSED(a_context);
    dap_return_val_if_fail(a_object, false);
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t*)a_object;
    return l_signature->required_signers > 1;
}

/**
 * @brief Get challenge size from signature
 * @param a_object Pointer to chipmunk_ring_signature_t structure
 * @param a_context User context (unused)
 * @return Challenge size in bytes
 */
size_t chipmunk_ring_get_challenge_size(const void *a_object, void *a_context) {
    UNUSED(a_context);
    dap_return_val_if_fail(a_object, CHIPMUNK_RING_CHALLENGE_SIZE);
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t*)a_object;
    return l_signature->challenge_size;
}

/**
 * @brief Get ring hash size from signature
 * @param a_object Pointer to chipmunk_ring_signature_t structure
 * @param a_context User context (unused)
 * @return Ring hash size in bytes
 */
size_t chipmunk_ring_get_ring_hash_size(const void *a_object, void *a_context) {
    UNUSED(a_context);
    dap_return_val_if_fail(a_object, CHIPMUNK_RING_RING_HASH_SIZE);
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t*)a_object;
    return l_signature->ring_hash_size;
}
