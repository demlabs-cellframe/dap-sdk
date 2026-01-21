/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_trans_test_fixtures.c
 * @brief Implementation of common fixtures for transport tests
 * @date 2025-01-07
 */

#include <stddef.h>
#include <string.h>
#include <arpa/inet.h>
#include "dap_trans_test_fixtures.h"
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_strfuncs.h"
#include "dap_stream_pkt.h"
#include "json-c/json.h"

#define LOG_TAG "dap_trans_test_fixtures"

// ============================================================================
// Static Mock Instances
// ============================================================================

static dap_server_t s_mock_server = {0};
static dap_stream_t s_mock_stream = {0};
static dap_events_socket_t s_mock_esocket = {0};
static dap_net_trans_ctx_t s_mock_trans_ctx = {0};

static bool s_fixtures_initialized = false;

// ============================================================================
// Setup/Teardown Implementation
// ============================================================================

int dap_trans_test_setup(void)
{
    if (s_fixtures_initialized) {
        return 0; // Already initialized
    }

    // Initialize DAP common FIRST (sets up paths and logging)
    int l_ret = dap_common_init("test_trans", NULL);
    if (l_ret != 0) {
        log_it(L_ERROR, "dap_common_init failed: %d", l_ret);
        return -1;
    }

    // CRITICAL: Enable log output to stderr (otherwise logs go to /dev/null!)
    dap_log_set_external_output(LOGGER_OUTPUT_STDERR, NULL);

    // Don't try to open config from file for tests, but create an empty config
    g_config = DAP_NEW_Z(dap_config_t);
    if (g_config) {
        // Minimal logging for performance tests
        dap_trans_test_config_set_bool(g_config, "general", "debug_more", false);
        dap_trans_test_config_set_int(g_config, "general", "log_level", L_WARNING);
    }
    
    // Set log level to WARNING (only errors and warnings)
    dap_log_level_set(L_NOTICE);
    

    // Initialize encryption system
    dap_enc_init();

    // Initialize mock framework
    dap_mock_init();

    s_fixtures_initialized = true;
    log_it(L_NOTICE, "Transport test fixtures initialized");

    return 0;
}

void dap_trans_test_teardown(void)
{
    // Reset all mocks for next test
    dap_mock_reset_all();
}

void dap_trans_test_suite_cleanup(void)
{
    if (!s_fixtures_initialized) {
        return;
    }

    // Deinitialize systems
    dap_mock_deinit();
    // Note: dap_common_deinit and dap_enc_deinit are typically not called
    // in unit tests to avoid interference between test runs

    // Close config
    if (g_config) {
        // Just free the config structure (items are internally managed)
        DAP_DELETE(g_config);
        g_config = NULL;
    }

    s_fixtures_initialized = false;
    log_it(L_NOTICE, "Transport test fixtures cleaned up");
}

// ============================================================================
// Config Helpers for Tests
// ============================================================================

// Internal dap_config_item structure (copied from dap_config.c)
typedef struct dap_config_item {
    char type, *name;
    union dap_config_val {
        bool        val_bool;
        char        *val_str;
        char        **val_arr;
        int64_t     val_int;
    } val;
    UT_hash_handle hh;
} dap_config_item_t;

int dap_trans_test_config_set_bool(dap_config_t *a_config, const char *a_section, const char *a_item, bool a_value)
{
    if (!a_config || !a_section || !a_item) {
        return -1;
    }

    // Create full key "section.item"
    char *l_key = dap_strdup_printf("%s.%s", a_section, a_item);
    if (!l_key) {
        return -1;
    }

    // Find existing item or create new
    dap_config_item_t *l_item = NULL;
    HASH_FIND_STR(a_config->items, l_key, l_item);
    
    if (l_item) {
        // Update existing
        l_item->type = DAP_CONFIG_ITEM_BOOL;
        l_item->val.val_bool = a_value;
    } else {
        // Create new
        l_item = DAP_NEW_Z(dap_config_item_t);
        if (!l_item) {
            DAP_DELETE(l_key);
            return -1;
        }
        l_item->name = l_key;
        l_item->type = DAP_CONFIG_ITEM_BOOL;
        l_item->val.val_bool = a_value;
        HASH_ADD_KEYPTR(hh, a_config->items, l_item->name, strlen(l_item->name), l_item);
        l_key = NULL; // Ownership transferred
    }

    DAP_DELETE(l_key);
    return 0;
}

int dap_trans_test_config_set_int(dap_config_t *a_config, const char *a_section, const char *a_item, int64_t a_value)
{
    if (!a_config || !a_section || !a_item) {
        return -1;
    }

    // Create full key "section.item"
    char *l_key = dap_strdup_printf("%s.%s", a_section, a_item);
    if (!l_key) {
        return -1;
    }

    // Find existing item or create new
    dap_config_item_t *l_item = NULL;
    HASH_FIND_STR(a_config->items, l_key, l_item);
    
    if (l_item) {
        // Update existing
        l_item->type = DAP_CONFIG_ITEM_DECIMAL;
        l_item->val.val_int = a_value;
    } else {
        // Create new
        l_item = DAP_NEW_Z(dap_config_item_t);
        if (!l_item) {
            DAP_DELETE(l_key);
            return -1;
        }
        l_item->name = l_key;
        l_item->type = DAP_CONFIG_ITEM_DECIMAL;
        l_item->val.val_int = a_value;
        HASH_ADD_KEYPTR(hh, a_config->items, l_item->name, strlen(l_item->name), l_item);
        l_key = NULL; // Ownership transferred
    }

    DAP_DELETE(l_key);
    return 0;
}

// ============================================================================
// Mock Instance Getters
// ============================================================================

dap_server_t* dap_trans_test_get_mock_server(void)
{
    return &s_mock_server;
}

dap_stream_t* dap_trans_test_get_mock_stream(void)
{
    return &s_mock_stream;
}

dap_events_socket_t* dap_trans_test_get_mock_esocket(void)
{
    return &s_mock_esocket;
}

dap_net_trans_ctx_t* dap_trans_test_get_mock_trans_ctx(void)
{
    return &s_mock_trans_ctx;
}

// ============================================================================
// DSHP Handshake Helpers
// ============================================================================

uint8_t* dap_trans_test_create_handshake_request(
    dap_enc_key_type_t a_enc_type,
    dap_enc_key_type_t a_pkey_exchange_type,
    uint32_t a_pkey_size,
    uint32_t a_block_size,
    size_t *a_out_size)
{
    if (!a_out_size) {
        return NULL;
    }

    // Create JSON object for handshake using json-c
    struct json_object *l_json = json_object_new_object();
    if (!l_json) {
        return NULL;
    }
    
    json_object_object_add(l_json, "magic", json_object_new_int64(DAP_STREAM_HANDSHAKE_MAGIC));
    json_object_object_add(l_json, "version", json_object_new_int64(DAP_STREAM_HANDSHAKE_VERSION));
    json_object_object_add(l_json, "enc_type", json_object_new_int(a_enc_type));
    json_object_object_add(l_json, "pkey_exchange_type", json_object_new_int(a_pkey_exchange_type));
    json_object_object_add(l_json, "pkey_size", json_object_new_int64(a_pkey_size));
    json_object_object_add(l_json, "block_size", json_object_new_int64(a_block_size));

    // Serialize to JSON string
    const char *l_json_str = json_object_to_json_string(l_json);
    if (!l_json_str) {
        json_object_put(l_json);
        return NULL;
    }

    size_t l_json_len = strlen(l_json_str);
    uint8_t *l_packet = DAP_NEW_SIZE(uint8_t, l_json_len + 1);
    if (!l_packet) {
        json_object_put(l_json);
        return NULL;
    }

    memcpy(l_packet, l_json_str, l_json_len);
    l_packet[l_json_len] = '\0';
    
    json_object_put(l_json); // Decrements ref count

    *a_out_size = l_json_len;
    return l_packet;
}

uint8_t* dap_trans_test_create_handshake_response(
    uint64_t a_session_id,
    uint8_t a_status,
    size_t *a_out_size)
{
    if (!a_out_size) {
        return NULL;
    }

    // Create JSON object for response using json-c
    struct json_object *l_json = json_object_new_object();
    if (!l_json) {
        return NULL;
    }
    
    json_object_object_add(l_json, "session_id", json_object_new_int64(a_session_id));
    json_object_object_add(l_json, "status", json_object_new_int(a_status));

    // Serialize to JSON string
    const char *l_json_str = json_object_to_json_string(l_json);
    if (!l_json_str) {
        json_object_put(l_json);
        return NULL;
    }

    size_t l_json_len = strlen(l_json_str);
    uint8_t *l_packet = DAP_NEW_SIZE(uint8_t, l_json_len + 1);
    if (!l_packet) {
        json_object_put(l_json);
        return NULL;
    }

    memcpy(l_packet, l_json_str, l_json_len);
    l_packet[l_json_len] = '\0';
    
    json_object_put(l_json);

    *a_out_size = l_json_len;
    return l_packet;
}

int dap_trans_test_parse_handshake_request(
    const uint8_t *a_data,
    size_t a_size,
    dap_stream_handshake_request_t *a_request_out)
{
    if (!a_data || !a_request_out || a_size == 0) {
        return -1;
    }

    // Parse JSON using json-c
    struct json_object *l_json = json_tokener_parse((const char*)a_data);
    if (!l_json) {
        log_it(L_ERROR, "Failed to parse handshake request JSON");
        return -2;
    }

    // Extract fields
    struct json_object *l_magic = NULL, *l_version = NULL, *l_enc_type = NULL;
    struct json_object *l_pkey_type = NULL, *l_pkey_size = NULL, *l_block_size = NULL;
    
    if (!json_object_object_get_ex(l_json, "magic", &l_magic) ||
        !json_object_object_get_ex(l_json, "version", &l_version) ||
        !json_object_object_get_ex(l_json, "enc_type", &l_enc_type) ||
        !json_object_object_get_ex(l_json, "pkey_exchange_type", &l_pkey_type)) {
        json_object_put(l_json);
        return -3;
    }

    memset(a_request_out, 0, sizeof(dap_stream_handshake_request_t));
    a_request_out->magic = (uint32_t)json_object_get_int64(l_magic);
    a_request_out->version = (uint32_t)json_object_get_int64(l_version);
    a_request_out->enc_type = (dap_enc_key_type_t)json_object_get_int(l_enc_type);
    a_request_out->pkey_exchange_type = (dap_enc_key_type_t)json_object_get_int(l_pkey_type);
    
    if (json_object_object_get_ex(l_json, "pkey_size", &l_pkey_size)) {
        a_request_out->pkey_exchange_size = (uint32_t)json_object_get_int64(l_pkey_size);
    }
    if (json_object_object_get_ex(l_json, "block_size", &l_block_size)) {
        a_request_out->block_key_size = (uint32_t)json_object_get_int64(l_block_size);
    }

    json_object_put(l_json);
    return 0;
}

int dap_trans_test_parse_handshake_response(
    const uint8_t *a_data,
    size_t a_size,
    dap_stream_handshake_response_t *a_response_out)
{
    if (!a_data || !a_response_out || a_size == 0) {
        return -1;
    }

    // Parse JSON using json-c
    struct json_object *l_json = json_tokener_parse((const char*)a_data);
    if (!l_json) {
        log_it(L_ERROR, "Failed to parse handshake response JSON");
        return -2;
    }

    // Extract fields
    struct json_object *l_session_id = NULL, *l_status = NULL;
    
    if (!json_object_object_get_ex(l_json, "session_id", &l_session_id) ||
        !json_object_object_get_ex(l_json, "status", &l_status)) {
        json_object_put(l_json);
        return -3;
    }

    memset(a_response_out, 0, sizeof(dap_stream_handshake_response_t));
    a_response_out->session_id = (uint64_t)json_object_get_int64(l_session_id);
    a_response_out->status = (uint8_t)json_object_get_int(l_status);

    json_object_put(l_json);
    return 0;
}

// ============================================================================
// Session Helpers
// ============================================================================

uint8_t* dap_trans_test_create_session_create(
    uint64_t a_session_id,
    const char *a_channels,
    dap_enc_key_t *a_key,
    size_t *a_out_size)
{
    if (!a_out_size) {
        return NULL;
    }

    // Create JSON payload using json-c
    struct json_object *l_json = json_object_new_object();
    if (!l_json) {
        return NULL;
    }
    
    json_object_object_add(l_json, "session_id", json_object_new_int64(a_session_id));
    if (a_channels) {
        json_object_object_add(l_json, "channels", json_object_new_string(a_channels));
    }

    // Serialize to JSON string
    const char *l_json_str = json_object_to_json_string(l_json);
    if (!l_json_str) {
        json_object_put(l_json);
        return NULL;
    }

    size_t l_json_len = strlen(l_json_str);
    
    // Encrypt if key provided
    if (a_key) {
        size_t l_encrypted_max = l_json_len * 2;
        uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
        if (!l_encrypted) {
            json_object_put(l_json);
            return NULL;
        }

        size_t l_encrypted_size = dap_enc_code(a_key, (const uint8_t*)l_json_str, l_json_len,
                                              l_encrypted, l_encrypted_max, DAP_ENC_DATA_TYPE_RAW);
        json_object_put(l_json);

        if (l_encrypted_size == 0) {
            DAP_DELETE(l_encrypted);
            return NULL;
        }

        *a_out_size = l_encrypted_size;
        return l_encrypted;
    }

    // Return unencrypted
    uint8_t *l_packet = DAP_NEW_SIZE(uint8_t, l_json_len + 1);
    if (!l_packet) {
        json_object_put(l_json);
        return NULL;
    }

    memcpy(l_packet, l_json_str, l_json_len);
    l_packet[l_json_len] = '\0';
    json_object_put(l_json);

    *a_out_size = l_json_len;
    return l_packet;
}

uint8_t* dap_trans_test_create_session_response(
    uint8_t a_status,
    uint64_t a_kdf_counter,
    size_t *a_out_size)
{
    if (!a_out_size) {
        return NULL;
    }

    // Create simple response packet with counter in network byte order
    uint8_t *l_packet = DAP_NEW_SIZE(uint8_t, 9); // 1 byte status + 8 bytes counter
    if (!l_packet) {
        return NULL;
    }

    l_packet[0] = a_status;
    uint64_t l_counter_be = htobe64(a_kdf_counter);
    memcpy(l_packet + 1, &l_counter_be, sizeof(uint64_t));

    *a_out_size = 9;
    return l_packet;
}

// ============================================================================
// Encryption Helpers
// ============================================================================

bool dap_trans_test_verify_encryption_roundtrip(
    dap_enc_key_t *a_key,
    const uint8_t *a_data,
    size_t a_size)
{
    if (!a_key || !a_data || a_size == 0) {
        return false;
    }

    // Encrypt
    size_t l_encrypted_max = a_size * 2;
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        return false;
    }

    size_t l_encrypted_size = dap_enc_code(a_key, a_data, a_size,
                                          l_encrypted, l_encrypted_max,
                                          DAP_ENC_DATA_TYPE_RAW);
    if (l_encrypted_size == 0) {
        DAP_DELETE(l_encrypted);
        return false;
    }

    // Decrypt
    size_t l_decrypted_max = l_encrypted_size * 2;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        DAP_DELETE(l_encrypted);
        return false;
    }

    size_t l_decrypted_size = dap_enc_decode(a_key, l_encrypted, l_encrypted_size,
                                            l_decrypted, l_decrypted_max,
                                            DAP_ENC_DATA_TYPE_RAW);
    DAP_DELETE(l_encrypted);

    if (l_decrypted_size != a_size) {
        DAP_DELETE(l_decrypted);
        return false;
    }

    // Compare
    bool l_result = (memcmp(a_data, l_decrypted, a_size) == 0);
    DAP_DELETE(l_decrypted);

    return l_result;
}

dap_enc_key_t* dap_trans_test_generate_key(
    dap_enc_key_type_t a_type,
    size_t a_size)
{
    dap_enc_key_t *l_key = dap_enc_key_new_generate(a_type, NULL, 0, NULL, 0, a_size);
    if (!l_key) {
        log_it(L_ERROR, "Failed to generate encryption key of type %d with size %zu", a_type, a_size);
        return NULL;
    }

    return l_key;
}

// ============================================================================
// Stream Packet Helpers
// ============================================================================

uint8_t* dap_trans_test_create_stream_packet(
    uint8_t a_type,
    const uint8_t *a_data,
    size_t a_size,
    size_t *a_out_size)
{
    if (!a_out_size) {
        return NULL;
    }

    // Stream packet = signature + header + payload
    extern const uint8_t c_dap_stream_sig[8]; // Defined in dap_stream_pkt.c
    
    size_t l_packet_size = sizeof(c_dap_stream_sig) + sizeof(dap_stream_pkt_hdr_t) + a_size;
    uint8_t *l_packet = DAP_NEW_Z_SIZE(uint8_t, l_packet_size);
    if (!l_packet) {
        return NULL;
    }

    // Copy signature
    memcpy(l_packet, c_dap_stream_sig, sizeof(c_dap_stream_sig));

    // Fill header
    dap_stream_pkt_hdr_t *l_hdr = (dap_stream_pkt_hdr_t*)(l_packet + sizeof(c_dap_stream_sig));
    memcpy(l_hdr->sig, c_dap_stream_sig, sizeof(l_hdr->sig));
    l_hdr->type = a_type;
    l_hdr->size = (uint32_t)a_size;

    // Copy payload
    if (a_data && a_size > 0) {
        memcpy(l_packet + sizeof(c_dap_stream_sig) + sizeof(dap_stream_pkt_hdr_t), a_data, a_size);
    }

    *a_out_size = l_packet_size;
    return l_packet;
}

bool dap_trans_test_verify_stream_packet_signature(
    const uint8_t *a_packet,
    size_t a_size)
{
    extern const uint8_t c_dap_stream_sig[8];
    
    if (!a_packet || a_size < sizeof(c_dap_stream_sig)) {
        return false;
    }

    return (memcmp(a_packet, c_dap_stream_sig, sizeof(c_dap_stream_sig)) == 0);
}

// ============================================================================
// Validation Helpers
// ============================================================================

bool dap_trans_test_has_valid_dshp_magic(
    const uint8_t *a_data,
    size_t a_size)
{
    if (!a_data || a_size < sizeof(uint32_t)) {
        return false;
    }

    // Try to parse as JSON and check magic field using json-c
    struct json_object *l_json = json_tokener_parse((const char*)a_data);
    if (!l_json) {
        return false;
    }

    struct json_object *l_magic = NULL;
    bool l_result = false;
    
    if (json_object_object_get_ex(l_json, "magic", &l_magic)) {
        uint32_t l_magic_val = (uint32_t)json_object_get_int64(l_magic);
        l_result = (l_magic_val == DAP_STREAM_HANDSHAKE_MAGIC);
    }

    json_object_put(l_json);
    return l_result;
}

bool dap_trans_test_has_valid_stream_signature(
    const uint8_t *a_data,
    size_t a_size)
{
    return dap_trans_test_verify_stream_packet_signature(a_data, a_size);
}

