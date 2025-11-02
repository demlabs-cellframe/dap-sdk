/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_enc_http.c
 * @brief HTTP transport adapter for transport-independent encryption server
 * @details Thin compatibility layer that adapts HTTP requests to dap_enc_server API
 * @date 2025-10-24
 * 
 * This file is now a thin adapter layer. Core encryption logic is in dap_enc_server.c
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_sign.h"
#include "dap_http_server.h"
#include "dap_http_client.h"
#include "dap_http_simple.h"
#include "dap_enc.h"
#include "dap_enc_ks.h"
#include "dap_enc_key.h"
#include "dap_enc_http.h"
#include "dap_enc_base64.h"
#include "dap_enc_server.h"
#include "http_status_code.h"
#include "dap_http_ban_list_client.h"
#include "json.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_enc_http"

/**
 * @brief Initialize HTTP encryption adapter
 */
int enc_http_init() {
    dap_http_ban_list_client_init();
    dap_enc_server_init();
    log_it(L_INFO, "HTTP encryption adapter initialized");
    return 0;
}

/**
 * @brief Deinitialize HTTP encryption adapter
 */
void enc_http_deinit() {
    dap_enc_server_deinit();
    log_it(L_INFO, "HTTP encryption adapter deinitialized");
}

/**
 * @brief Set ACL callback (forwards to dap_enc_server)
 */
void dap_enc_http_set_acl_callback(dap_enc_acl_callback_t a_callback) {
    dap_enc_server_set_acl_callback((dap_enc_server_acl_callback_t)a_callback);
}

/**
 * @brief Write encryption handshake reply in JSON format
 */
static void _enc_http_write_reply(struct dap_http_simple *a_cl_st,
                                  const char* a_encrypt_id, int a_id_len,
                                  const char* a_encrypt_msg, int a_msg_len,
                                  const char* a_node_sign, int a_sign_len)
{
    struct json_object *l_jobj = json_object_new_object();
    json_object_object_add(l_jobj, "encrypt_id", json_object_new_string_len(a_encrypt_id, a_id_len));
    json_object_object_add(l_jobj, "encrypt_msg", json_object_new_string_len(a_encrypt_msg, a_msg_len));
    if (a_node_sign)
        json_object_object_add(l_jobj, "node_sign", json_object_new_string_len(a_node_sign, a_sign_len));
    json_object_object_add(l_jobj, "dap_protocol_version", json_object_new_int(DAP_PROTOCOL_VERSION));
    
    const char* l_json_str = json_object_to_json_string(l_jobj);
    dap_http_simple_reply(a_cl_st, (void*) l_json_str, (size_t) strlen(l_json_str));
    json_object_put(l_jobj);
}

/**
 * @brief HTTP encryption handler (thin adapter to dap_enc_server)
 * @param cl_st HTTP Simple client instance
 * @param arg Pointer to http_status_code_t return code
 */
void enc_http_proc(struct dap_http_simple *cl_st, void *arg)
{
    log_it(L_DEBUG, "Processing HTTP encryption request (via adapter)");
    http_status_code_t *return_code = (http_status_code_t*)arg;
    
    // HTTP server extracts basename before calling processor
    // So url_path should be "gd4y5yh78w42aaagh", not "enc_init/gd4y5yh78w42aaagh"
    const char *l_expected_path = "gd4y5yh78w42aaagh";
    
    if (!cl_st) {
        log_it(L_ERROR, "HTTP simple client is NULL");
        *return_code = Http_Status_BadRequest;
        return;
    }
    
    // Log actual path for debugging
    log_it(L_DEBUG, "enc_http_proc: url_path='%s' (len=%u), expected='%s'", 
           cl_st->http_client->url_path, 
           cl_st->http_client->url_path_len,
           l_expected_path);
    
    // Validate URL path - should be basename after HTTP server processing
    if (strcmp(cl_st->http_client->url_path, l_expected_path) != 0) {
        log_it(L_ERROR, "Wrong path '%s' in enc_http request (expected '%s')", 
               cl_st->http_client->url_path, l_expected_path);
        *return_code = Http_Status_NotFound;
        return;
    }
    
    // Parse query string into request structure
    dap_enc_server_request_t l_request = {0};
    if (dap_enc_server_parse_query(cl_st->http_client->in_query_string, &l_request) != 0) {
        log_it(L_ERROR, "Failed to parse query string");
        *return_code = Http_Status_BadRequest;
        return;
    }
    
    // Decode Alice's message from Base64
    if (!cl_st->request || cl_st->request_size == 0) {
        log_it(L_ERROR, "Empty request body");
        *return_code = Http_Status_BadRequest;
        return;
    }
    
    size_t l_decode_len = DAP_ENC_BASE64_DECODE_SIZE(cl_st->request_size);
    uint8_t *alice_msg = DAP_NEW_Z_SIZE(uint8_t, l_decode_len + 1);
    if (!alice_msg) {
        log_it(L_CRITICAL, "Failed to allocate alice_msg buffer");
        *return_code = Http_Status_InternalServerError;
        return;
    }
    
    l_decode_len = dap_enc_base64_decode(cl_st->request, cl_st->request_size, 
                                         alice_msg, DAP_ENC_DATA_TYPE_B64);
    alice_msg[l_decode_len] = '\0';
    
    // Fill request with decoded data
    l_request.alice_msg = alice_msg;
    l_request.alice_msg_size = l_decode_len;
    
    // Process encryption handshake via transport-independent API
    dap_enc_server_response_t *l_response = NULL;
    int l_ret = dap_enc_server_process_request(&l_request, &l_response);
    
    DAP_DELETE(alice_msg);
    
    // Handle response
    if (l_ret != 0 || !l_response || !l_response->success) {
        log_it(L_ERROR, "Encryption handshake failed: %s", 
               l_response && l_response->error_message ? l_response->error_message : "unknown error");
        
        if (l_response) {
            // Map error codes to HTTP status codes
            switch (l_response->error_code) {
                case -5:  // Signature verification failed
                    *return_code = Http_Status_Unauthorized;
                    break;
                case -6:  // Client banned
                    *return_code = Http_Status_Forbidden;
                    break;
                default:
                    *return_code = Http_Status_BadRequest;
            }
            dap_enc_server_response_free(l_response);
        } else {
            *return_code = Http_Status_InternalServerError;
        }
        return;
    }
    
    // Write reply in JSON format
    _enc_http_write_reply(cl_st,
                         l_response->encrypt_id, (int)l_response->encrypt_id_len,
                         l_response->encrypt_msg, (int)l_response->encrypt_msg_len,
                         l_response->node_sign_msg, (int)l_response->node_sign_msg_len);
    
    dap_enc_server_response_free(l_response);
    *return_code = Http_Status_OK;
}

/**
 * @brief enc_http_add_proc
 * @param sh
 * @param url
 */
void enc_http_add_proc(struct dap_http_server* sh, const char * url)
{
    dap_http_simple_proc_add(sh, url, 140000, enc_http_proc);
    log_it(L_INFO, "HTTP encryption endpoint registered: %s", url);
}

/**
 * @brief Decode encrypted HTTP request
 * @param a_http_simple HTTP simple structure
 * @return Delegate structure with decoded data, or NULL on error
 */
enc_http_delegate_t *enc_http_request_decode(struct dap_http_simple *a_http_simple)
{
    dap_enc_key_t *l_key = dap_enc_ks_find_http(a_http_simple->http_client);
    if (!l_key) {
        log_it(L_WARNING, "No encryption key found for HTTP client");
        return NULL;
    }
    
    enc_http_delegate_t *dg = DAP_NEW_Z_RET_VAL_IF_FAIL(enc_http_delegate_t, NULL);
    dg->key = l_key;
    dg->http = a_http_simple->http_client;
    
    strncpy(dg->action, (char *)a_http_simple->http_client->action, sizeof(dg->action) - 1);
    dg->action[sizeof(dg->action) - 1] = '\0';
    
    if (a_http_simple->http_client->in_cookie[0])
        dg->cookie = strdup(a_http_simple->http_client->in_cookie);
    
    // Decode request body if present
    if (a_http_simple->request_size) {
        size_t l_dg_request_size_max = a_http_simple->request_size;
        dg->request = DAP_NEW_SIZE(void, l_dg_request_size_max + 1);
        if (!dg->request) {
            log_it(L_CRITICAL, "Memory allocation failed");
            DAP_DEL_Z(dg);
            return NULL;
        }
        dg->request_size = dap_enc_decode(l_key, a_http_simple->request, a_http_simple->request_size,
                                          dg->request, l_dg_request_size_max, DAP_ENC_DATA_TYPE_RAW);
        dg->request_str[dg->request_size] = 0;
    }
    
    // Decode URL path if present
    dap_enc_data_type_t l_enc_type = DAP_ENC_DATA_TYPE_B64;  // Default
    int protocol_version = 21;  // TODO: Get actual protocol version
    if (protocol_version >= 21)
        l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
    
    size_t l_url_path_size_max = strlen(a_http_simple->http_client->url_path);
    if (l_url_path_size_max) {
        dg->url_path = DAP_NEW_SIZE(char, l_url_path_size_max + 1);
        if (!dg->url_path) {
            log_it(L_CRITICAL, "Memory allocation failed");
            DAP_DEL_Z(dg->request);
            DAP_DEL_Z(dg);
            return NULL;
        }
        dg->url_path_size = dap_enc_decode(l_key, a_http_simple->http_client->url_path, l_url_path_size_max,
                                           dg->url_path, l_url_path_size_max, l_enc_type);
        dg->url_path[dg->url_path_size] = '\0';
    }
    
    // Decode query string if present
    size_t l_in_query_size_max = strlen(a_http_simple->http_client->in_query_string);
    if (l_in_query_size_max) {
        dg->in_query = DAP_NEW_SIZE(char, l_in_query_size_max + 1);
        if (!dg->in_query) {
            log_it(L_CRITICAL, "Memory allocation failed");
            DAP_DEL_Z(dg->url_path);
            DAP_DEL_Z(dg->request);
            DAP_DEL_Z(dg);
            return NULL;
        }
        dg->in_query_size = dap_enc_decode(l_key, a_http_simple->http_client->in_query_string, l_in_query_size_max,
                                           dg->in_query, l_in_query_size_max, l_enc_type);
        dg->in_query[dg->in_query_size] = '\0';
    }
    
    return dg;
}

/**
 * @brief Encode and send HTTP reply
 * @param a_http_simple HTTP simple structure
 * @param a_http_delegate Delegate with response data
 */
void enc_http_reply_encode(struct dap_http_simple *a_http_simple, enc_http_delegate_t *a_http_delegate)
{
    if (!a_http_simple || !a_http_delegate || !a_http_delegate->key) {
        log_it(L_ERROR, "Invalid arguments to enc_http_reply_encode");
        return;
    }
    
    if (!a_http_delegate->response_size) {
        log_it(L_WARNING, "Empty response, nothing to encode");
        return;
    }
    
    // Encode response
    size_t l_response_enc_max = a_http_delegate->response_size * 2 + 16;
    void *l_response_enc = DAP_NEW_SIZE(void, l_response_enc_max);
    if (!l_response_enc) {
        log_it(L_CRITICAL, "Memory allocation failed");
        return;
    }
    
    size_t l_response_enc_size = dap_enc_code(a_http_delegate->key,
                                              a_http_delegate->response, a_http_delegate->response_size,
                                              l_response_enc, l_response_enc_max, DAP_ENC_DATA_TYPE_RAW);
    
    dap_http_simple_reply(a_http_simple, l_response_enc, l_response_enc_size);
    DAP_DELETE(l_response_enc);
}

/**
 * @brief Send reply via delegate
 * @param dg Delegate structure
 * @param data Response data
 * @param data_size Response data size
 * @return Number of bytes sent
 */
size_t enc_http_reply(enc_http_delegate_t *dg, void *data, size_t data_size)
{
    if (!dg || !data) return 0;
    
    if (dg->response_size_max < data_size) {
        DAP_DEL_Z(dg->response);
        dg->response = DAP_NEW_SIZE(void, data_size);
        dg->response_size_max = data_size;
    }
    
    memcpy(dg->response, data, data_size);
    dg->response_size = data_size;
    return data_size;
}

/**
 * @brief Send formatted reply via delegate
 * @param a_http_delegate Delegate structure
 * @param a_data Format string
 * @param ... Format arguments
 * @return Number of bytes sent
 */
size_t enc_http_reply_f(enc_http_delegate_t *a_http_delegate, const char *a_data, ...)
{
    if (!a_http_delegate || !a_data) return 0;
    
    va_list ap;
    va_start(ap, a_data);
    int l_ret = vsnprintf(NULL, 0, a_data, ap);
    va_end(ap);
    
    if (l_ret <= 0) return 0;
    
    size_t l_size = (size_t)l_ret + 1;
    if (a_http_delegate->response_size_max < l_size) {
        DAP_DEL_Z(a_http_delegate->response);
        a_http_delegate->response = DAP_NEW_SIZE(void, l_size);
        a_http_delegate->response_size_max = l_size;
    }
    
    va_start(ap, a_data);
    vsnprintf(a_http_delegate->response_str, l_size, a_data, ap);
    va_end(ap);
    
    a_http_delegate->response_size = l_size - 1;
    return a_http_delegate->response_size;
}

/**
 * @brief Delete delegate structure
 * @param dg Delegate to delete
 */
void enc_http_delegate_delete(enc_http_delegate_t *dg)
{
    if (!dg) return;
    
    DAP_DEL_Z(dg->url_path);
    DAP_DEL_Z(dg->in_query);
    DAP_DEL_Z(dg->cookie);
    DAP_DEL_Z(dg->request);
    DAP_DEL_Z(dg->response);
    DAP_DELETE(dg);
}
