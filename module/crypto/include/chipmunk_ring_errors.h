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

#include <errno.h>
#include <stdbool.h>

/**
 * @brief Comprehensive error codes for ChipmunkRing operations
 * @details Unified error handling system following DAP SDK standards
 */
typedef enum {
    // Success codes
    CHIPMUNK_RING_SUCCESS = 0,                    ///< Operation completed successfully
    
    // Parameter validation errors (-1 to -20)
    CHIPMUNK_RING_ERROR_NULL_PARAM = -1,          ///< NULL parameter passed to function
    CHIPMUNK_RING_ERROR_INVALID_PARAM = -2,       ///< Invalid parameter value
    CHIPMUNK_RING_ERROR_INVALID_SIZE = -3,        ///< Invalid size parameter
    CHIPMUNK_RING_ERROR_INVALID_RING_SIZE = -4,   ///< Ring size out of valid range [2, 64]
    CHIPMUNK_RING_ERROR_INVALID_THRESHOLD = -5,   ///< Invalid threshold (must be 1 <= t <= ring_size)
    CHIPMUNK_RING_ERROR_BUFFER_TOO_SMALL = -6,    ///< Output buffer too small
    CHIPMUNK_RING_ERROR_INVALID_KEY_SIZE = -7,    ///< Key size doesn't match expected size
    CHIPMUNK_RING_ERROR_INVALID_MESSAGE_SIZE = -8, ///< Message size invalid
    
    // Memory management errors (-21 to -30)
    CHIPMUNK_RING_ERROR_MEMORY_ALLOC = -21,       ///< Memory allocation failed
    CHIPMUNK_RING_ERROR_MEMORY_OVERFLOW = -22,    ///< Integer overflow in memory calculation
    CHIPMUNK_RING_ERROR_MEMORY_CORRUPTION = -23,  ///< Memory corruption detected
    
    // Cryptographic errors (-31 to -50)
    CHIPMUNK_RING_ERROR_HASH_FAILED = -31,        ///< Hash operation failed
    CHIPMUNK_RING_ERROR_SIGNATURE_FAILED = -32,   ///< Signature creation failed
    CHIPMUNK_RING_ERROR_VERIFY_FAILED = -33,      ///< Signature verification failed
    CHIPMUNK_RING_ERROR_ZK_PROOF_FAILED = -34,    ///< ZK proof generation/verification failed
    CHIPMUNK_RING_ERROR_COMMITMENT_FAILED = -35,  ///< Commitment creation failed
    CHIPMUNK_RING_ERROR_RESPONSE_FAILED = -36,    ///< Response creation failed
    CHIPMUNK_RING_ERROR_CHALLENGE_FAILED = -37,   ///< Challenge generation failed
    CHIPMUNK_RING_ERROR_SECRET_SHARING_FAILED = -38, ///< Secret sharing operation failed
    CHIPMUNK_RING_ERROR_RECONSTRUCTION_FAILED = -39, ///< Secret reconstruction failed
    CHIPMUNK_RING_ERROR_THRESHOLD_FAILED = -40,   ///< Threshold operation failed
    
    // Serialization/Deserialization errors (-51 to -60)
    CHIPMUNK_RING_ERROR_SERIALIZATION_FAILED = -51, ///< Serialization failed
    CHIPMUNK_RING_ERROR_DESERIALIZATION_FAILED = -52, ///< Deserialization failed
    CHIPMUNK_RING_ERROR_INVALID_FORMAT = -53,     ///< Invalid data format
    CHIPMUNK_RING_ERROR_VERSION_MISMATCH = -54,   ///< Version mismatch in serialized data
    CHIPMUNK_RING_ERROR_CHECKSUM_FAILED = -55,    ///< Checksum verification failed
    
    // Initialization and state errors (-61 to -70)
    CHIPMUNK_RING_ERROR_NOT_INITIALIZED = -61,    ///< Module not initialized
    CHIPMUNK_RING_ERROR_ALREADY_INITIALIZED = -62, ///< Module already initialized
    CHIPMUNK_RING_ERROR_INIT_FAILED = -63,        ///< Initialization failed
    CHIPMUNK_RING_ERROR_INVALID_STATE = -64,      ///< Invalid internal state
    
    // Ring-specific errors (-71 to -90)
    CHIPMUNK_RING_ERROR_SIGNER_NOT_IN_RING = -71, ///< Signer not found in ring
    CHIPMUNK_RING_ERROR_RING_TOO_SMALL = -72,     ///< Ring size too small (minimum 2)
    CHIPMUNK_RING_ERROR_RING_TOO_LARGE = -73,     ///< Ring size exceeds maximum
    CHIPMUNK_RING_ERROR_DUPLICATE_KEYS = -74,     ///< Duplicate keys in ring
    CHIPMUNK_RING_ERROR_ANONYMITY_VIOLATED = -75, ///< Anonymity property violated
    CHIPMUNK_RING_ERROR_LINKABILITY_FAILED = -76, ///< Linkability check failed
    
    // Coordination and multi-signer errors (-91 to -100)
    CHIPMUNK_RING_ERROR_COORDINATION_FAILED = -91, ///< Multi-signer coordination failed
    CHIPMUNK_RING_ERROR_INSUFFICIENT_SIGNERS = -92, ///< Not enough signers participated
    CHIPMUNK_RING_ERROR_TIMEOUT = -93,            ///< Operation timeout
    CHIPMUNK_RING_ERROR_PROTOCOL_VIOLATION = -94, ///< Protocol violation detected
    
    // Security and validation errors (-101 to -120)
    CHIPMUNK_RING_ERROR_SECURITY_VIOLATION = -101, ///< Security policy violation
    CHIPMUNK_RING_ERROR_REPLAY_ATTACK = -102,     ///< Replay attack detected
    CHIPMUNK_RING_ERROR_TIMING_ATTACK = -103,     ///< Timing attack vulnerability
    CHIPMUNK_RING_ERROR_SIDE_CHANNEL = -104,      ///< Side-channel vulnerability
    
    // System and compatibility errors (-121 to -140)
    CHIPMUNK_RING_ERROR_SYSTEM = -121,            ///< System error
    CHIPMUNK_RING_ERROR_NOT_SUPPORTED = -122,     ///< Operation not supported
    CHIPMUNK_RING_ERROR_COMPATIBILITY = -123,     ///< Compatibility issue
    CHIPMUNK_RING_ERROR_DEPRECATED = -124,        ///< Function deprecated
    
} chipmunk_ring_error_t;

/**
 * @brief Convert ChipmunkRing error code to human-readable string
 * @param error_code Error code to convert
 * @return Constant string describing the error
 */
const char* chipmunk_ring_error_to_string(chipmunk_ring_error_t error_code);

/**
 * @brief Check if error code indicates a critical failure
 * @param error_code Error code to check
 * @return true if error is critical and requires immediate attention
 */
bool chipmunk_ring_error_is_critical(chipmunk_ring_error_t error_code);

/**
 * @brief Check if error code is related to memory management
 * @param error_code Error code to check
 * @return true if error is memory-related
 */
bool chipmunk_ring_error_is_memory_related(chipmunk_ring_error_t error_code);

/**
 * @brief Check if error code is related to cryptographic operations
 * @param error_code Error code to check
 * @return true if error is crypto-related
 */
bool chipmunk_ring_error_is_crypto_related(chipmunk_ring_error_t error_code);

/**
 * @brief Log error with appropriate level based on error severity
 * @param error_code Error code to log
 * @param function_name Name of function where error occurred
 * @param additional_info Additional information about the error
 */
void chipmunk_ring_log_error(chipmunk_ring_error_t error_code, 
                            const char* function_name, 
                            const char* additional_info);

// Convenience macros for error handling
#define CHIPMUNK_RING_RETURN_IF_FAIL(expr, error_code) \
    do { \
        if (!(expr)) { \
            chipmunk_ring_log_error(error_code, __func__, #expr); \
            return error_code; \
        } \
    } while(0)

#define CHIPMUNK_RING_RETURN_IF_NULL(ptr, error_code) \
    CHIPMUNK_RING_RETURN_IF_FAIL((ptr) != NULL, error_code)

#define CHIPMUNK_RING_RETURN_IF_INVALID_SIZE(size, min_size, max_size) \
    CHIPMUNK_RING_RETURN_IF_FAIL((size) >= (min_size) && (size) <= (max_size), \
                                 CHIPMUNK_RING_ERROR_INVALID_SIZE)

// Error propagation macro
#define CHIPMUNK_RING_PROPAGATE_ERROR(result, function_name) \
    do { \
        if ((result) != CHIPMUNK_RING_SUCCESS) { \
            chipmunk_ring_log_error((chipmunk_ring_error_t)(result), function_name, \
                                   "Error propagated from nested function"); \
            return result; \
        } \
    } while(0)
