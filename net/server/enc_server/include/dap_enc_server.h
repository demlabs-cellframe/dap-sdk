/*
 * Authors:
 * Cellframe Development Team
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
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

/**
 * @file dap_enc_server.h
 * @brief Transport-independent encryption server for DAP Stream
 * @details Provides encryption handshake functionality that works over any transport
 * @date 2025-10-24
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "dap_hash.h"
#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport-independent encryption request structure
 * @details Contains all data needed for encryption handshake, regardless of transport
 */
typedef struct dap_enc_server_request {
    // Request parameters
    dap_enc_key_type_t enc_type;           // Block encryption type (e.g., IAES)
    dap_enc_key_type_t pkey_exchange_type; // Public key exchange type (e.g., Kyber)
    size_t pkey_exchange_size;             // Expected public key size
    size_t block_key_size;                 // Block encryption key size
    int protocol_version;                  // Protocol version (0 = legacy)
    size_t sign_count;                     // Number of signatures to verify
    
    // Client data
    uint8_t *alice_msg;                    // Alice's message (public key + signatures)
    size_t alice_msg_size;                 // Size of Alice's message
    
    // Optional ACL check
    dap_chain_hash_fast_t *sign_hashes;    // Array of signature hashes for ACL
    size_t sign_hashes_count;              // Number of signature hashes
} dap_enc_server_request_t;

/**
 * @brief Transport-independent encryption response structure
 */
typedef struct dap_enc_server_response {
    bool success;                          // Handshake successful
    
    // Response data
    char *encrypt_id;                      // Base64-encoded encryption session ID
    size_t encrypt_id_len;                 // Length of encrypt_id
    
    char *encrypt_msg;                     // Base64-encoded Bob's public key
    size_t encrypt_msg_len;                // Length of encrypt_msg
    
    char *node_sign_msg;                   // Base64-encoded node signature (optional)
    size_t node_sign_msg_len;              // Length of node_sign_msg
    
    // Error info (if success == false)
    int error_code;                        // Error code
    char *error_message;                   // Human-readable error message
} dap_enc_server_response_t;

/**
 * @brief ACL callback type for access control
 * @param a_sign_hash Hash of the signature to check
 * @return Pointer to allowed network ID, or NULL if access denied
 */
typedef uint8_t *(*dap_enc_server_acl_callback_t)(dap_chain_hash_fast_t *a_sign_hash);

/**
 * @brief Initialize encryption server
 * @return 0 on success, negative on error
 */
int dap_enc_server_init(void);

/**
 * @brief Deinitialize encryption server
 */
void dap_enc_server_deinit(void);

/**
 * @brief Set ACL callback for access control
 * @param a_callback Callback function for ACL checks
 */
void dap_enc_server_set_acl_callback(dap_enc_server_acl_callback_t a_callback);

/**
 * @brief Process encryption handshake request (transport-independent)
 * @param a_request Encryption request structure
 * @param a_response Output response structure (caller must free with dap_enc_server_response_free)
 * @return 0 on success, negative on error
 */
int dap_enc_server_process_request(
    const dap_enc_server_request_t *a_request,
    dap_enc_server_response_t **a_response
);

/**
 * @brief Free encryption server response
 * @param a_response Response to free
 */
void dap_enc_server_response_free(dap_enc_server_response_t *a_response);

/**
 * @brief Parse query string into request structure
 * @param a_query_string Query string (e.g., "enc_type=1,pkey_exchange_type=2,...")
 * @param a_request Output request structure (partial fill, caller must set alice_msg)
 * @return 0 on success, negative on error
 */
int dap_enc_server_parse_query(const char *a_query_string, dap_enc_server_request_t *a_request);

#ifdef __cplusplus
}
#endif

