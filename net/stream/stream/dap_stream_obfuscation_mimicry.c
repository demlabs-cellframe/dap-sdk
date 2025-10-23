/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
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

#include <string.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_stream_obfuscation_mimicry.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_mimicry"

// TLS content types
#define TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC  0x14
#define TLS_CONTENT_TYPE_ALERT               0x15
#define TLS_CONTENT_TYPE_HANDSHAKE           0x16
#define TLS_CONTENT_TYPE_APPLICATION_DATA    0x17

// TLS handshake message types
#define TLS_HANDSHAKE_CLIENT_HELLO  0x01
#define TLS_HANDSHAKE_SERVER_HELLO  0x02

// WebSocket opcodes
#define WS_OPCODE_CONTINUATION  0x00
#define WS_OPCODE_TEXT          0x01
#define WS_OPCODE_BINARY        0x02
#define WS_OPCODE_CLOSE         0x08
#define WS_OPCODE_PING          0x09
#define WS_OPCODE_PONG          0x0A

// Internal state
typedef struct dap_stream_mimicry_internal {
    uint64_t packet_count;
    uint32_t tls_sequence_number;
    char *sni_hostname;
} dap_stream_mimicry_internal_t;

// Protocol mimicry engine structure
struct dap_stream_mimicry {
    dap_stream_mimicry_config_t config;
    dap_stream_mimicry_internal_t *internal;
};

// Forward declarations
static int s_wrap_https(dap_stream_mimicry_t *a_mimicry,
                        const void *a_data, size_t a_data_size,
                        void **a_out_data, size_t *a_out_size);
static int s_unwrap_https(dap_stream_mimicry_t *a_mimicry,
                          const void *a_data, size_t a_data_size,
                          void **a_out_data, size_t *a_out_size);
static int s_wrap_websocket(dap_stream_mimicry_t *a_mimicry,
                            const void *a_data, size_t a_data_size,
                            void **a_out_data, size_t *a_out_size);
static int s_unwrap_websocket(dap_stream_mimicry_t *a_mimicry,
                              const void *a_data, size_t a_data_size,
                              void **a_out_data, size_t *a_out_size);

//=============================================================================
// Public API Implementation
//=============================================================================

/**
 * @brief Create protocol mimicry engine with default configuration
 */
dap_stream_mimicry_t *dap_stream_mimicry_create(void)
{
    dap_stream_mimicry_config_t l_config = 
        dap_stream_mimicry_config_for_protocol(DAP_STREAM_MIMICRY_HTTPS);
    return dap_stream_mimicry_create_with_config(&l_config);
}

/**
 * @brief Create protocol mimicry engine with custom configuration
 */
dap_stream_mimicry_t *dap_stream_mimicry_create_with_config(
    const dap_stream_mimicry_config_t *a_config)
{
    if (!a_config) {
        log_it(L_ERROR, "Cannot create mimicry engine with NULL config");
        return NULL;
    }

    dap_stream_mimicry_t *l_mimicry = DAP_NEW_Z(dap_stream_mimicry_t);
    if (!l_mimicry) {
        log_it(L_CRITICAL, "Memory allocation failed for mimicry engine");
        return NULL;
    }

    dap_stream_mimicry_internal_t *l_internal = DAP_NEW_Z(dap_stream_mimicry_internal_t);
    if (!l_internal) {
        log_it(L_CRITICAL, "Memory allocation failed for internal state");
        DAP_DELETE(l_mimicry);
        return NULL;
    }

    memcpy(&l_mimicry->config, a_config, sizeof(dap_stream_mimicry_config_t));
    l_mimicry->internal = l_internal;

    l_internal->packet_count = 0;
    l_internal->tls_sequence_number = 0;
    
    if (a_config->https.sni_hostname) {
        l_internal->sni_hostname = dap_strdup(a_config->https.sni_hostname);
    } else {
        l_internal->sni_hostname = dap_strdup("www.google.com");  // Default SNI
    }

    log_it(L_INFO, "Protocol mimicry engine created (protocol=%d, browser=%d, SNI=%s)",
           a_config->protocol, a_config->browser, l_internal->sni_hostname);

    return l_mimicry;
}

/**
 * @brief Destroy protocol mimicry engine
 */
void dap_stream_mimicry_destroy(dap_stream_mimicry_t *a_mimicry)
{
    if (!a_mimicry)
        return;

    if (a_mimicry->internal) {
        if (a_mimicry->internal->sni_hostname) {
            DAP_DELETE(a_mimicry->internal->sni_hostname);
        }
        DAP_DELETE(a_mimicry->internal);
    }

    DAP_DELETE(a_mimicry);
    log_it(L_DEBUG, "Protocol mimicry engine destroyed");
}

/**
 * @brief Wrap data in protocol-specific format
 */
int dap_stream_mimicry_wrap(dap_stream_mimicry_t *a_mimicry,
                             const void *a_data, size_t a_data_size,
                             void **a_out_data, size_t *a_out_size)
{
    if (!a_mimicry || !a_data || !a_out_data || !a_out_size) {
        log_it(L_ERROR, "Invalid arguments for mimicry wrap");
        return -1;
    }

    switch (a_mimicry->config.protocol) {
        case DAP_STREAM_MIMICRY_HTTPS:
            return s_wrap_https(a_mimicry, a_data, a_data_size, a_out_data, a_out_size);
        
        case DAP_STREAM_MIMICRY_WEBSOCKET:
            return s_wrap_websocket(a_mimicry, a_data, a_data_size, a_out_data, a_out_size);
        
        case DAP_STREAM_MIMICRY_NONE:
            // No wrapping - just copy
            *a_out_data = DAP_DUP_SIZE(a_data, a_data_size);
            *a_out_size = a_data_size;
            return 0;
        
        default:
            log_it(L_ERROR, "Unsupported mimicry protocol: %d", a_mimicry->config.protocol);
            return -1;
    }
}

/**
 * @brief Unwrap data from protocol-specific format
 */
int dap_stream_mimicry_unwrap(dap_stream_mimicry_t *a_mimicry,
                               const void *a_data, size_t a_data_size,
                               void **a_out_data, size_t *a_out_size)
{
    if (!a_mimicry || !a_data || !a_out_data || !a_out_size) {
        log_it(L_ERROR, "Invalid arguments for mimicry unwrap");
        return -1;
    }

    switch (a_mimicry->config.protocol) {
        case DAP_STREAM_MIMICRY_HTTPS:
            return s_unwrap_https(a_mimicry, a_data, a_data_size, a_out_data, a_out_size);
        
        case DAP_STREAM_MIMICRY_WEBSOCKET:
            return s_unwrap_websocket(a_mimicry, a_data, a_data_size, a_out_data, a_out_size);
        
        case DAP_STREAM_MIMICRY_NONE:
            // No unwrapping - just copy
            *a_out_data = DAP_DUP_SIZE(a_data, a_data_size);
            *a_out_size = a_data_size;
            return 0;
        
        default:
            log_it(L_ERROR, "Unsupported mimicry protocol: %d", a_mimicry->config.protocol);
            return -1;
    }
}

/**
 * @brief Generate TLS ClientHello message
 */
int dap_stream_mimicry_generate_client_hello(dap_stream_mimicry_t *a_mimicry,
                                              void **a_client_hello,
                                              size_t *a_hello_size)
{
    if (!a_mimicry || !a_client_hello || !a_hello_size) {
        log_it(L_ERROR, "Invalid arguments for ClientHello generation");
        return -1;
    }

    // Simplified ClientHello - real implementation would be much more complex
    // For production, use a proper TLS library or detailed browser fingerprints
    
    size_t l_sni_len = a_mimicry->internal->sni_hostname ? 
                       strlen(a_mimicry->internal->sni_hostname) : 0;
    
    // Approximate size: header + version + random + session_id + ciphers + compression + extensions + SNI
    size_t l_hello_size = 5 + 2 + 32 + 1 + 32 + 2 + 50 + l_sni_len + 20;
    
    uint8_t *l_hello = DAP_NEW_Z_SIZE(uint8_t, l_hello_size);
    if (!l_hello) {
        log_it(L_CRITICAL, "Memory allocation failed for ClientHello");
        return -1;
    }

    size_t l_offset = 0;
    
    // TLS record header
    l_hello[l_offset++] = TLS_CONTENT_TYPE_HANDSHAKE;
    uint16_t l_version = htons(a_mimicry->config.tls_version);
    memcpy(&l_hello[l_offset], &l_version, 2);
    l_offset += 2;
    
    // Length placeholder (will be filled later)
    l_offset += 2;
    
    // Handshake message header
    l_hello[l_offset++] = TLS_HANDSHAKE_CLIENT_HELLO;
    l_offset += 3;  // Length (24-bit, filled later)
    
    // Client version
    memcpy(&l_hello[l_offset], &l_version, 2);
    l_offset += 2;
    
    // Random (32 bytes)
    dap_random_bytes(&l_hello[l_offset], 32);
    l_offset += 32;
    
    // Session ID (empty for now)
    l_hello[l_offset++] = 0;
    
    // Cipher suites (2 bytes length + ciphers)
    l_hello[l_offset++] = 0;
    l_hello[l_offset++] = 16;  // 8 cipher suites
    
    // Add some common cipher suites (2 bytes each)
    uint16_t l_ciphers[] = {
        0x1301,  // TLS_AES_128_GCM_SHA256
        0x1302,  // TLS_AES_256_GCM_SHA384
        0x1303,  // TLS_CHACHA20_POLY1305_SHA256
        0xc02f,  // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
        0xc030,  // TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
        0xcca8,  // TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
        0xc02b,  // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
        0xc02c   // TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
    };
    
    for (size_t i = 0; i < sizeof(l_ciphers) / sizeof(l_ciphers[0]); i++) {
        uint16_t l_cipher = htons(l_ciphers[i]);
        memcpy(&l_hello[l_offset], &l_cipher, 2);
        l_offset += 2;
    }
    
    // Compression methods (1 byte length + null compression)
    l_hello[l_offset++] = 1;
    l_hello[l_offset++] = 0;  // No compression
    
    // Extensions (simplified - SNI only for now)
    if (l_sni_len > 0) {
        uint16_t l_ext_len = htons(l_sni_len + 9);
        memcpy(&l_hello[l_offset], &l_ext_len, 2);
        l_offset += 2;
        
        // SNI extension
        uint16_t l_sni_type = htons(0);  // server_name extension
        memcpy(&l_hello[l_offset], &l_sni_type, 2);
        l_offset += 2;
        
        uint16_t l_sni_ext_len = htons(l_sni_len + 5);
        memcpy(&l_hello[l_offset], &l_sni_ext_len, 2);
        l_offset += 2;
        
        uint16_t l_sni_list_len = htons(l_sni_len + 3);
        memcpy(&l_hello[l_offset], &l_sni_list_len, 2);
        l_offset += 2;
        
        l_hello[l_offset++] = 0;  // host_name type
        
        uint16_t l_hostname_len = htons(l_sni_len);
        memcpy(&l_hello[l_offset], &l_hostname_len, 2);
        l_offset += 2;
        
        memcpy(&l_hello[l_offset], a_mimicry->internal->sni_hostname, l_sni_len);
        l_offset += l_sni_len;
    }

    *a_client_hello = l_hello;
    *a_hello_size = l_offset;

    log_it(L_DEBUG, "Generated TLS ClientHello: %zu bytes", l_offset);
    return 0;
}

/**
 * @brief Generate TLS ServerHello message
 */
int dap_stream_mimicry_generate_server_hello(dap_stream_mimicry_t *a_mimicry,
                                              const void *a_client_hello,
                                              size_t a_client_hello_size,
                                              void **a_server_hello,
                                              size_t *a_hello_size)
{
    UNUSED(a_client_hello);
    UNUSED(a_client_hello_size);
    
    if (!a_mimicry || !a_server_hello || !a_hello_size) {
        log_it(L_ERROR, "Invalid arguments for ServerHello generation");
        return -1;
    }

    // Simplified ServerHello
    size_t l_hello_size = 5 + 2 + 32 + 1 + 2 + 1 + 10;
    
    uint8_t *l_hello = DAP_NEW_Z_SIZE(uint8_t, l_hello_size);
    if (!l_hello) {
        log_it(L_CRITICAL, "Memory allocation failed for ServerHello");
        return -1;
    }

    size_t l_offset = 0;
    
    // TLS record header
    l_hello[l_offset++] = TLS_CONTENT_TYPE_HANDSHAKE;
    uint16_t l_version = htons(a_mimicry->config.tls_version);
    memcpy(&l_hello[l_offset], &l_version, 2);
    l_offset += 2;
    
    // Length placeholder
    l_offset += 2;
    
    // Handshake message header
    l_hello[l_offset++] = TLS_HANDSHAKE_SERVER_HELLO;
    l_offset += 3;  // Length (24-bit)
    
    // Server version
    memcpy(&l_hello[l_offset], &l_version, 2);
    l_offset += 2;
    
    // Random (32 bytes)
    dap_random_bytes(&l_hello[l_offset], 32);
    l_offset += 32;
    
    // Session ID (empty)
    l_hello[l_offset++] = 0;
    
    // Chosen cipher suite
    uint16_t l_cipher = htons(0x1301);  // TLS_AES_128_GCM_SHA256
    memcpy(&l_hello[l_offset], &l_cipher, 2);
    l_offset += 2;
    
    // Compression method
    l_hello[l_offset++] = 0;  // No compression

    *a_server_hello = l_hello;
    *a_hello_size = l_offset;

    log_it(L_DEBUG, "Generated TLS ServerHello: %zu bytes", l_offset);
    return 0;
}

/**
 * @brief Set SNI hostname
 */
int dap_stream_mimicry_set_sni(dap_stream_mimicry_t *a_mimicry,
                                const char *a_hostname)
{
    if (!a_mimicry || !a_hostname) {
        log_it(L_ERROR, "Invalid arguments for set SNI");
        return -1;
    }

    if (a_mimicry->internal->sni_hostname) {
        DAP_DELETE(a_mimicry->internal->sni_hostname);
    }

    a_mimicry->internal->sni_hostname = dap_strdup(a_hostname);
    a_mimicry->config.https.sni_hostname = a_mimicry->internal->sni_hostname;

    log_it(L_INFO, "SNI hostname set to: %s", a_hostname);
    return 0;
}

/**
 * @brief Set target protocol
 */
int dap_stream_mimicry_set_protocol(dap_stream_mimicry_t *a_mimicry,
                                     dap_stream_mimicry_protocol_t a_protocol)
{
    if (!a_mimicry) {
        return -1;
    }

    a_mimicry->config.protocol = a_protocol;
    log_it(L_INFO, "Mimicry protocol set to: %d", a_protocol);
    return 0;
}

/**
 * @brief Set browser type for fingerprinting
 */
int dap_stream_mimicry_set_browser(dap_stream_mimicry_t *a_mimicry,
                                    dap_stream_browser_type_t a_browser)
{
    if (!a_mimicry) {
        return -1;
    }

    a_mimicry->config.browser = a_browser;
    log_it(L_INFO, "Mimicry browser set to: %d", a_browser);
    return 0;
}

/**
 * @brief Get current configuration
 */
int dap_stream_mimicry_get_config(dap_stream_mimicry_t *a_mimicry,
                                   dap_stream_mimicry_config_t *a_config)
{
    if (!a_mimicry || !a_config) {
        return -1;
    }

    memcpy(a_config, &a_mimicry->config, sizeof(dap_stream_mimicry_config_t));
    return 0;
}

/**
 * @brief Update configuration
 */
int dap_stream_mimicry_set_config(dap_stream_mimicry_t *a_mimicry,
                                   const dap_stream_mimicry_config_t *a_config)
{
    if (!a_mimicry || !a_config) {
        return -1;
    }

    memcpy(&a_mimicry->config, a_config, sizeof(dap_stream_mimicry_config_t));
    log_it(L_INFO, "Mimicry configuration updated");
    return 0;
}

/**
 * @brief Create default configuration for protocol
 */
dap_stream_mimicry_config_t dap_stream_mimicry_config_for_protocol(
    dap_stream_mimicry_protocol_t a_protocol)
{
    dap_stream_mimicry_config_t l_config;
    memset(&l_config, 0, sizeof(l_config));
    
    l_config.protocol = a_protocol;
    l_config.tls_version = DAP_STREAM_TLS_1_3;
    l_config.browser = DAP_STREAM_BROWSER_CHROME;
    
    switch (a_protocol) {
        case DAP_STREAM_MIMICRY_HTTPS:
            l_config.https.sni_hostname = "www.google.com";
            l_config.https.use_realistic_cipher_suites = true;
            l_config.https.emulate_extensions = true;
            l_config.https.add_grease = true;
            break;
        
        case DAP_STREAM_MIMICRY_HTTP2:
            l_config.http2.initial_window_size = 65535;
            l_config.http2.use_hpack_compression = true;
            break;
        
        case DAP_STREAM_MIMICRY_WEBSOCKET:
            l_config.websocket.mask_client_frames = true;
            l_config.websocket.ping_interval_ms = 30000;
            break;
        
        default:
            break;
    }
    
    return l_config;
}

/**
 * @brief Validate wrapped packet
 */
bool dap_stream_mimicry_validate_packet(dap_stream_mimicry_t *a_mimicry,
                                         const void *a_data,
                                         size_t a_data_size)
{
    if (!a_mimicry || !a_data || a_data_size < 5) {
        return false;
    }

    const uint8_t *l_data = (const uint8_t*)a_data;

    switch (a_mimicry->config.protocol) {
        case DAP_STREAM_MIMICRY_HTTPS: {
            // Check TLS record header
            uint8_t l_content_type = l_data[0];
            uint16_t l_version = ntohs(*(uint16_t*)&l_data[1]);
            uint16_t l_length = ntohs(*(uint16_t*)&l_data[3]);
            
            // Valid TLS content types
            if (l_content_type < TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC ||
                l_content_type > TLS_CONTENT_TYPE_APPLICATION_DATA) {
                return false;
            }
            
            // Valid TLS versions
            if (l_version != DAP_STREAM_TLS_1_2 && l_version != DAP_STREAM_TLS_1_3) {
                return false;
            }
            
            // Check length
            if (l_length + 5 > a_data_size) {
                return false;
            }
            
            return true;
        }
        
        case DAP_STREAM_MIMICRY_WEBSOCKET: {
            // Check WebSocket frame header
            uint8_t l_byte0 = l_data[0];
            uint8_t l_fin = (l_byte0 >> 7) & 0x01;
            uint8_t l_opcode = l_byte0 & 0x0F;
            
            UNUSED(l_fin);
            
            // Valid WebSocket opcodes
            if (l_opcode > WS_OPCODE_PONG && l_opcode != WS_OPCODE_CLOSE) {
                return false;
            }
            
            return true;
        }
        
        default:
            return true;
    }
}

//=============================================================================
// Internal Implementation
//=============================================================================

/**
 * @brief Wrap data in HTTPS (TLS) format
 */
static int s_wrap_https(dap_stream_mimicry_t *a_mimicry,
                        const void *a_data, size_t a_data_size,
                        void **a_out_data, size_t *a_out_size)
{
    // Allocate buffer for TLS record header + data
    size_t l_total_size = sizeof(dap_stream_tls_record_header_t) + a_data_size;
    uint8_t *l_output = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_output) {
        log_it(L_CRITICAL, "Memory allocation failed for TLS wrapping");
        return -1;
    }

    // Fill TLS record header
    dap_stream_tls_record_header_t *l_header = (dap_stream_tls_record_header_t*)l_output;
    l_header->content_type = TLS_CONTENT_TYPE_APPLICATION_DATA;
    l_header->version = htons(a_mimicry->config.tls_version);
    l_header->length = htons(a_data_size);

    // Copy payload
    memcpy(l_output + sizeof(dap_stream_tls_record_header_t), a_data, a_data_size);

    *a_out_data = l_output;
    *a_out_size = l_total_size;

    a_mimicry->internal->packet_count++;
    
    log_it(L_DEBUG, "Wrapped %zu bytes in TLS record", a_data_size);
    return 0;
}

/**
 * @brief Unwrap data from HTTPS (TLS) format
 */
static int s_unwrap_https(dap_stream_mimicry_t *a_mimicry,
                          const void *a_data, size_t a_data_size,
                          void **a_out_data, size_t *a_out_size)
{
    if (a_data_size < sizeof(dap_stream_tls_record_header_t)) {
        log_it(L_ERROR, "Data too small for TLS record header");
        return -1;
    }

    const dap_stream_tls_record_header_t *l_header = 
        (const dap_stream_tls_record_header_t*)a_data;
    
    uint16_t l_payload_length = ntohs(l_header->length);
    
    if (sizeof(dap_stream_tls_record_header_t) + l_payload_length > a_data_size) {
        log_it(L_ERROR, "TLS record length mismatch");
        return -1;
    }

    // Extract payload
    const uint8_t *l_payload = (const uint8_t*)a_data + sizeof(dap_stream_tls_record_header_t);
    *a_out_data = DAP_DUP_SIZE(l_payload, l_payload_length);
    *a_out_size = l_payload_length;

    log_it(L_DEBUG, "Unwrapped %u bytes from TLS record", l_payload_length);
    return 0;
}

/**
 * @brief Wrap data in WebSocket format
 */
static int s_wrap_websocket(dap_stream_mimicry_t *a_mimicry,
                            const void *a_data, size_t a_data_size,
                            void **a_out_data, size_t *a_out_size)
{
    // WebSocket frame header: 2 bytes minimum (FIN + opcode + mask + payload length)
    // For lengths > 125: +2 bytes (16-bit length)
    // For lengths > 65535: +8 bytes (64-bit length)
    // If masked: +4 bytes (masking key)
    
    size_t l_header_size = 2;
    if (a_data_size > 125) {
        if (a_data_size > 65535)
            l_header_size += 8;
        else
            l_header_size += 2;
    }
    
    bool l_mask = a_mimicry->config.websocket.mask_client_frames;
    if (l_mask)
        l_header_size += 4;  // Masking key
    
    size_t l_total_size = l_header_size + a_data_size;
    uint8_t *l_output = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_output) {
        log_it(L_CRITICAL, "Memory allocation failed for WebSocket wrapping");
        return -1;
    }

    size_t l_offset = 0;
    
    // Byte 0: FIN + RSV + opcode
    l_output[l_offset++] = 0x80 | WS_OPCODE_BINARY;  // FIN=1, opcode=binary
    
    // Byte 1: MASK + payload length
    uint8_t l_mask_bit = l_mask ? 0x80 : 0x00;
    if (a_data_size <= 125) {
        l_output[l_offset++] = l_mask_bit | (uint8_t)a_data_size;
    } else if (a_data_size <= 65535) {
        l_output[l_offset++] = l_mask_bit | 126;
        uint16_t l_len = htons(a_data_size);
        memcpy(&l_output[l_offset], &l_len, 2);
        l_offset += 2;
    } else {
        l_output[l_offset++] = l_mask_bit | 127;
        uint64_t l_len = htobe64(a_data_size);
        memcpy(&l_output[l_offset], &l_len, 8);
        l_offset += 8;
    }
    
    // Masking key (if masked)
    uint8_t l_masking_key[4] = {0};
    if (l_mask) {
        dap_random_bytes(l_masking_key, 4);
        memcpy(&l_output[l_offset], l_masking_key, 4);
        l_offset += 4;
    }
    
    // Payload (masked if necessary)
    const uint8_t *l_data = (const uint8_t*)a_data;
    for (size_t i = 0; i < a_data_size; i++) {
        if (l_mask) {
            l_output[l_offset++] = l_data[i] ^ l_masking_key[i % 4];
        } else {
            l_output[l_offset++] = l_data[i];
        }
    }

    *a_out_data = l_output;
    *a_out_size = l_total_size;

    a_mimicry->internal->packet_count++;
    
    log_it(L_DEBUG, "Wrapped %zu bytes in WebSocket frame (masked=%d)", a_data_size, l_mask);
    return 0;
}

/**
 * @brief Unwrap data from WebSocket format
 */
static int s_unwrap_websocket(dap_stream_mimicry_t *a_mimicry,
                              const void *a_data, size_t a_data_size,
                              void **a_out_data, size_t *a_out_size)
{
    UNUSED(a_mimicry);
    
    if (a_data_size < 2) {
        log_it(L_ERROR, "Data too small for WebSocket frame");
        return -1;
    }

    const uint8_t *l_data = (const uint8_t*)a_data;
    size_t l_offset = 0;
    
    // Parse byte 0
    l_offset++;
    
    // Parse byte 1
    uint8_t l_byte1 = l_data[l_offset++];
    bool l_masked = (l_byte1 & 0x80) != 0;
    uint64_t l_payload_len = l_byte1 & 0x7F;
    
    // Extended payload length
    if (l_payload_len == 126) {
        if (a_data_size < l_offset + 2) {
            log_it(L_ERROR, "Invalid WebSocket frame length");
            return -1;
        }
        l_payload_len = ntohs(*(uint16_t*)&l_data[l_offset]);
        l_offset += 2;
    } else if (l_payload_len == 127) {
        if (a_data_size < l_offset + 8) {
            log_it(L_ERROR, "Invalid WebSocket frame length");
            return -1;
        }
        l_payload_len = be64toh(*(uint64_t*)&l_data[l_offset]);
        l_offset += 8;
    }
    
    // Masking key
    uint8_t l_masking_key[4] = {0};
    if (l_masked) {
        if (a_data_size < l_offset + 4) {
            log_it(L_ERROR, "Invalid WebSocket frame - missing masking key");
            return -1;
        }
        memcpy(l_masking_key, &l_data[l_offset], 4);
        l_offset += 4;
    }
    
    // Check total size
    if (a_data_size < l_offset + l_payload_len) {
        log_it(L_ERROR, "WebSocket frame payload length mismatch");
        return -1;
    }
    
    // Extract and unmask payload
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_len);
    if (!l_payload) {
        log_it(L_CRITICAL, "Memory allocation failed for WebSocket payload");
        return -1;
    }
    
    for (uint64_t i = 0; i < l_payload_len; i++) {
        if (l_masked) {
            l_payload[i] = l_data[l_offset + i] ^ l_masking_key[i % 4];
        } else {
            l_payload[i] = l_data[l_offset + i];
        }
    }

    *a_out_data = l_payload;
    *a_out_size = l_payload_len;

    log_it(L_DEBUG, "Unwrapped %lu bytes from WebSocket frame", l_payload_len);
    return 0;
}

