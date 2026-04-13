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

// Argument indices for ChipmunkRing parametric calculations (for performance)
typedef enum chipmunk_ring_arg_index {
    CHIPMUNK_RING_ARG_RING_SIZE = 0,           ///< Ring size argument
    CHIPMUNK_RING_ARG_USE_EMBEDDED_KEYS = 1,   ///< Use embedded keys flag
    CHIPMUNK_RING_ARG_REQUIRED_SIGNERS = 2,    ///< Required signers count
    CHIPMUNK_RING_ARG_COUNT = 3                ///< Total number of arguments
} chipmunk_ring_arg_index_t;

// Helper structures for universal serialization
typedef struct chipmunk_ring_challenge_salt {
    uint8_t *challenge;
    size_t challenge_size;
    uint32_t required_signers;
    uint32_t ring_size;
} chipmunk_ring_challenge_salt_t;

typedef struct chipmunk_ring_acorn_input {
    uint8_t public_key[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t *message;
    size_t message_size;
    uint8_t *randomness;
    size_t randomness_size;
} chipmunk_ring_acorn_input_t;

typedef struct chipmunk_ring_combined_data {
    uint8_t *message;
    size_t message_size;
    uint8_t *ring_hash;
    size_t ring_hash_size;
    chipmunk_ring_acorn_t *acorn_proofs;
    uint32_t acorn_proofs_count;
} chipmunk_ring_combined_data_t;

typedef struct chipmunk_ring_proof_input {
    chipmunk_ring_private_key_t ring_private_key;
    uint32_t required_signers;
    uint32_t total_participants;
} chipmunk_ring_proof_input_t;

typedef struct chipmunk_ring_response_input {
    uint8_t *randomness;
    size_t randomness_size;
    uint8_t *message;
    size_t message_size;
    uint32_t participant_context;
} chipmunk_ring_response_input_t;

typedef struct chipmunk_ring_linkability_input {
    uint8_t *ring_hash;
    size_t ring_hash_size;
    uint8_t *message;
    size_t message_size;
    uint8_t *challenge;
    size_t challenge_size;
} chipmunk_ring_linkability_input_t;

/**
 * @brief Serialization schemas for ChipmunkRing structures
 * @details Demonstrates usage of universal serializer with complex cryptographic structures
 */

// Forward declarations - will be defined in chipmunk_ring.c
extern const dap_serialize_schema_t chipmunk_ring_acorn_schema;
extern const dap_serialize_schema_t chipmunk_ring_signature_schema;
extern const dap_serialize_schema_t chipmunk_ring_container_schema;
extern const dap_serialize_schema_t chipmunk_ring_challenge_salt_schema;
extern const dap_serialize_schema_t chipmunk_ring_acorn_input_schema;
extern const dap_serialize_schema_t chipmunk_ring_combined_data_schema;
extern const dap_serialize_schema_t chipmunk_ring_proof_input_schema;
extern const dap_serialize_schema_t chipmunk_ring_response_input_schema;
extern const dap_serialize_schema_t chipmunk_ring_linkability_input_schema;

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

// Field definitions moved to chipmunk_ring.c to avoid multiple definitions

// Schema definitions will be in chipmunk_ring.c to avoid multiple definitions

/**
 * @brief Example usage of the universal serializer
 */

// Serialize signature to buffer
static inline dap_serialize_result_t chipmunk_ring_signature_serialize(
    const chipmunk_ring_signature_t *a_signature,
    uint8_t *a_buffer,
    size_t a_buffer_size) 
{
    // Create parameters from signature for proper serialization
    // This ensures dynamic fields like threshold_zk_proofs are handled correctly
    dap_serialize_arg_t l_args[CHIPMUNK_RING_ARG_COUNT];
    l_args[CHIPMUNK_RING_ARG_RING_SIZE] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->ring_size, 
        .type = 0
    };
    l_args[CHIPMUNK_RING_ARG_USE_EMBEDDED_KEYS] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->use_embedded_keys ? 1 : 0, 
        .type = 0
    };
    l_args[CHIPMUNK_RING_ARG_REQUIRED_SIGNERS] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->required_signers, 
        .type = 0
    };
    
    dap_serialize_size_params_t l_params = {
        .field_count = 0,
        .array_counts = NULL,
        .data_sizes = NULL,
        .field_present = NULL,
        .args = l_args,
        .args_count = CHIPMUNK_RING_ARG_COUNT
    };
    
    // Pass parameters through context for parametric functions
    return dap_serialize_to_buffer(&chipmunk_ring_signature_schema,
                                   a_signature,
                                   a_buffer,
                                   a_buffer_size,
                                   &l_params);
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
    // Create parameters from signature for accurate size calculation
    dap_serialize_arg_t l_args[CHIPMUNK_RING_ARG_COUNT];
    l_args[CHIPMUNK_RING_ARG_RING_SIZE] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->ring_size, 
        .type = 0
    };
    l_args[CHIPMUNK_RING_ARG_USE_EMBEDDED_KEYS] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->use_embedded_keys ? 1 : 0, 
        .type = 0
    };
    l_args[CHIPMUNK_RING_ARG_REQUIRED_SIGNERS] = (dap_serialize_arg_t){
        .value.uint_value = a_signature->required_signers, 
        .type = 0
    };
    
    dap_serialize_size_params_t l_params = {
        .field_count = 0,
        .array_counts = NULL,
        .data_sizes = NULL,
        .field_present = NULL,
        .args = l_args,
        .args_count = CHIPMUNK_RING_ARG_COUNT
    };
    
    return dap_serialize_calc_size(&chipmunk_ring_signature_schema,
                                   &l_params,
                                   a_signature,
                                   &l_params);
}
