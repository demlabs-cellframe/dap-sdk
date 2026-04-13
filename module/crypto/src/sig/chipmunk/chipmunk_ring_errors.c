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

#include "chipmunk_ring_errors.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "chipmunk_ring_errors"

/**
 * @brief Convert ChipmunkRing error code to human-readable string
 */
const char* chipmunk_ring_error_to_string(chipmunk_ring_error_t error_code) {
    switch (error_code) {
        // Success
        case CHIPMUNK_RING_SUCCESS:
            return "Success";
            
        // Parameter validation errors
        case CHIPMUNK_RING_ERROR_NULL_PARAM:
            return "NULL parameter passed to function";
        case CHIPMUNK_RING_ERROR_INVALID_PARAM:
            return "Invalid parameter value";
        case CHIPMUNK_RING_ERROR_INVALID_SIZE:
            return "Invalid size parameter";
        case CHIPMUNK_RING_ERROR_INVALID_RING_SIZE:
            return "Ring size out of valid range [2, 64]";
        case CHIPMUNK_RING_ERROR_INVALID_THRESHOLD:
            return "Invalid threshold (must be 1 <= t <= ring_size)";
        case CHIPMUNK_RING_ERROR_BUFFER_TOO_SMALL:
            return "Output buffer too small";
        case CHIPMUNK_RING_ERROR_INVALID_KEY_SIZE:
            return "Key size doesn't match expected size";
        case CHIPMUNK_RING_ERROR_INVALID_MESSAGE_SIZE:
            return "Message size invalid";
            
        // Memory management errors
        case CHIPMUNK_RING_ERROR_MEMORY_ALLOC:
            return "Memory allocation failed";
        case CHIPMUNK_RING_ERROR_MEMORY_OVERFLOW:
            return "Integer overflow in memory calculation";
        case CHIPMUNK_RING_ERROR_MEMORY_CORRUPTION:
            return "Memory corruption detected";
            
        // Cryptographic errors
        case CHIPMUNK_RING_ERROR_HASH_FAILED:
            return "Hash operation failed";
        case CHIPMUNK_RING_ERROR_SIGNATURE_FAILED:
            return "Signature creation failed";
        case CHIPMUNK_RING_ERROR_VERIFY_FAILED:
            return "Signature verification failed";
        case CHIPMUNK_RING_ERROR_ZK_PROOF_FAILED:
            return "ZK proof generation/verification failed";
        case CHIPMUNK_RING_ERROR_COMMITMENT_FAILED:
            return "Commitment creation failed";
        case CHIPMUNK_RING_ERROR_RESPONSE_FAILED:
            return "Response creation failed";
        case CHIPMUNK_RING_ERROR_CHALLENGE_FAILED:
            return "Challenge generation failed";
        case CHIPMUNK_RING_ERROR_SECRET_SHARING_FAILED:
            return "Secret sharing operation failed";
        case CHIPMUNK_RING_ERROR_RECONSTRUCTION_FAILED:
            return "Secret reconstruction failed";
        case CHIPMUNK_RING_ERROR_THRESHOLD_FAILED:
            return "Threshold operation failed";
            
        // Serialization errors
        case CHIPMUNK_RING_ERROR_SERIALIZATION_FAILED:
            return "Serialization failed";
        case CHIPMUNK_RING_ERROR_DESERIALIZATION_FAILED:
            return "Deserialization failed";
        case CHIPMUNK_RING_ERROR_INVALID_FORMAT:
            return "Invalid data format";
        case CHIPMUNK_RING_ERROR_VERSION_MISMATCH:
            return "Version mismatch in serialized data";
        case CHIPMUNK_RING_ERROR_CHECKSUM_FAILED:
            return "Checksum verification failed";
            
        // Initialization errors
        case CHIPMUNK_RING_ERROR_NOT_INITIALIZED:
            return "Module not initialized";
        case CHIPMUNK_RING_ERROR_ALREADY_INITIALIZED:
            return "Module already initialized";
        case CHIPMUNK_RING_ERROR_INIT_FAILED:
            return "Initialization failed";
        case CHIPMUNK_RING_ERROR_INVALID_STATE:
            return "Invalid internal state";
            
        // Ring-specific errors
        case CHIPMUNK_RING_ERROR_SIGNER_NOT_IN_RING:
            return "Signer not found in ring";
        case CHIPMUNK_RING_ERROR_RING_TOO_SMALL:
            return "Ring size too small (minimum 2)";
        case CHIPMUNK_RING_ERROR_RING_TOO_LARGE:
            return "Ring size exceeds maximum";
        case CHIPMUNK_RING_ERROR_DUPLICATE_KEYS:
            return "Duplicate keys in ring";
        case CHIPMUNK_RING_ERROR_ANONYMITY_VIOLATED:
            return "Anonymity property violated";
        case CHIPMUNK_RING_ERROR_LINKABILITY_FAILED:
            return "Linkability check failed";
            
        // Coordination errors
        case CHIPMUNK_RING_ERROR_COORDINATION_FAILED:
            return "Multi-signer coordination failed";
        case CHIPMUNK_RING_ERROR_INSUFFICIENT_SIGNERS:
            return "Not enough signers participated";
        case CHIPMUNK_RING_ERROR_TIMEOUT:
            return "Operation timeout";
        case CHIPMUNK_RING_ERROR_PROTOCOL_VIOLATION:
            return "Protocol violation detected";
            
        // Security errors
        case CHIPMUNK_RING_ERROR_SECURITY_VIOLATION:
            return "Security policy violation";
        case CHIPMUNK_RING_ERROR_REPLAY_ATTACK:
            return "Replay attack detected";
        case CHIPMUNK_RING_ERROR_TIMING_ATTACK:
            return "Timing attack vulnerability";
        case CHIPMUNK_RING_ERROR_SIDE_CHANNEL:
            return "Side-channel vulnerability";
            
        // System errors
        case CHIPMUNK_RING_ERROR_SYSTEM:
            return "System error";
        case CHIPMUNK_RING_ERROR_NOT_SUPPORTED:
            return "Operation not supported";
        case CHIPMUNK_RING_ERROR_COMPATIBILITY:
            return "Compatibility issue";
        case CHIPMUNK_RING_ERROR_DEPRECATED:
            return "Function deprecated";
            
        default:
            return "Unknown error";
    }
}

/**
 * @brief Check if error code indicates a critical failure
 */
bool chipmunk_ring_error_is_critical(chipmunk_ring_error_t error_code) {
    switch (error_code) {
        case CHIPMUNK_RING_ERROR_MEMORY_ALLOC:
        case CHIPMUNK_RING_ERROR_MEMORY_OVERFLOW:
        case CHIPMUNK_RING_ERROR_MEMORY_CORRUPTION:
        case CHIPMUNK_RING_ERROR_SECURITY_VIOLATION:
        case CHIPMUNK_RING_ERROR_REPLAY_ATTACK:
        case CHIPMUNK_RING_ERROR_TIMING_ATTACK:
        case CHIPMUNK_RING_ERROR_SIDE_CHANNEL:
        case CHIPMUNK_RING_ERROR_ANONYMITY_VIOLATED:
        case CHIPMUNK_RING_ERROR_INIT_FAILED:
        case CHIPMUNK_RING_ERROR_SYSTEM:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if error code is related to memory management
 */
bool chipmunk_ring_error_is_memory_related(chipmunk_ring_error_t error_code) {
    return (error_code <= CHIPMUNK_RING_ERROR_MEMORY_ALLOC && 
            error_code >= CHIPMUNK_RING_ERROR_MEMORY_CORRUPTION);
}

/**
 * @brief Check if error code is related to cryptographic operations
 */
bool chipmunk_ring_error_is_crypto_related(chipmunk_ring_error_t error_code) {
    return (error_code <= CHIPMUNK_RING_ERROR_HASH_FAILED && 
            error_code >= CHIPMUNK_RING_ERROR_THRESHOLD_FAILED);
}

/**
 * @brief Log error with appropriate level based on error severity
 */
void chipmunk_ring_log_error(chipmunk_ring_error_t error_code, 
                            const char* function_name, 
                            const char* additional_info) {
    if (!function_name) function_name = "unknown_function";
    if (!additional_info) additional_info = "";
    
    const char* error_message = chipmunk_ring_error_to_string(error_code);
    
    if (chipmunk_ring_error_is_critical(error_code)) {
        log_it(L_CRITICAL, "[%s] CRITICAL ERROR %d: %s. %s", 
               function_name, error_code, error_message, additional_info);
    } else if (chipmunk_ring_error_is_memory_related(error_code)) {
        log_it(L_ERROR, "[%s] MEMORY ERROR %d: %s. %s", 
               function_name, error_code, error_message, additional_info);
    } else if (chipmunk_ring_error_is_crypto_related(error_code)) {
        log_it(L_ERROR, "[%s] CRYPTO ERROR %d: %s. %s", 
               function_name, error_code, error_message, additional_info);
    } else if (error_code <= CHIPMUNK_RING_ERROR_INVALID_MESSAGE_SIZE) {
        log_it(L_WARNING, "[%s] VALIDATION ERROR %d: %s. %s", 
               function_name, error_code, error_message, additional_info);
    } else {
        log_it(L_ERROR, "[%s] ERROR %d: %s. %s", 
               function_name, error_code, error_message, additional_info);
    }
}
