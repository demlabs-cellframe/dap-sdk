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
 * @file dap_enc_server.c
 * @brief Transport-independent encryption server implementation
 * @details Core encryption handshake logic, independent of transport layer
 * @date 2025-10-24
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dap_enc_server.h"
#include "dap_common.h"
#include "dap_sign.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc_ks.h"
#include "dap_cert.h"
#include "dap_strfuncs.h"
#include "dap_http_ban_list_client.h"

#define LOG_TAG "dap_enc_server"

// External function from dap_stream (to avoid circular dependency)
extern dap_stream_node_addr_t dap_stream_node_addr_from_sign(dap_sign_t *a_sign);

static dap_enc_server_acl_callback_t s_acl_callback = NULL;

/**
 * @brief Initialize encryption server
 */
int dap_enc_server_init(void) {
    log_it(L_INFO, "Transport-independent encryption server initialized");
    return 0;
}

/**
 * @brief Deinitialize encryption server
 */
void dap_enc_server_deinit(void) {
    s_acl_callback = NULL;
    log_it(L_INFO, "Encryption server deinitialized");
}

/**
 * @brief Set ACL callback for access control
 */
void dap_enc_server_set_acl_callback(dap_enc_server_acl_callback_t a_callback) {
    s_acl_callback = a_callback;
    log_it(L_DEBUG, "ACL callback configured");
}

/**
 * @brief Parse query string into request structure
 */
int dap_enc_server_parse_query(const char *a_query_string, dap_enc_server_request_t *a_request) {
    if (!a_query_string || !a_request) {
        log_it(L_ERROR, "Invalid arguments to parse_query");
        return -1;
    }
    
    // Set defaults
    a_request->enc_type = DAP_ENC_KEY_TYPE_SALSA2012;
    a_request->pkey_exchange_type = DAP_ENC_KEY_TYPE_KEM_KYBER512;
    a_request->pkey_exchange_size = 800;  // Kyber512 public key size
    a_request->block_key_size = 32;
    a_request->protocol_version = 0;
    a_request->sign_count = 0;
    
    // Parse query parameters
    sscanf(a_query_string, 
           "enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,block_key_size=%zu,protocol_version=%d,sign_count=%zu",
           (int*)&a_request->enc_type,
           (int*)&a_request->pkey_exchange_type,
           &a_request->pkey_exchange_size,
           &a_request->block_key_size,
           &a_request->protocol_version,
           &a_request->sign_count);
    
    log_it(L_DEBUG, "Parsed: enc_type=%s, pkey_exchange=%s, protocol_v=%d, sign_count=%zu",
           dap_enc_get_type_name(a_request->enc_type),
           dap_enc_get_type_name(a_request->pkey_exchange_type),
           a_request->protocol_version,
           a_request->sign_count);
    
    return 0;
}

/**
 * @brief Process encryption handshake request (core logic)
 */
int dap_enc_server_process_request(
    const dap_enc_server_request_t *a_request,
    dap_enc_server_response_t **a_response
) {
    if (!a_request || !a_response) {
        log_it(L_ERROR, "Invalid arguments to process_request");
        return -1;
    }
    
    // Allocate response
    dap_enc_server_response_t *l_resp = DAP_NEW_Z(dap_enc_server_response_t);
    if (!l_resp) {
        log_it(L_CRITICAL, "Failed to allocate response");
        return -2;
    }
    
    log_it(L_DEBUG, "Processing handshake request: protocol_version=%d, sign_count=%zu, msg_size=%zu",
           a_request->protocol_version, a_request->sign_count, a_request->alice_msg_size);
    
    // Validate Alice message
    if (!a_request->alice_msg || a_request->alice_msg_size == 0) {
        log_it(L_ERROR, "Missing Alice message: msg=%p, size=%zu", a_request->alice_msg, a_request->alice_msg_size);
        l_resp->success = false;
        l_resp->error_code = -3;
        l_resp->error_message = dap_strdup("Missing Alice message");
        *a_response = l_resp;
        return -3;
    }
    
    size_t l_sign_count = a_request->sign_count;
    size_t l_pkey_size = a_request->pkey_exchange_size;
    
    // Auto-detect signature if not explicitly set
    if (!a_request->protocol_version && !l_sign_count) {
        if (a_request->alice_msg_size > l_pkey_size + sizeof(dap_sign_hdr_t)) {
            l_sign_count = 1;
            log_it(L_DEBUG, "Auto-detected signature (legacy mode)");
        } else if (a_request->alice_msg_size != l_pkey_size) {
            l_resp->success = false;
            l_resp->error_code = -4;
            l_resp->error_message = dap_strdup("Invalid message size");
            *a_response = l_resp;
            return -4;
        }
    }
    
    // Verify all signatures
    dap_sign_t *l_first_sign = NULL;
    size_t l_bias = l_pkey_size;
    size_t l_sign_validated = 0;
    dap_chain_hash_fast_t l_sign_hash = {0};
    
    for (; l_sign_validated < l_sign_count && l_bias < a_request->alice_msg_size; ++l_sign_validated) {
        dap_sign_t *l_sign = (dap_sign_t *)&a_request->alice_msg[l_bias];
        
        // Verify signature
        int l_verify_ret = dap_sign_verify_all(l_sign, a_request->alice_msg_size - l_bias,
                                                a_request->alice_msg, l_pkey_size);
        if (l_verify_ret) {
            log_it(L_ERROR, "Signature verification failed (err %d)", l_verify_ret);
            l_resp->success = false;
            l_resp->error_code = -5;
            l_resp->error_message = dap_strdup("Signature verification failed");
            *a_response = l_resp;
            return -5;
        }
        
        l_bias += dap_sign_get_size(l_sign);
        
        // Check ban list
        dap_stream_node_addr_t l_client_addr = dap_stream_node_addr_from_sign(l_sign);
        const char *l_addr_str = dap_stream_node_addr_to_str_static(l_client_addr);
        
        log_it(L_DEBUG, "Validated signature %zu from node "NODE_ADDR_FP_STR, l_sign_validated, NODE_ADDR_FP_ARGS_S(l_client_addr));
        
        if (dap_http_ban_list_client_check(l_addr_str, NULL, NULL)) {
            log_it(L_ERROR, "Client %s is banned", l_addr_str);
            l_resp->success = false;
            l_resp->error_code = -6;
            l_resp->error_message = dap_strdup("Client is banned");
            *a_response = l_resp;
            return -6;
        }
        
        if (!l_first_sign) l_first_sign = l_sign;
    }
    
    if (l_sign_validated != l_sign_count) {
        log_it(L_ERROR, "Can't authorize all %zu signatures", l_sign_count);
        l_resp->success = false;
        l_resp->error_code = -7;
        l_resp->error_message = dap_strdup("Incomplete signature validation");
        *a_response = l_resp;
        return -7;
    }
    
    // Generate server keypair (Bob)
    dap_enc_key_t *l_pkey_exchange_key = dap_enc_key_new(a_request->pkey_exchange_type);
    if (!l_pkey_exchange_key) {
        log_it(L_ERROR, "Failed to create keypair for %s",
               dap_enc_get_type_name(a_request->pkey_exchange_type));
        l_resp->success = false;
        l_resp->error_code = -8;
        l_resp->error_message = dap_strdup("Keypair generation failed");
        *a_response = l_resp;
        return -8;
    }
    
    // Generate Bob's shared key
    if (l_pkey_exchange_key->gen_bob_shared_key) {
        l_pkey_exchange_key->pub_key_data_size = l_pkey_exchange_key->gen_bob_shared_key(
            l_pkey_exchange_key, a_request->alice_msg, l_pkey_size,
            &l_pkey_exchange_key->pub_key_data);
    }
    
    // Create key storage entry
    dap_enc_ks_key_t *l_enc_key_ks = dap_enc_ks_new();
    if (!l_enc_key_ks) {
        log_it(L_CRITICAL, "Failed to create key storage entry");
        dap_enc_key_delete(l_pkey_exchange_key);
        l_resp->success = false;
        l_resp->error_code = -9;
        l_resp->error_message = dap_strdup("Key storage allocation failed");
        *a_response = l_resp;
        return -9;
    }
    
    // Apply ACL if callback is set
    if (s_acl_callback) {
        l_enc_key_ks->acl_list = s_acl_callback(&l_sign_hash);
    }
    
    // Generate session encryption key
    l_enc_key_ks->key = dap_enc_key_new_generate(
        a_request->enc_type,
        l_pkey_exchange_key->priv_key_data,      // shared secret
        l_pkey_exchange_key->priv_key_data_size,
        l_enc_key_ks->id, DAP_ENC_KS_KEY_ID_SIZE,
        a_request->block_key_size);
    
    // Save key in storage
    dap_enc_ks_save_in_storage(l_enc_key_ks);
    
    // Encode session ID to Base64
    size_t l_enc_id_size = DAP_ENC_BASE64_ENCODE_SIZE(DAP_ENC_KS_KEY_ID_SIZE) + 1;
    char *l_encrypt_id = DAP_NEW_Z_SIZE(char, l_enc_id_size);
    if (!l_encrypt_id) {
        log_it(L_CRITICAL, "Failed to allocate encrypt_id buffer");
        dap_enc_key_delete(l_pkey_exchange_key);
        l_resp->success = false;
        l_resp->error_code = -10;
        l_resp->error_message = dap_strdup("Memory allocation failed");
        *a_response = l_resp;
        return -10;
    }
    size_t l_enc_id_len = dap_enc_base64_encode(l_enc_key_ks->id, DAP_ENC_KS_KEY_ID_SIZE,
                                                l_encrypt_id, DAP_ENC_DATA_TYPE_B64);
    
    // Encode Bob's public key to Base64
    size_t l_enc_msg_size = DAP_ENC_BASE64_ENCODE_SIZE(l_pkey_exchange_key->pub_key_data_size) + 1;
    char *l_encrypt_msg = DAP_NEW_Z_SIZE(char, l_enc_msg_size);
    if (!l_encrypt_msg) {
        log_it(L_CRITICAL, "Failed to allocate encrypt_msg buffer");
        DAP_DELETE(l_encrypt_id);
        dap_enc_key_delete(l_pkey_exchange_key);
        l_resp->success = false;
        l_resp->error_code = -11;
        l_resp->error_message = dap_strdup("Memory allocation failed");
        *a_response = l_resp;
        return -11;
    }
    size_t l_enc_msg_len = dap_enc_base64_encode(l_pkey_exchange_key->pub_key_data,
                                                 l_pkey_exchange_key->pub_key_data_size,
                                                 l_encrypt_msg, DAP_ENC_DATA_TYPE_B64);
    
    // Generate node signature (if protocol version supports it)
    char *l_node_sign_msg = NULL;
    size_t l_node_sign_msg_len = 0;
    
    if (a_request->protocol_version && l_sign_count) {
        l_enc_key_ks->node_addr = dap_stream_node_addr_from_sign(l_first_sign);
        
        dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
        if (l_node_cert) {
            dap_sign_t *l_node_sign = dap_sign_create(l_node_cert->enc_key,
                                                      l_pkey_exchange_key->pub_key_data,
                                                      l_pkey_exchange_key->pub_key_data_size);
            if (l_node_sign) {
                size_t l_node_sign_size = dap_sign_get_size(l_node_sign);
                size_t l_node_sign_buf_size = DAP_ENC_BASE64_ENCODE_SIZE(l_node_sign_size) + 1;
                
                l_node_sign_msg = DAP_NEW_Z_SIZE(char, l_node_sign_buf_size);
                if (l_node_sign_msg) {
                    l_node_sign_msg_len = dap_enc_base64_encode(l_node_sign, l_node_sign_size,
                                                                l_node_sign_msg, DAP_ENC_DATA_TYPE_B64);
                }
                DAP_DELETE(l_node_sign);
            }
        }
    }
    
    // Fill response
    l_resp->success = true;
    l_resp->encrypt_id = l_encrypt_id;
    l_resp->encrypt_id_len = l_enc_id_len;
    l_resp->encrypt_msg = l_encrypt_msg;
    l_resp->encrypt_msg_len = l_enc_msg_len;
    l_resp->node_sign_msg = l_node_sign_msg;
    l_resp->node_sign_msg_len = l_node_sign_msg_len;
    l_resp->error_code = 0;
    l_resp->error_message = NULL;
    
    // Cleanup
    dap_enc_key_delete(l_pkey_exchange_key);
    
    *a_response = l_resp;
    log_it(L_INFO, "Encryption handshake completed successfully");
    return 0;
}

/**
 * @brief Free response structure
 */
void dap_enc_server_response_free(dap_enc_server_response_t *a_response) {
    if (!a_response) return;
    
    DAP_DEL_Z(a_response->encrypt_id);
    DAP_DEL_Z(a_response->encrypt_msg);
    DAP_DEL_Z(a_response->node_sign_msg);
    DAP_DEL_Z(a_response->error_message);
    DAP_DELETE(a_response);
}

