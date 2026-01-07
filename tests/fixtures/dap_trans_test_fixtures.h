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
 * @file dap_trans_test_fixtures.h
 * @brief Common fixtures for all transport protocol unit tests
 * 
 * Provides shared setup/teardown functions, mock instances, and helper
 * functions for transport-agnostic testing.
 * 
 * @date 2025-01-07
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_events_socket.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_enc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Setup/Teardown Functions
// ============================================================================

/**
 * @brief Common setup for all transport tests
 * 
 * Initializes:
 * - g_config with minimal test configuration
 * - dap_common
 * - dap_mock framework
 * - dap_enc system
 * 
 * Does NOT initialize:
 * - dap_proc_thread (not needed for unit tests)
 * - dap_events (mocked)
 * 
 * @return 0 on success, negative on failure
 */
int dap_trans_test_setup(void);

/**
 * @brief Common teardown for transport tests
 * 
 * Cleanup after each test, resets mocks.
 */
void dap_trans_test_teardown(void);

/**
 * @brief Final cleanup for test suite
 * 
 * Called once at the end of test suite.
 * Deinitializes all systems.
 */
void dap_trans_test_suite_cleanup(void);

// ============================================================================
// Config Helpers for Tests
// ============================================================================

/**
 * @brief Set boolean value in config (for testing)
 * @param a_config Config instance
 * @param a_section Section name
 * @param a_item Item name
 * @param a_value Boolean value
 * @return 0 on success, -1 on failure
 */
int dap_trans_test_config_set_bool(dap_config_t *a_config, const char *a_section, const char *a_item, bool a_value);

/**
 * @brief Set integer value in config (for testing)
 * @param a_config Config instance
 * @param a_section Section name
 * @param a_item Item name
 * @param a_value Integer value
 * @return 0 on success, -1 on failure
 */
int dap_trans_test_config_set_int(dap_config_t *a_config, const char *a_section, const char *a_item, int64_t a_value);

// ============================================================================
// Mock Instances
// ============================================================================

/**
 * @brief Get shared mock server instance
 * @return Pointer to static mock server
 */
dap_server_t* dap_trans_test_get_mock_server(void);

/**
 * @brief Get shared mock stream instance
 * @return Pointer to static mock stream
 */
dap_stream_t* dap_trans_test_get_mock_stream(void);

/**
 * @brief Get shared mock events socket instance
 * @return Pointer to static mock esocket
 */
dap_events_socket_t* dap_trans_test_get_mock_esocket(void);

/**
 * @brief Get shared mock trans context instance
 * @return Pointer to static mock trans_ctx
 */
dap_net_trans_ctx_t* dap_trans_test_get_mock_trans_ctx(void);

// ============================================================================
// DSHP Handshake Helper Functions
// ============================================================================

/**
 * @brief Create DSHP handshake request packet
 * 
 * @param a_enc_type Symmetric encryption algorithm
 * @param a_pkey_exchange_type Key exchange algorithm
 * @param a_pkey_size Public key size
 * @param a_block_size Block size
 * @param a_out_size Output parameter for packet size
 * @return Allocated handshake request packet (caller must free)
 */
uint8_t* dap_trans_test_create_handshake_request(
    dap_enc_key_type_t a_enc_type,
    dap_enc_key_type_t a_pkey_exchange_type,
    uint32_t a_pkey_size,
    uint32_t a_block_size,
    size_t *a_out_size
);

/**
 * @brief Create DSHP handshake response packet
 * 
 * @param a_session_id Session ID from server
 * @param a_status Status code (0 = success)
 * @param a_out_size Output parameter for packet size
 * @return Allocated handshake response packet (caller must free)
 */
uint8_t* dap_trans_test_create_handshake_response(
    uint64_t a_session_id,
    uint8_t a_status,
    size_t *a_out_size
);

/**
 * @brief Parse DSHP handshake request from buffer
 * 
 * @param a_data Buffer containing handshake request
 * @param a_size Buffer size
 * @param a_request_out Output parameter for parsed request
 * @return 0 on success, negative on error
 */
int dap_trans_test_parse_handshake_request(
    const uint8_t *a_data,
    size_t a_size,
    dap_stream_handshake_request_t *a_request_out
);

/**
 * @brief Parse DSHP handshake response from buffer
 * 
 * @param a_data Buffer containing handshake response
 * @param a_size Buffer size
 * @param a_response_out Output parameter for parsed response
 * @return 0 on success, negative on error
 */
int dap_trans_test_parse_handshake_response(
    const uint8_t *a_data,
    size_t a_size,
    dap_stream_handshake_response_t *a_response_out
);

// ============================================================================
// Session Helper Functions
// ============================================================================

/**
 * @brief Create session create packet (encrypted)
 * 
 * @param a_session_id Session ID
 * @param a_channels Comma-separated list of channels
 * @param a_key Encryption key for payload
 * @param a_out_size Output parameter for packet size
 * @return Allocated session create packet (caller must free)
 */
uint8_t* dap_trans_test_create_session_create(
    uint64_t a_session_id,
    const char *a_channels,
    dap_enc_key_t *a_key,
    size_t *a_out_size
);

/**
 * @brief Create session create response packet
 * 
 * @param a_status Status code (0 = success)
 * @param a_kdf_counter KDF ratcheting counter
 * @param a_out_size Output parameter for packet size
 * @return Allocated session response packet (caller must free)
 */
uint8_t* dap_trans_test_create_session_response(
    uint8_t a_status,
    uint64_t a_kdf_counter,
    size_t *a_out_size
);

// ============================================================================
// Encryption Helper Functions
// ============================================================================

/**
 * @brief Verify encryption/decryption round-trip
 * 
 * @param a_key Encryption key
 * @param a_data Data to encrypt
 * @param a_size Data size
 * @return true if round-trip successful, false otherwise
 */
bool dap_trans_test_verify_encryption_roundtrip(
    dap_enc_key_t *a_key,
    const uint8_t *a_data,
    size_t a_size
);

/**
 * @brief Generate test encryption key
 * 
 * @param a_type Key type
 * @param a_size Key size (for symmetric keys)
 * @return Generated key (caller must free with dap_enc_key_delete)
 */
dap_enc_key_t* dap_trans_test_generate_key(
    dap_enc_key_type_t a_type,
    size_t a_size
);

// ============================================================================
// Stream Packet Helper Functions
// ============================================================================

/**
 * @brief Create stream packet with signature
 * 
 * @param a_type Packet type
 * @param a_data Packet payload
 * @param a_size Payload size
 * @param a_out_size Output parameter for total packet size
 * @return Allocated stream packet (caller must free)
 */
uint8_t* dap_trans_test_create_stream_packet(
    uint8_t a_type,
    const uint8_t *a_data,
    size_t a_size,
    size_t *a_out_size
);

/**
 * @brief Verify stream packet signature
 * 
 * @param a_packet Packet buffer
 * @param a_size Packet size
 * @return true if signature valid, false otherwise
 */
bool dap_trans_test_verify_stream_packet_signature(
    const uint8_t *a_packet,
    size_t a_size
);

// ============================================================================
// Validation Helper Functions
// ============================================================================

/**
 * @brief Check if buffer contains valid DSHP magic
 * 
 * @param a_data Buffer to check
 * @param a_size Buffer size
 * @return true if magic valid, false otherwise
 */
bool dap_trans_test_has_valid_dshp_magic(
    const uint8_t *a_data,
    size_t a_size
);

/**
 * @brief Check if buffer contains valid stream packet signature
 * 
 * @param a_data Buffer to check
 * @param a_size Buffer size
 * @return true if signature present and valid, false otherwise
 */
bool dap_trans_test_has_valid_stream_signature(
    const uint8_t *a_data,
    size_t a_size
);

#ifdef __cplusplus
}
#endif

