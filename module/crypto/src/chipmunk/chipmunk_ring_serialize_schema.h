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

#include "dap_serialize.h"
#include "chipmunk_ring.h"

/**
 * @brief Serialization schemas for ChipmunkRing structures
 * @details Demonstrates usage of universal serializer with complex cryptographic structures
 */

// Forward declarations
extern const dap_serialize_schema_t chipmunk_ring_acorn_schema;
extern const dap_serialize_schema_t chipmunk_ring_signature_schema;
extern const dap_serialize_schema_t chipmunk_ring_container_schema;

/**
 * @brief Condition function for embedded keys
 * @details Checks if signature uses embedded keys vs external key resolution
 */
bool chipmunk_ring_has_embedded_keys(const void *a_object, void *a_context);

/**
 * @brief Condition function for threshold signatures
 * @details Checks if signature is threshold (multi-signer) vs single signer
 */
bool chipmunk_ring_is_threshold_signature(const void *a_object, void *a_context);

/**
 * @brief Size calculation for dynamic challenge field
 */
size_t chipmunk_ring_get_challenge_size(const void *a_object, void *a_context);

/**
 * @brief Size calculation for dynamic ring hash
 */
size_t chipmunk_ring_get_ring_hash_size(const void *a_object, void *a_context);

// Example schema definition for Acorn verification structure
static const dap_serialize_field_t chipmunk_ring_acorn_fields[] = {
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

// Example schema definition for ChipmunkRing signature
static const dap_serialize_field_t chipmunk_ring_signature_fields[] = {
    // Format version for compatibility
    {
        .name = "format_version",
        .type = DAP_SERIALIZE_TYPE_VERSION,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .size = sizeof(uint32_t)
    },
    
    // Ring size
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_ring_signature_t, ring_size, DAP_SERIALIZE_TYPE_UINT32),
    
    // Required signers (threshold)
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_ring_signature_t, required_signers, DAP_SERIALIZE_TYPE_UINT32),
    
    // Embedded keys flag
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_ring_signature_t, use_embedded_keys, DAP_SERIALIZE_TYPE_UINT8),
    
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
    
    // Acorn proofs array (dynamic count)
    {
        .name = "acorn_proofs",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, acorn_proofs),
        .count_offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .nested_schema = &chipmunk_ring_acorn_schema
    },
    
    // Embedded public keys (conditional - only if use_embedded_keys is true)
    {
        .name = "ring_public_keys",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_OPTIONAL,
        .offset = offsetof(chipmunk_ring_signature_t, ring_public_keys),
        .count_offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .size = sizeof(chipmunk_ring_public_key_t),
        .condition = chipmunk_ring_has_embedded_keys
    },
    
    // Threshold ZK proofs (conditional - only for threshold signatures)
    {
        .name = "threshold_zk_proofs",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_OPTIONAL,
        .offset = offsetof(chipmunk_ring_signature_t, threshold_zk_proofs),
        .size_offset = offsetof(chipmunk_ring_signature_t, zk_proof_size_per_participant),
        .condition = chipmunk_ring_is_threshold_signature
    },
    
    // Coordination fields (conditional - only for threshold signatures)
    {
        .name = "coordination_round",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_OPTIONAL,
        .offset = offsetof(chipmunk_ring_signature_t, coordination_round),
        .condition = chipmunk_ring_is_threshold_signature
    },
    
    {
        .name = "is_coordinated",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_OPTIONAL,
        .offset = offsetof(chipmunk_ring_signature_t, is_coordinated),
        .condition = chipmunk_ring_is_threshold_signature
    },
    
    // Linkability tag (dynamic size)
    {
        .name = "linkability_tag",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, linkability_tag),
        .size_offset = offsetof(chipmunk_ring_signature_t, linkability_tag_size)
    },
    
    // ZK proof parameters
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_ring_signature_t, zk_iterations, DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_ring_signature_t, zk_proof_size_per_participant, DAP_SERIALIZE_TYPE_UINT64),
    
    // Checksum for data integrity (automatically calculated)
    {
        .name = "checksum",
        .type = DAP_SERIALIZE_TYPE_CHECKSUM,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .size = 32  // SHA256 hash
    }
};

// Schema definitions
DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_acorn_schema, 
                           chipmunk_ring_acorn_t, 
                           chipmunk_ring_acorn_fields);

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_signature_schema, 
                           chipmunk_ring_signature_t, 
                           chipmunk_ring_signature_fields);

/**
 * @brief Example usage of the universal serializer
 */

// Serialize signature to buffer
static inline dap_serialize_result_t chipmunk_ring_signature_serialize(
    const chipmunk_ring_signature_t *a_signature,
    uint8_t *a_buffer,
    size_t a_buffer_size) 
{
    return dap_serialize_to_buffer(&chipmunk_ring_signature_schema,
                                   a_signature,
                                   a_buffer,
                                   a_buffer_size,
                                   NULL);
}

// Deserialize signature from buffer  
static inline dap_serialize_result_t chipmunk_ring_signature_deserialize(
    const uint8_t *a_buffer,
    size_t a_buffer_size,
    chipmunk_ring_signature_t *a_signature)
{
    return dap_serialize_from_buffer(&chipmunk_ring_signature_schema,
                                     a_buffer,
                                     a_buffer_size,
                                     a_signature,
                                     NULL);
}

// Calculate required buffer size
static inline size_t chipmunk_ring_signature_calc_size(
    const chipmunk_ring_signature_t *a_signature)
{
    return dap_serialize_calc_size(&chipmunk_ring_signature_schema,
                                   a_signature,
                                   NULL);
}
