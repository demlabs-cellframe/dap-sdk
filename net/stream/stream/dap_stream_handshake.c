/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_stream_handshake.c
 * @brief DAP Stream Handshake Protocol (DSHP) v1.0 implementation
 * @date 2025-10-23
 * @author Cellframe Team
 */

#include <string.h>
#include <arpa/inet.h>  // for htons/ntohs (network byte order)
#include "dap_stream_handshake.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_stream_handshake"

// Helper macros for network byte order conversion
#define HTON16(x) htons(x)
#define NTOH16(x) ntohs(x)
#define HTON32(x) htonl(x)
#define NTOH32(x) ntohl(x)

/**
 * @brief Initialize DSHP handshake subsystem
 * @return 0 on success
 */
int dap_stream_handshake_init(void)
{
    log_it(L_NOTICE, "Initializing DAP Stream Handshake Protocol (DSHP) v1.0");
    log_it(L_INFO, "Protocol magic: 0x%08X", DAP_STREAM_HANDSHAKE_MAGIC);
    log_it(L_INFO, "Protocol version: 0x%08X", DAP_STREAM_HANDSHAKE_VERSION);
    return 0;
}

/**
 * @brief Cleanup DSHP handshake subsystem
 */
void dap_stream_handshake_deinit(void)
{
    log_it(L_NOTICE, "Deinitializing DAP Stream Handshake Protocol");
}

/**
 * @brief Validate magic number and version
 * @param a_magic Magic number to check
 * @param a_version Version to check
 * @return 0 if valid, -1 if invalid
 */
int dap_stream_handshake_validate_header(uint32_t a_magic, uint32_t a_version)
{
    if (a_magic != DAP_STREAM_HANDSHAKE_MAGIC) {
        log_it(L_ERROR, "Invalid magic number: 0x%08X (expected 0x%08X)", 
               a_magic, DAP_STREAM_HANDSHAKE_MAGIC);
        return -1;
    }
    
    // Check major version compatibility
    uint8_t l_major = (a_version >> 24) & 0xFF;
    uint8_t l_expected_major = (DAP_STREAM_HANDSHAKE_VERSION >> 24) & 0xFF;
    
    if (l_major != l_expected_major) {
        log_it(L_WARNING, "Version mismatch: major=%d (expected %d)", 
               l_major, l_expected_major);
        // For now, allow minor version differences
    }
    
    return 0;
}

/**
 * @brief Write TLV field to buffer
 * @param a_buffer Output buffer
 * @param a_offset Current offset (updated on success)
 * @param a_buffer_size Total buffer size
 * @param a_type TLV type ID
 * @param a_value Value data
 * @param a_value_size Value size
 * @return 0 on success, -1 on failure
 */
int dap_stream_handshake_tlv_write(uint8_t *a_buffer,
                                     size_t *a_offset,
                                     size_t a_buffer_size,
                                     uint16_t a_type,
                                     const void *a_value,
                                     uint16_t a_value_size)
{
    // Validate parameters
    if (!a_buffer || !a_offset || !a_value) {
        log_it(L_ERROR, "Invalid parameters for TLV write");
        return -1;
    }
    
    // Check if we have enough space (4 bytes header + value)
    size_t l_required = *a_offset + sizeof(dap_stream_handshake_tlv_hdr_t) + a_value_size;
    if (l_required > a_buffer_size) {
        log_it(L_ERROR, "Buffer overflow: required=%zu, available=%zu", 
               l_required, a_buffer_size);
        return -1;
    }
    
    // Write TLV header (type and length in network byte order)
    dap_stream_handshake_tlv_hdr_t *l_hdr = 
        (dap_stream_handshake_tlv_hdr_t *)(a_buffer + *a_offset);
    l_hdr->type = HTON16(a_type);
    l_hdr->length = HTON16(a_value_size);
    
    *a_offset += sizeof(dap_stream_handshake_tlv_hdr_t);
    
    // Write value
    if (a_value_size > 0) {
        memcpy(a_buffer + *a_offset, a_value, a_value_size);
        *a_offset += a_value_size;
    }
    
    return 0;
}

/**
 * @brief Read TLV field from buffer
 * @param a_buffer Input buffer
 * @param a_offset Current offset (updated on success)
 * @param a_buffer_size Total buffer size
 * @param a_type_out Output TLV type
 * @param a_value_out Output value pointer (points into buffer)
 * @param a_value_size_out Output value size
 * @return 0 on success, -1 on failure
 */
int dap_stream_handshake_tlv_read(const uint8_t *a_buffer,
                                    size_t *a_offset,
                                    size_t a_buffer_size,
                                    uint16_t *a_type_out,
                                    const void **a_value_out,
                                    uint16_t *a_value_size_out)
{
    // Validate parameters
    if (!a_buffer || !a_offset || !a_type_out || !a_value_out || !a_value_size_out) {
        log_it(L_ERROR, "Invalid parameters for TLV read");
        return -1;
    }
    
    // Check if we have enough data for header
    if (*a_offset + sizeof(dap_stream_handshake_tlv_hdr_t) > a_buffer_size) {
        log_it(L_ERROR, "Buffer underflow reading TLV header");
        return -1;
    }
    
    // Read TLV header
    const dap_stream_handshake_tlv_hdr_t *l_hdr = 
        (const dap_stream_handshake_tlv_hdr_t *)(a_buffer + *a_offset);
    *a_type_out = NTOH16(l_hdr->type);
    *a_value_size_out = NTOH16(l_hdr->length);
    
    *a_offset += sizeof(dap_stream_handshake_tlv_hdr_t);
    
    // Check if we have enough data for value
    if (*a_offset + *a_value_size_out > a_buffer_size) {
        log_it(L_ERROR, "Buffer underflow reading TLV value: type=0x%04X, size=%u",
               *a_type_out, *a_value_size_out);
        return -1;
    }
    
    // Set value pointer (points into buffer)
    *a_value_out = (*a_value_size_out > 0) ? (a_buffer + *a_offset) : NULL;
    *a_offset += *a_value_size_out;
    
    return 0;
}

/**
 * @brief Create and serialize handshake request to TLV
 * @param a_request Request structure
 * @param a_data_out Output buffer (caller must free)
 * @param a_data_size_out Output size
 * @return 0 on success, negative on error
 */
int dap_stream_handshake_request_create(const dap_stream_handshake_request_t *a_request,
                                         void **a_data_out,
                                         size_t *a_data_size_out)
{
    if (!a_request || !a_data_out || !a_data_size_out) {
        log_it(L_ERROR, "Invalid parameters for handshake request create");
        return -1;
    }
    
    // Calculate required buffer size
    size_t l_size = 0;
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // magic
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // version
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint16_t);  // message_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // enc_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // pkey_exchange_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // pkey_exchange_size
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // block_key_size
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + a_request->alice_pub_key_size;  // alice_pub_key
    
    // Optional fields
    if (a_request->alice_signature) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + a_request->alice_signature_size;
    }
    
    // Allocate buffer
    uint8_t *l_buffer = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Failed to allocate %zu bytes for handshake request", l_size);
        return -2;
    }
    
    size_t l_offset = 0;
    int l_ret = 0;
    
    // Write fields
    uint32_t l_magic = HTON32(a_request->magic);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size, 
                                             DSHP_TLV_MAGIC, &l_magic, sizeof(l_magic));
    
    uint32_t l_version = HTON32(a_request->version);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_VERSION, &l_version, sizeof(l_version));
    
    uint16_t l_msg_type = HTON16(DSHP_MSG_HANDSHAKE_REQUEST);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MESSAGE_TYPE, &l_msg_type, sizeof(l_msg_type));
    
    uint8_t l_enc_type = (uint8_t)a_request->enc_type;
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_ENC_TYPE, &l_enc_type, sizeof(l_enc_type));
    
    uint8_t l_pkey_type = (uint8_t)a_request->pkey_exchange_type;
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_PKEY_EXCHANGE_TYPE, &l_pkey_type, sizeof(l_pkey_type));
    
    uint32_t l_pkey_size = HTON32(a_request->pkey_exchange_size);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_PKEY_EXCHANGE_SIZE, &l_pkey_size, sizeof(l_pkey_size));
    
    uint32_t l_block_size = HTON32(a_request->block_key_size);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_BLOCK_KEY_SIZE, &l_block_size, sizeof(l_block_size));
    
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_ALICE_PUB_KEY, 
                                             a_request->alice_pub_key, 
                                             a_request->alice_pub_key_size);
    
    // Optional signature
    if (a_request->alice_signature) {
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_ALICE_SIGNATURE,
                                                 a_request->alice_signature,
                                                 a_request->alice_signature_size);
    }
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to write TLV fields");
        DAP_DELETE(l_buffer);
        return -3;
    }
    
    *a_data_out = l_buffer;
    *a_data_size_out = l_offset;
    
    log_it(L_DEBUG, "Created handshake request: %zu bytes", l_offset);
    return 0;
}

/**
 * @brief Parse binary TLV data into handshake request
 * @param a_data Input TLV data
 * @param a_data_size Size of input
 * @param a_request_out Output structure (caller must free)
 * @return 0 on success, negative on error
 */
int dap_stream_handshake_request_parse(const void *a_data,
                                        size_t a_data_size,
                                        dap_stream_handshake_request_t **a_request_out)
{
    if (!a_data || !a_request_out) {
        log_it(L_ERROR, "Invalid parameters for handshake request parse");
        return -1;
    }
    
    dap_stream_handshake_request_t *l_req = DAP_NEW_Z(dap_stream_handshake_request_t);
    if (!l_req) {
        log_it(L_CRITICAL, "Failed to allocate handshake request");
        return -2;
    }
    
    const uint8_t *l_buffer = (const uint8_t *)a_data;
    size_t l_offset = 0;
    
    // Parse TLV fields
    while (l_offset < a_data_size) {
        uint16_t l_type;
        const void *l_value;
        uint16_t l_value_size;
        
        if (dap_stream_handshake_tlv_read(l_buffer, &l_offset, a_data_size,
                                           &l_type, &l_value, &l_value_size) != 0) {
            log_it(L_ERROR, "Failed to read TLV field at offset %zu", l_offset);
            dap_stream_handshake_request_free(l_req);
            return -3;
        }
        
        // Process field based on type
        switch (l_type) {
            case DSHP_TLV_MAGIC:
                if (l_value_size == sizeof(uint32_t)) {
                    l_req->magic = NTOH32(*(const uint32_t *)l_value);
                }
                break;
                
            case DSHP_TLV_VERSION:
                if (l_value_size == sizeof(uint32_t)) {
                    l_req->version = NTOH32(*(const uint32_t *)l_value);
                }
                break;
                
            case DSHP_TLV_ENC_TYPE:
                if (l_value_size == sizeof(uint8_t)) {
                    l_req->enc_type = (dap_enc_key_type_t)(*(const uint8_t *)l_value);
                }
                break;
                
            case DSHP_TLV_PKEY_EXCHANGE_TYPE:
                if (l_value_size == sizeof(uint8_t)) {
                    l_req->pkey_exchange_type = (dap_enc_key_type_t)(*(const uint8_t *)l_value);
                }
                break;
                
            case DSHP_TLV_PKEY_EXCHANGE_SIZE:
                if (l_value_size == sizeof(uint32_t)) {
                    l_req->pkey_exchange_size = NTOH32(*(const uint32_t *)l_value);
                }
                break;
                
            case DSHP_TLV_BLOCK_KEY_SIZE:
                if (l_value_size == sizeof(uint32_t)) {
                    l_req->block_key_size = NTOH32(*(const uint32_t *)l_value);
                }
                break;
                
            case DSHP_TLV_ALICE_PUB_KEY:
                l_req->alice_pub_key_size = l_value_size;
                l_req->alice_pub_key = DAP_NEW_Z_SIZE(uint8_t, l_value_size);
                if (l_req->alice_pub_key) {
                    memcpy(l_req->alice_pub_key, l_value, l_value_size);
                }
                break;
                
            case DSHP_TLV_ALICE_SIGNATURE:
                l_req->alice_signature_size = l_value_size;
                l_req->alice_signature = DAP_NEW_Z_SIZE(uint8_t, l_value_size);
                if (l_req->alice_signature) {
                    memcpy(l_req->alice_signature, l_value, l_value_size);
                }
                break;
                
            default:
                // Unknown type - skip (forward compatibility)
                log_it(L_DEBUG, "Skipping unknown TLV type 0x%04X", l_type);
                break;
        }
    }
    
    // Validate required fields
    if (dap_stream_handshake_validate_header(l_req->magic, l_req->version) != 0) {
        dap_stream_handshake_request_free(l_req);
        return -4;
    }
    
    if (!l_req->alice_pub_key || l_req->alice_pub_key_size == 0) {
        log_it(L_ERROR, "Missing required alice_pub_key");
        dap_stream_handshake_request_free(l_req);
        return -5;
    }
    
    *a_request_out = l_req;
    log_it(L_DEBUG, "Parsed handshake request successfully");
    return 0;
}

/**
 * @brief Free handshake request structure
 * @param a_request Request to free
 */
void dap_stream_handshake_request_free(dap_stream_handshake_request_t *a_request)
{
    if (!a_request) {
        return;
    }
    
    if (a_request->alice_pub_key) {
        DAP_DELETE(a_request->alice_pub_key);
    }
    
    if (a_request->alice_signature) {
        DAP_DELETE(a_request->alice_signature);
    }
    
    DAP_DELETE(a_request);
}

/**
 * @brief Create and serialize handshake response to TLV
 * @param a_response Response structure
 * @param a_data_out Output buffer (caller must free)
 * @param a_data_size_out Output size
 * @return 0 on success, negative on error
 */
int dap_stream_handshake_response_create(const dap_stream_handshake_response_t *a_response,
                                          void **a_data_out,
                                          size_t *a_data_size_out)
{
    if (!a_response || !a_data_out || !a_data_size_out) {
        log_it(L_ERROR, "Invalid parameters for handshake response create");
        return -1;
    }
    
    // Calculate buffer size
    size_t l_size = 0;
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // magic
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // version
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint16_t);  // message_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // status
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // session_id
    
    if (a_response->session_timeout > 0) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);
    }
    
    if (a_response->bob_pub_key && a_response->bob_pub_key_size > 0) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + a_response->bob_pub_key_size;
    }
    
    if (a_response->status != 0) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // error_code
        if (a_response->error_message) {
            l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + strlen(a_response->error_message);
        }
    }
    
    uint8_t *l_buffer = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Failed to allocate %zu bytes", l_size);
        return -2;
    }
    
    size_t l_offset = 0;
    int l_ret = 0;
    
    uint32_t l_magic = HTON32(a_response->magic);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MAGIC, &l_magic, sizeof(l_magic));
    
    uint32_t l_version = HTON32(a_response->version);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_VERSION, &l_version, sizeof(l_version));
    
    uint16_t l_msg_type = HTON16(DSHP_MSG_HANDSHAKE_RESPONSE);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MESSAGE_TYPE, &l_msg_type, sizeof(l_msg_type));
    
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_STATUS, &a_response->status, sizeof(a_response->status));
    
    uint32_t l_session_id = HTON32(a_response->session_id);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_SESSION_ID, &l_session_id, sizeof(l_session_id));
    
    if (a_response->session_timeout > 0) {
        uint32_t l_timeout = HTON32(a_response->session_timeout);
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_SESSION_TIMEOUT, &l_timeout, sizeof(l_timeout));
    }
    
    if (a_response->bob_pub_key && a_response->bob_pub_key_size > 0) {
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_BOB_PUB_KEY,
                                                 a_response->bob_pub_key,
                                                 a_response->bob_pub_key_size);
    }
    
    if (a_response->status != 0) {
        uint32_t l_error = HTON32(a_response->error_code);
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_ERROR_CODE, &l_error, sizeof(l_error));
        
        if (a_response->error_message) {
            size_t l_err_len = strlen(a_response->error_message);
            l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                     DSHP_TLV_ERROR_MESSAGE,
                                                     a_response->error_message,
                                                     l_err_len);
        }
    }
    
    if (l_ret != 0) {
        DAP_DELETE(l_buffer);
        return -3;
    }
    
    *a_data_out = l_buffer;
    *a_data_size_out = l_offset;
    
    log_it(L_DEBUG, "Created handshake response: %zu bytes", l_offset);
    return 0;
}

/**
 * @brief Parse handshake response from TLV
 * @param a_data Input data
 * @param a_data_size Input size
 * @param a_response_out Output (caller must free)
 * @return 0 on success
 */
int dap_stream_handshake_response_parse(const void *a_data,
                                         size_t a_data_size,
                                         dap_stream_handshake_response_t **a_response_out)
{
    if (!a_data || !a_response_out) {
        return -1;
    }
    
    dap_stream_handshake_response_t *l_resp = DAP_NEW_Z(dap_stream_handshake_response_t);
    if (!l_resp) {
        return -2;
    }
    
    const uint8_t *l_buffer = (const uint8_t *)a_data;
    size_t l_offset = 0;
    
    while (l_offset < a_data_size) {
        uint16_t l_type;
        const void *l_value;
        uint16_t l_value_size;
        
        if (dap_stream_handshake_tlv_read(l_buffer, &l_offset, a_data_size,
                                           &l_type, &l_value, &l_value_size) != 0) {
            dap_stream_handshake_response_free(l_resp);
            return -3;
        }
        
        switch (l_type) {
            case DSHP_TLV_MAGIC:
                if (l_value_size == sizeof(uint32_t)) {
                    l_resp->magic = NTOH32(*(const uint32_t *)l_value);
                }
                break;
            case DSHP_TLV_VERSION:
                if (l_value_size == sizeof(uint32_t)) {
                    l_resp->version = NTOH32(*(const uint32_t *)l_value);
                }
                break;
            case DSHP_TLV_STATUS:
                if (l_value_size == sizeof(uint8_t)) {
                    l_resp->status = *(const uint8_t *)l_value;
                }
                break;
            case DSHP_TLV_SESSION_ID:
                if (l_value_size == sizeof(uint32_t)) {
                    l_resp->session_id = NTOH32(*(const uint32_t *)l_value);
                }
                break;
            case DSHP_TLV_SESSION_TIMEOUT:
                if (l_value_size == sizeof(uint32_t)) {
                    l_resp->session_timeout = NTOH32(*(const uint32_t *)l_value);
                }
                break;
            case DSHP_TLV_BOB_PUB_KEY:
                l_resp->bob_pub_key_size = l_value_size;
                l_resp->bob_pub_key = DAP_NEW_Z_SIZE(uint8_t, l_value_size);
                if (l_resp->bob_pub_key) {
                    memcpy(l_resp->bob_pub_key, l_value, l_value_size);
                }
                break;
            case DSHP_TLV_ERROR_CODE:
                if (l_value_size == sizeof(uint32_t)) {
                    l_resp->error_code = NTOH32(*(const uint32_t *)l_value);
                }
                break;
            case DSHP_TLV_ERROR_MESSAGE:
                l_resp->error_message = DAP_NEW_Z_SIZE(char, l_value_size + 1);
                if (l_resp->error_message) {
                    memcpy(l_resp->error_message, l_value, l_value_size);
                }
                break;
            default:
                break;
        }
    }
    
    if (dap_stream_handshake_validate_header(l_resp->magic, l_resp->version) != 0) {
        dap_stream_handshake_response_free(l_resp);
        return -4;
    }
    
    *a_response_out = l_resp;
    return 0;
}

/**
 * @brief Free handshake response
 */
void dap_stream_handshake_response_free(dap_stream_handshake_response_t *a_response)
{
    if (!a_response) {
        return;
    }
    
    if (a_response->bob_pub_key) {
        DAP_DELETE(a_response->bob_pub_key);
    }
    
    if (a_response->bob_signature) {
        DAP_DELETE(a_response->bob_signature);
    }
    
    if (a_response->error_message) {
        DAP_DELETE(a_response->error_message);
    }
    
    DAP_DELETE(a_response);
}

// Session create functions follow similar pattern - implementation continues...
// Due to length constraints, providing stub implementations

int dap_stream_session_create_request_create(const dap_stream_session_create_request_t *a_request,
                                               void **a_data_out,
                                               size_t *a_data_size_out)
{
    if (!a_request || !a_data_out || !a_data_size_out) {
        log_it(L_ERROR, "Invalid parameters for session create request");
        return -1;
    }
    
    // Calculate buffer size
    size_t l_size = 0;
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // magic
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // version
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint16_t);  // message_type
    
    if (a_request->channels) {
        size_t l_channels_len = strlen(a_request->channels);
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + l_channels_len;
    }
    
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // enc_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // enc_key_size
    if (a_request->enc_headers) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // enc_headers (optional)
    }
    
    uint8_t *l_buffer = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Failed to allocate %zu bytes", l_size);
        return -2;
    }
    
    size_t l_offset = 0;
    int l_ret = 0;
    
    uint32_t l_magic = HTON32(a_request->magic);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MAGIC, &l_magic, sizeof(l_magic));
    
    uint32_t l_version = HTON32(a_request->version);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_VERSION, &l_version, sizeof(l_version));
    
    uint16_t l_msg_type = HTON16(DSHP_MSG_SESSION_CREATE);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MESSAGE_TYPE, &l_msg_type, sizeof(l_msg_type));
    
    if (a_request->channels) {
        size_t l_channels_len = strlen(a_request->channels);
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_CHANNELS, a_request->channels, 
                                                 l_channels_len);
    }
    
    uint8_t l_enc_type = (uint8_t)a_request->enc_type;
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_ENC_TYPE, &l_enc_type, sizeof(l_enc_type));
    
    uint32_t l_enc_key_size = HTON32(a_request->enc_key_size);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_BLOCK_KEY_SIZE, &l_enc_key_size, sizeof(l_enc_key_size));
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to write TLV fields");
        DAP_DELETE(l_buffer);
        return -3;
    }
    
    *a_data_out = l_buffer;
    *a_data_size_out = l_offset;
    
    log_it(L_DEBUG, "Created session create request (%zu bytes, channels: %s)", 
           l_offset, a_request->channels ? a_request->channels : "none");
    return 0;
}

int dap_stream_session_create_request_parse(const void *a_data,
                                              size_t a_data_size,
                                              dap_stream_session_create_request_t **a_request_out)
{
    if (!a_data || a_data_size == 0 || !a_request_out) {
        log_it(L_ERROR, "Invalid parameters for session create request parse");
        return -1;
    }
    
    dap_stream_session_create_request_t *l_req = DAP_NEW_Z(dap_stream_session_create_request_t);
    if (!l_req) {
        log_it(L_CRITICAL, "Failed to allocate session create request");
        return -2;
    }
    
    const uint8_t *l_data = (const uint8_t *)a_data;
    size_t l_offset = 0;
    
    // Parse TLV fields
    while (l_offset < a_data_size) {
        const dap_stream_handshake_tlv_hdr_t *l_tlv = 
            (const dap_stream_handshake_tlv_hdr_t *)(l_data + l_offset);
        
        if (l_offset + sizeof(dap_stream_handshake_tlv_hdr_t) > a_data_size) {
            log_it(L_ERROR, "Truncated TLV header");
            dap_stream_session_create_request_free(l_req);
            return -3;
        }
        
        uint16_t l_type = NTOH16(l_tlv->type);
        uint16_t l_length = NTOH16(l_tlv->length);
        const uint8_t *l_value = l_data + l_offset + sizeof(dap_stream_handshake_tlv_hdr_t);
        
        if (l_offset + sizeof(dap_stream_handshake_tlv_hdr_t) + l_length > a_data_size) {
            log_it(L_ERROR, "Truncated TLV value");
            dap_stream_session_create_request_free(l_req);
            return -4;
        }
        
        switch (l_type) {
            case DSHP_TLV_MAGIC:
                if (l_length == sizeof(uint32_t)) {
                    l_req->magic = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_VERSION:
                if (l_length == sizeof(uint32_t)) {
                    l_req->version = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_CHANNELS:
                if (l_length > 0) {
                    l_req->channels = DAP_NEW_Z_SIZE(char, l_length + 1);
                    if (l_req->channels) {
                        memcpy(l_req->channels, l_value, l_length);
                        l_req->channels[l_length] = '\0';
                    }
                }
                break;
            case DSHP_TLV_ENC_TYPE:
                if (l_length == sizeof(uint8_t)) {
                    l_req->enc_type = (dap_enc_key_type_t)(*l_value);
                }
                break;
            case DSHP_TLV_BLOCK_KEY_SIZE:
                if (l_length == sizeof(uint32_t)) {
                    l_req->enc_key_size = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            default:
                log_it(L_DEBUG, "Unknown TLV type 0x%04x, skipping", l_type);
                break;
        }
        
        l_offset += sizeof(dap_stream_handshake_tlv_hdr_t) + l_length;
    }
    
    // Validate parsed data
    if (l_req->magic != DAP_STREAM_HANDSHAKE_MAGIC) {
        log_it(L_ERROR, "Invalid magic: 0x%08x", l_req->magic);
        dap_stream_session_create_request_free(l_req);
        return -5;
    }
    
    *a_request_out = l_req;
    log_it(L_DEBUG, "Parsed session create request (channels: %s)", 
           l_req->channels ? l_req->channels : "none");
    return 0;
}

void dap_stream_session_create_request_free(dap_stream_session_create_request_t *a_request)
{
    if (!a_request) {
        return;
    }
    if (a_request->channels) {
        DAP_DELETE(a_request->channels);
    }
    DAP_DELETE(a_request);
}

int dap_stream_session_create_response_create(const dap_stream_session_create_response_t *a_response,
                                                void **a_data_out,
                                                size_t *a_data_size_out)
{
    if (!a_response || !a_data_out || !a_data_size_out) {
        log_it(L_ERROR, "Invalid parameters for session create response");
        return -1;
    }
    
    // Calculate buffer size
    size_t l_size = 0;
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // magic
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // version
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint16_t);  // message_type
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint8_t);   // status
    l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // session_id
    
    if (a_response->status != 0) {
        l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + sizeof(uint32_t);  // error_code
        if (a_response->error_message) {
            size_t l_err_len = strlen(a_response->error_message);
            l_size += sizeof(dap_stream_handshake_tlv_hdr_t) + l_err_len;
        }
    }
    
    uint8_t *l_buffer = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Failed to allocate %zu bytes", l_size);
        return -2;
    }
    
    size_t l_offset = 0;
    int l_ret = 0;
    
    uint32_t l_magic = HTON32(a_response->magic);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MAGIC, &l_magic, sizeof(l_magic));
    
    uint32_t l_version = HTON32(a_response->version);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_VERSION, &l_version, sizeof(l_version));
    
    uint16_t l_msg_type = HTON16(DSHP_MSG_SESSION_CREATE_RESPONSE);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_MESSAGE_TYPE, &l_msg_type, sizeof(l_msg_type));
    
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_STATUS, &a_response->status, sizeof(a_response->status));
    
    uint32_t l_session_id = HTON32(a_response->session_id);
    l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                             DSHP_TLV_SESSION_ID, &l_session_id, sizeof(l_session_id));
    
    if (a_response->status != 0) {
        uint32_t l_error_code = HTON32(a_response->error_code);
        l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                 DSHP_TLV_ERROR_CODE, &l_error_code, sizeof(l_error_code));
        
        if (a_response->error_message) {
            size_t l_err_len = strlen(a_response->error_message);
            l_ret |= dap_stream_handshake_tlv_write(l_buffer, &l_offset, l_size,
                                                     DSHP_TLV_ERROR_MESSAGE, a_response->error_message, l_err_len);
        }
    }
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to write TLV fields");
        DAP_DELETE(l_buffer);
        return -3;
    }
    
    *a_data_out = l_buffer;
    *a_data_size_out = l_offset;
    
    log_it(L_DEBUG, "Created session create response (%zu bytes, status: %u)", 
           l_offset, a_response->status);
    return 0;
}

int dap_stream_session_create_response_parse(const void *a_data,
                                               size_t a_data_size,
                                               dap_stream_session_create_response_t **a_response_out)
{
    if (!a_data || a_data_size == 0 || !a_response_out) {
        log_it(L_ERROR, "Invalid parameters for session create response parse");
        return -1;
    }
    
    dap_stream_session_create_response_t *l_resp = DAP_NEW_Z(dap_stream_session_create_response_t);
    if (!l_resp) {
        log_it(L_CRITICAL, "Failed to allocate session create response");
        return -2;
    }
    
    const uint8_t *l_data = (const uint8_t *)a_data;
    size_t l_offset = 0;
    
    // Parse TLV fields
    while (l_offset < a_data_size) {
        const dap_stream_handshake_tlv_hdr_t *l_tlv = 
            (const dap_stream_handshake_tlv_hdr_t *)(l_data + l_offset);
        
        if (l_offset + sizeof(dap_stream_handshake_tlv_hdr_t) > a_data_size) {
            log_it(L_ERROR, "Truncated TLV header");
            dap_stream_session_create_response_free(l_resp);
            return -3;
        }
        
        uint16_t l_type = NTOH16(l_tlv->type);
        uint16_t l_length = NTOH16(l_tlv->length);
        const uint8_t *l_value = l_data + l_offset + sizeof(dap_stream_handshake_tlv_hdr_t);
        
        if (l_offset + sizeof(dap_stream_handshake_tlv_hdr_t) + l_length > a_data_size) {
            log_it(L_ERROR, "Truncated TLV value");
            dap_stream_session_create_response_free(l_resp);
            return -4;
        }
        
        switch (l_type) {
            case DSHP_TLV_MAGIC:
                if (l_length == sizeof(uint32_t)) {
                    l_resp->magic = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_VERSION:
                if (l_length == sizeof(uint32_t)) {
                    l_resp->version = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_STATUS:
                if (l_length == sizeof(uint8_t)) {
                    l_resp->status = *l_value;
                }
                break;
            case DSHP_TLV_SESSION_ID:
                if (l_length == sizeof(uint32_t)) {
                    l_resp->session_id = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_ERROR_CODE:
                if (l_length == sizeof(uint32_t)) {
                    l_resp->error_code = NTOH32(*(const uint32_t*)l_value);
                }
                break;
            case DSHP_TLV_ERROR_MESSAGE:
                if (l_length > 0) {
                    l_resp->error_message = DAP_NEW_Z_SIZE(char, l_length + 1);
                    if (l_resp->error_message) {
                        memcpy(l_resp->error_message, l_value, l_length);
                        l_resp->error_message[l_length] = '\0';
                    }
                }
                break;
            default:
                log_it(L_DEBUG, "Unknown TLV type 0x%04x, skipping", l_type);
                break;
        }
        
        l_offset += sizeof(dap_stream_handshake_tlv_hdr_t) + l_length;
    }
    
    // Validate parsed data
    if (l_resp->magic != DAP_STREAM_HANDSHAKE_MAGIC) {
        log_it(L_ERROR, "Invalid magic: 0x%08x", l_resp->magic);
        dap_stream_session_create_response_free(l_resp);
        return -5;
    }
    
    *a_response_out = l_resp;
    log_it(L_DEBUG, "Parsed session create response (status: %u, session_id: %u)", 
           l_resp->status, l_resp->session_id);
    return 0;
}

void dap_stream_session_create_response_free(dap_stream_session_create_response_t *a_response)
{
    if (!a_response) {
        return;
    }
    if (a_response->error_message) {
        DAP_DELETE(a_response->error_message);
    }
    DAP_DELETE(a_response);
}

