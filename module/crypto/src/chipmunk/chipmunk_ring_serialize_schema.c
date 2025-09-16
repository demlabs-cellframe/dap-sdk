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
#include "chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"
#include "dap_common.h"

#define LOG_TAG "chipmunk_ring_serialize"

// Debug flag for detailed logging
static bool s_debug_more = false;

// Size helpers for parameter-based size calculation of nested fields
static size_t s_size_acorn_proof(const void *a_object, void *a_context)
{
    UNUSED(a_object);
    UNUSED(a_context);
    // Use enterprise-grade size as conservative estimate to avoid under-allocation
    return (size_t)CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE; // 96 by default
}

static size_t s_size_randomness(const void *a_object, void *a_context)
{
    UNUSED(a_object);
    UNUSED(a_context);
    return (size_t)CHIPMUNK_RING_RANDOMNESS_SIZE_DEFAULT; // 32 bytes
}

static size_t s_size_linkability_tag(const void *a_object, void *a_context)
{
    UNUSED(a_object);
    UNUSED(a_context);
    return (size_t)CHIPMUNK_RING_LINKABILITY_TAG_SIZE; // 32 bytes
}


// Schema definitions for helper structures
static const dap_serialize_field_t s_challenge_salt_fields[] = {
    {
        .name = "challenge",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_challenge_salt_t, challenge),
        .size_offset = offsetof(chipmunk_ring_challenge_salt_t, challenge_size)
    },
    {
        .name = "required_signers",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_challenge_salt_t, required_signers),
        .size = sizeof(uint32_t)
    },
    {
        .name = "ring_size",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_challenge_salt_t, ring_size),
        .size = sizeof(uint32_t)
    }
};

static const dap_serialize_field_t s_acorn_input_fields[] = {
    {
        .name = "public_key",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_acorn_input_t, public_key),
        .size = CHIPMUNK_PUBLIC_KEY_SIZE
    },
    {
        .name = "message",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_acorn_input_t, message),
        .size_offset = offsetof(chipmunk_ring_acorn_input_t, message_size)
    },
    {
        .name = "randomness",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_acorn_input_t, randomness),
        .size_offset = offsetof(chipmunk_ring_acorn_input_t, randomness_size)
    }
};

static const dap_serialize_field_t s_combined_data_fields[] = {
    {
        .name = "message",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_combined_data_t, message),
        .size_offset = offsetof(chipmunk_ring_combined_data_t, message_size)
    },
    {
        .name = "ring_hash",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_combined_data_t, ring_hash),
        .size_offset = offsetof(chipmunk_ring_combined_data_t, ring_hash_size)
    },
    {
        .name = "acorn_proofs",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_combined_data_t, acorn_proofs),
        .count_offset = offsetof(chipmunk_ring_combined_data_t, acorn_proofs_count),
        .nested_schema = &chipmunk_ring_acorn_schema
    }
};

// Schema definitions
DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_challenge_salt_schema, 
                           chipmunk_ring_challenge_salt_t, 
                           s_challenge_salt_fields);

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_acorn_input_schema, 
                           chipmunk_ring_acorn_input_t, 
                           s_acorn_input_fields);

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_combined_data_schema, 
                           chipmunk_ring_combined_data_t, 
                           s_combined_data_fields);


static const dap_serialize_field_t s_proof_input_fields[] = {
    {
        .name = "ring_private_key",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_proof_input_t, ring_private_key),
        .size = sizeof(chipmunk_ring_private_key_t)
    },
    {
        .name = "required_signers",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_proof_input_t, required_signers),
        .size = sizeof(uint32_t)
    },
    {
        .name = "total_participants",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_proof_input_t, total_participants),
        .size = sizeof(uint32_t)
    }
};

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_proof_input_schema, 
                           chipmunk_ring_proof_input_t, 
                           s_proof_input_fields);

static const dap_serialize_field_t s_response_input_fields[] = {
    {
        .name = "randomness",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_response_input_t, randomness),
        .size_offset = offsetof(chipmunk_ring_response_input_t, randomness_size)
    },
    {
        .name = "message",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_response_input_t, message),
        .size_offset = offsetof(chipmunk_ring_response_input_t, message_size)
    },
    {
        .name = "participant_context",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_response_input_t, participant_context),
        .size = sizeof(uint32_t)
    }
};

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_response_input_schema, 
                           chipmunk_ring_response_input_t, 
                           s_response_input_fields);

static const dap_serialize_field_t s_linkability_input_fields[] = {
    {
        .name = "ring_hash",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_linkability_input_t, ring_hash),
        .size_offset = offsetof(chipmunk_ring_linkability_input_t, ring_hash_size)
    },
    {
        .name = "message",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_linkability_input_t, message),
        .size_offset = offsetof(chipmunk_ring_linkability_input_t, message_size)
    },
    {
        .name = "challenge",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_linkability_input_t, challenge),
        .size_offset = offsetof(chipmunk_ring_linkability_input_t, challenge_size)
    }
};

DAP_SERIALIZE_SCHEMA_DEFINE(chipmunk_ring_linkability_input_schema, 
                           chipmunk_ring_linkability_input_t, 
                           s_linkability_input_fields);

// Field definitions for Acorn verification structure
static const dap_serialize_field_t s_chipmunk_ring_acorn_fields[] = {
    // Acorn proof (dynamic size)
    {
        .name = "acorn_proof",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_acorn_t, acorn_proof),
        .size_offset = offsetof(chipmunk_ring_acorn_t, acorn_proof_size),
        .size_func = s_size_acorn_proof
    },
    
    // Randomness (dynamic size)
    {
        .name = "randomness", 
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_SECURE_CLEAR,
        .offset = offsetof(chipmunk_ring_acorn_t, randomness),
        .size_offset = offsetof(chipmunk_ring_acorn_t, randomness_size),
        .size_func = s_size_randomness
    },
    
    // Linkability tag (dynamic size)
    {
        .name = "linkability_tag",
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_acorn_t, linkability_tag),
        .size_offset = offsetof(chipmunk_ring_acorn_t, linkability_tag_size),
        .size_func = s_size_linkability_tag
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
    
    // Ring public keys array (conditional - only if use_embedded_keys is true)
    {
        .name = "ring_public_keys",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_CONDITIONAL,
        .offset = offsetof(chipmunk_ring_signature_t, ring_public_keys),
        .count_offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .size = sizeof(chipmunk_ring_public_key_t),
        .condition = chipmunk_ring_has_embedded_keys
    },
    
    // Acorn proofs array (dynamic count) - CRITICAL: needed for ChipmunkRing functionality
    {
        .name = "acorn_proofs",
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_ring_signature_t, acorn_proofs),
        .count_offset = offsetof(chipmunk_ring_signature_t, ring_size),
        .nested_schema = &chipmunk_ring_acorn_schema
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
    
    // Safe check without assertion for condition functions
    if (!a_object) {
        debug_if(s_debug_more, L_DEBUG, "chipmunk_ring_has_embedded_keys: NULL object, returning false");
        return false;
    }
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t*)a_object;
    
    // Additional safety check for structure validity
    if (l_signature->ring_size == 0 || l_signature->ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        debug_if(s_debug_more, L_DEBUG, "chipmunk_ring_has_embedded_keys: invalid ring_size=%u, returning false", l_signature->ring_size);
        return false;
    }
    
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
