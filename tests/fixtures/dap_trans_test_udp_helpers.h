/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2026
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
 * @file dap_trans_test_udp_helpers.h
 * @brief UDP-specific test helpers for transport unit tests
 * 
 * Provides analysis and validation utilities for UDP transport testing:
 * - Packet capture via mock framework (NOT manual mocking!)
 * - Encryption/decryption validation helpers
 * - Internal header parsing and validation
 * - Kyber512 handshake validation
 * - KDF ratcheting validation
 * - Obfuscation validation
 * 
 * NOTE: All mocking is done via DAP_MOCK framework!
 *       This file provides ONLY analysis/validation utilities!
 * 
 * @date 2026-01-08
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_events_socket.h"
#include "dap_stream.h"
#include "dap_net_trans_udp_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// UDP PACKET CAPTURE STORAGE (populated by mock framework)
// ============================================================================

/**
 * @brief Maximum captured packet size
 */
#define DAP_UDP_TEST_MAX_PACKET_SIZE 2048

/**
 * @brief Captured packet information (populated by mocked dap_events_socket_write_unsafe)
 */
typedef struct dap_udp_test_captured_packet {
    uint8_t data[DAP_UDP_TEST_MAX_PACKET_SIZE];  ///< Packet data
    size_t size;                                   ///< Actual packet size
    struct sockaddr_storage dest_addr;             ///< Destination address
    socklen_t dest_addr_len;                       ///< Address length
    bool is_valid;                                 ///< Packet captured flag
} dap_udp_test_captured_packet_t;

/**
 * @brief Get pointer to captured packet storage
 * 
 * This storage is populated by mocked dap_events_socket_write_unsafe
 * via DAP_MOCK_WRAPPER_CUSTOM callback
 */
dap_udp_test_captured_packet_t* dap_udp_test_get_captured_packet(void);

/**
 * @brief Reset captured packet
 */
void dap_udp_test_reset_captured_packet(void);

// ============================================================================
// PACKET VALIDATION HELPERS (work with captured data from mocks)
// ============================================================================

/**
 * @brief Decrypt and parse UDP packet internal header
 * 
 * Analyzes packet captured by mock framework
 * 
 * @param a_packet Encrypted packet data
 * @param a_packet_size Packet size
 * @param a_key Encryption key for decryption
 * @param a_out_type Packet type (output)
 * @param a_out_seq_num Sequence number (output)
 * @param a_out_session_id Session ID (output)
 * @param a_out_payload Decrypted payload (output, caller must free)
 * @param a_out_payload_size Payload size (output)
 * @return 0 on success, -1 on error
 */
int dap_udp_test_decrypt_and_parse_packet(
    const uint8_t *a_packet,
    size_t a_packet_size,
    dap_enc_key_t *a_key,
    uint8_t *a_out_type,
    uint32_t *a_out_seq_num,
    uint64_t *a_out_session_id,
    uint8_t **a_out_payload,
    size_t *a_out_payload_size
);

/**
 * @brief Validate encrypted packet format
 * 
 * Checks packet captured by mock framework:
 * - Minimum size for internal header + encryption overhead
 * - Valid encryption (can be decrypted)
 * - Valid internal header structure
 * 
 * @param a_packet Packet data
 * @param a_packet_size Packet size
 * @param a_key Encryption key for validation
 * @param a_expected_type Expected packet type (or 0xFF for any)
 * @return true if valid, false otherwise
 */
bool dap_udp_test_validate_encrypted_packet(
    const uint8_t *a_packet,
    size_t a_packet_size,
    dap_enc_key_t *a_key,
    uint8_t a_expected_type
);

// ============================================================================
// CRYPTO VALIDATION HELPERS
// ============================================================================

/**
 * @brief Validate Kyber512 handshake exchange
 * 
 * Validates that Alice's public key and Bob's ciphertext
 * can derive identical shared secrets
 * 
 * @param a_alice_pubkey Alice's public key
 * @param a_alice_pubkey_size Alice's public key size
 * @param a_bob_ciphertext Bob's ciphertext
 * @param a_bob_ciphertext_size Bob's ciphertext size
 * @param a_out_handshake_key Derived handshake key (output, caller must delete)
 * @return 0 on success, -1 on error
 */
int dap_udp_test_validate_kyber_handshake(
    const uint8_t *a_alice_pubkey,
    size_t a_alice_pubkey_size,
    const uint8_t *a_bob_ciphertext,
    size_t a_bob_ciphertext_size,
    dap_enc_key_t **a_out_handshake_key
);

/**
 * @brief Validate KDF ratcheting for session key derivation
 * 
 * Validates that session key is correctly derived from handshake key
 * using KDF-SHAKE256 with counter
 * 
 * @param a_handshake_key Handshake key
 * @param a_counter KDF counter
 * @param a_context KDF context string
 * @param a_out_session_key Derived session key (output, caller must delete)
 * @return 0 on success, -1 on error
 */
int dap_udp_test_validate_kdf_ratcheting(
    dap_enc_key_t *a_handshake_key,
    uint64_t a_counter,
    const char *a_context,
    dap_enc_key_t **a_out_session_key
);

// ============================================================================
// OBFUSCATION VALIDATION HELPERS
// ============================================================================

/**
 * @brief Validate handshake obfuscation/deobfuscation roundtrip
 * 
 * Tests that obfuscated handshake can be correctly deobfuscated
 * 
 * @param a_handshake Original handshake data
 * @param a_handshake_size Handshake size
 * @param a_obfuscated_min_size Expected minimum obfuscated size
 * @param a_obfuscated_max_size Expected maximum obfuscated size
 * @return true if roundtrip is successful, false otherwise
 */
bool dap_udp_test_validate_obfuscation_roundtrip(
    const uint8_t *a_handshake,
    size_t a_handshake_size,
    size_t a_obfuscated_min_size,
    size_t a_obfuscated_max_size
);

// ============================================================================
// SEQUENCE NUMBER VALIDATION
// ============================================================================

/**
 * @brief Validate sequence number monotonicity for replay protection
 * 
 * @param a_packets Array of captured packets (from mock)
 * @param a_packet_count Number of packets
 * @param a_key Encryption key for decryption
 * @return true if sequence numbers are strictly increasing, false otherwise
 */
bool dap_udp_test_validate_sequence_numbers(
    const dap_udp_test_captured_packet_t *a_packets,
    size_t a_packet_count,
    dap_enc_key_t *a_key
);

// ============================================================================
// MOCK CONTEXT CREATION (for tests that need UDP context without real socket)
// ============================================================================

/**
 * @brief Create mock UDP client context for testing
 * 
 * Allocates and initializes a minimal dap_net_trans_udp_ctx_t with:
 * - Session ID
 * - Sequence number (starting at 1)
 * - Handshake key (generated)
 * - Mock esocket (for address storage)
 * - Mock stream
 * 
 * @param a_session_id Session ID for this context
 * @param a_key_type Encryption key type (e.g., DAP_ENC_KEY_TYPE_SALSA2012)
 * @param a_dest_addr Destination IP address (string)
 * @param a_dest_port Destination port
 * @return Allocated context or NULL on error
 * 
 * @note Caller must free with dap_udp_test_cleanup_mock_client_ctx()
 */
dap_net_trans_udp_ctx_t* dap_udp_test_create_mock_client_ctx(
    uint64_t a_session_id,
    dap_enc_key_type_t a_key_type,
    const char *a_dest_addr,
    uint16_t a_dest_port
);

/**
 * @brief Create mock UDP server context for testing
 * 
 * Similar to client context but for server-side testing
 * 
 * @param a_session_id Session ID for this context
 * @param a_key_type Encryption key type
 * @param a_remote_addr Remote client IP address (string)
 * @param a_remote_port Remote client port
 * @return Allocated context or NULL on error
 * 
 * @note Caller must free with dap_udp_test_cleanup_mock_client_ctx()
 */
dap_net_trans_udp_ctx_t* dap_udp_test_create_mock_server_ctx(
    uint64_t a_session_id,
    dap_enc_key_type_t a_key_type,
    const char *a_remote_addr,
    uint16_t a_remote_port
);

// ============================================================================
// DAP_MOCK_CUSTOM DECLARATION
// ============================================================================

/**
 * @brief Mock declaration for dap_events_socket_write_unsafe
 * 
 * This mock is defined in dap_trans_test_udp_helpers.c using DAP_MOCK_WRAPPER_CUSTOM.
 * It automatically captures UDP packets for validation in tests.
 * 
 * Tests MUST include this header and call DAP_MOCK_ENABLE(dap_events_socket_write_unsafe)
 * to activate packet capture.
 * 
 * Usage in tests:
 * ```c
 * #include "dap_trans_test_udp_helpers.h"
 * 
 * // In test function:
 * DAP_MOCK_ENABLE(dap_events_socket_write_unsafe);
 * 
 * // Call function under test that writes UDP packets
 * trans->ops->write(...);
 * 
 * // Validate captured packet
 * dap_udp_test_captured_packet_t *packet = dap_udp_test_get_captured_packet();
 * TEST_ASSERT(packet->is_valid, "Packet should be captured");
 * ```
 */

/**
 * @brief Cleanup mock UDP client context and free all resources
 * 
 * This function deallocates:
 * - UDP context structure
 * - Handshake key (if present)
 * - Mock esocket (if present)
 * 
 * @param a_ctx UDP context to cleanup
 */
void dap_udp_test_cleanup_mock_client_ctx(dap_net_trans_udp_ctx_t *a_ctx);

#ifdef __cplusplus
}
#endif

