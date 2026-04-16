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

#include "dap_trans_test_udp_helpers.h"
#include "dap_trans_test_fixtures.h"  // For dap_trans_test_get_mock_stream/ctx
#include "dap_common.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_enc_kdf.h"
#include "dap_transport_obfuscation.h"
#include "dap_mock.h"
#include <inttypes.h>
#include <string.h>
#include <arpa/inet.h>

#define LOG_TAG "test_udp_helpers"

// NOTE: DAP_MOCK_DECLARE is in dap_trans_test_udp_helpers.h (not here!)
// This ensures the mock state is globally visible to all test files

// ============================================================================
// PACKET CAPTURE STORAGE
// ============================================================================

static dap_udp_test_captured_packet_t s_captured_packet = {0};

dap_udp_test_captured_packet_t* dap_udp_test_get_captured_packet(void)
{
    return &s_captured_packet;
}

void dap_udp_test_reset_captured_packet(void)
{
    memset(&s_captured_packet, 0, sizeof(s_captured_packet));
}

// ============================================================================
// MOCK UDP CONTEXT MANAGEMENT
// ============================================================================

dap_net_trans_udp_ctx_t* dap_udp_test_create_mock_client_ctx(
    uint64_t a_session_id,
    dap_enc_key_type_t a_key_type,
    const char *a_dest_addr,
    uint16_t a_dest_port)
{
    // Allocate context
    dap_net_trans_udp_ctx_t *l_ctx = DAP_NEW_Z(dap_net_trans_udp_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to allocate UDP context");
        return NULL;
    }
    
    // Initialize basic fields
    l_ctx->session_id = a_session_id;
    l_ctx->seq_num = 1; // Start from 1
    
    // Generate encryption key
    l_ctx->handshake_key = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, 0, 0);
    if (!l_ctx->handshake_key) {
        log_it(L_ERROR, "Failed to generate encryption key");
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Create mock stream
    l_ctx->stream = DAP_NEW_Z(dap_stream_t);
    if (!l_ctx->stream) {
        log_it(L_ERROR, "Failed to allocate stream");
        dap_enc_key_delete(l_ctx->handshake_key);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Create mock esocket
    l_ctx->esocket = DAP_NEW_Z(dap_events_socket_t);
    if (!l_ctx->esocket) {
        log_it(L_ERROR, "Failed to allocate esocket");
        DAP_DELETE(l_ctx->stream);
        dap_enc_key_delete(l_ctx->handshake_key);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Setup esocket for UDP
    l_ctx->esocket->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_ctx->esocket->socket = 100; // Mock FD
    l_ctx->esocket->flags = DAP_SOCK_READY_TO_WRITE;
    
    // Setup destination address
    struct sockaddr_in *l_addr = (struct sockaddr_in*)&l_ctx->esocket->addr_storage;
    l_addr->sin_family = AF_INET;
    l_addr->sin_port = htons(a_dest_port);
    if (inet_pton(AF_INET, a_dest_addr, &l_addr->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid destination address: %s", a_dest_addr);
        DAP_DELETE(l_ctx->esocket);
        DAP_DELETE(l_ctx->stream);
        dap_enc_key_delete(l_ctx->handshake_key);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    l_ctx->esocket->addr_size = sizeof(struct sockaddr_in);
    
    // CRITICAL: Also setup remote_addr in UDP context (used by s_udp_write_typed)
    memcpy(&l_ctx->remote_addr, &l_ctx->esocket->addr_storage, sizeof(struct sockaddr_in));
    l_ctx->remote_addr_len = sizeof(struct sockaddr_in);
    
    log_it(L_DEBUG, "Created mock client UDP context: session_id=0x%" PRIx64 ", dest=%s:%u",
           a_session_id, a_dest_addr, a_dest_port);
    
    return l_ctx;
}

void dap_udp_test_cleanup_mock_client_ctx(dap_net_trans_udp_ctx_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }
    
    // Delete handshake key if present
    if (a_ctx->handshake_key) {
        dap_enc_key_delete(a_ctx->handshake_key);
        a_ctx->handshake_key = NULL;
    }
    
    // Delete esocket if present
    if (a_ctx->esocket) {
        DAP_DELETE(a_ctx->esocket);
        a_ctx->esocket = NULL;
    }
    
    // Delete stream if present
    if (a_ctx->stream) {
        DAP_DELETE(a_ctx->stream);
        a_ctx->stream = NULL;
    }
    
    // Delete context itself
    DAP_DELETE(a_ctx);
    
    log_it(L_DEBUG, "Cleaned up mock client UDP context");
}

dap_net_trans_udp_ctx_t* dap_udp_test_create_mock_server_ctx(
    uint64_t a_session_id,
    dap_enc_key_type_t a_key_type,
    const char *a_remote_addr,
    uint16_t a_remote_port)
{
    // Allocate context
    dap_net_trans_udp_ctx_t *l_ctx = DAP_NEW_Z(dap_net_trans_udp_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to allocate UDP context");
        return NULL;
    }
    
    // Initialize basic fields
    l_ctx->session_id = a_session_id;
    l_ctx->seq_num = 1;
    
    // Generate encryption key
    l_ctx->handshake_key = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, 0, 0);
    if (!l_ctx->handshake_key) {
        log_it(L_ERROR, "Failed to generate encryption key");
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Setup remote address
    struct sockaddr_in *l_addr = (struct sockaddr_in*)&l_ctx->remote_addr;
    l_addr->sin_family = AF_INET;
    l_addr->sin_port = htons(a_remote_port);
    if (inet_pton(AF_INET, a_remote_addr, &l_addr->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid remote address: %s", a_remote_addr);
        dap_enc_key_delete(l_ctx->handshake_key);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Create mock listener esocket
    l_ctx->listener_esocket = DAP_NEW_Z(dap_events_socket_t);
    if (!l_ctx->listener_esocket) {
        log_it(L_ERROR, "Failed to allocate listener esocket");
        dap_enc_key_delete(l_ctx->handshake_key);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    l_ctx->listener_esocket->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_ctx->listener_esocket->socket = 200; // Mock FD
    l_ctx->listener_esocket->flags = DAP_SOCK_READY_TO_WRITE;
    
    log_it(L_DEBUG, "Created mock server UDP context: session_id=0x%" PRIx64 ", remote=%s:%u",
           a_session_id, a_remote_addr, a_remote_port);
    
    return l_ctx;
}

void dap_udp_test_delete_mock_ctx(dap_net_trans_udp_ctx_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }
    
    if (a_ctx->handshake_key) {
        dap_enc_key_delete(a_ctx->handshake_key);
    }
    
    if (a_ctx->esocket) {
        DAP_DELETE(a_ctx->esocket);
    }
    
    if (a_ctx->listener_esocket) {
        DAP_DELETE(a_ctx->listener_esocket);
    }
    
    if (a_ctx->stream) {
        DAP_DELETE(a_ctx->stream);
    }
    
    DAP_DELETE(a_ctx);
}

// ============================================================================
// MOCK STREAM SETUP WITH SESSION (High-level helpers for write tests)
// ============================================================================

dap_stream_t* dap_udp_test_setup_mock_stream_with_session(
    dap_net_trans_t *a_trans,
    dap_net_trans_udp_ctx_t *a_udp_ctx)
{
    if (!a_trans || !a_udp_ctx) {
        log_it(L_ERROR, "Invalid parameters for mock stream setup");
        return NULL;
    }
    
    if (!a_udp_ctx->handshake_key) {
        log_it(L_ERROR, "UDP context must have handshake_key for session");
        return NULL;
    }
    
    // Get global mock instances
    dap_stream_t *l_stream = dap_trans_test_get_mock_stream();
    dap_net_trans_ctx_t *l_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    
    // Reset trans_ctx to clean state (critical for test isolation!)
    memset(l_trans_ctx, 0, sizeof(*l_trans_ctx));
    
    l_trans_ctx->transport_priv = a_udp_ctx;
    l_stream->esocket = a_udp_ctx->esocket;
    l_stream->trans = a_trans;
    l_stream->trans_ctx = l_trans_ctx;
    
    // Create session with encryption key
    // NOTE: We reuse existing session if present, just update fields
    if (!l_stream->session) {
        l_stream->session = DAP_NEW_Z(dap_stream_session_t);
        if (!l_stream->session) {
            log_it(L_ERROR, "Failed to allocate stream session");
            return NULL;
        }
    }
    
    l_stream->session->key = a_udp_ctx->handshake_key;  // Link to UDP context key
    l_stream->session->id = (uint32_t)a_udp_ctx->session_id;
    
    log_it(L_DEBUG, "Setup mock stream with session: session_id=0x%" PRIx64 ", key=%p",
           a_udp_ctx->session_id, a_udp_ctx->handshake_key);
    
    return l_stream;
}

void dap_udp_test_cleanup_mock_stream(dap_stream_t *a_stream)
{
    if (!a_stream) {
        return;
    }
    
    // Cleanup session (but DON'T delete the key - it belongs to UDP context)
    if (a_stream->session) {
        a_stream->session->key = NULL;  // Clear pointer, key is owned by UDP context
        DAP_DELETE(a_stream->session);
        a_stream->session = NULL;
    }
    
    // Reset trans_ctx to clean state for next test
    dap_net_trans_ctx_t *l_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    memset(l_trans_ctx, 0, sizeof(*l_trans_ctx));
    
    log_it(L_DEBUG, "Cleaned up mock stream and trans_ctx");
}

// ============================================================================
// CRYPTO KEY HELPERS
// ============================================================================

dap_enc_key_t* dap_udp_test_generate_test_key(
    dap_enc_key_type_t a_key_type,
    uint32_t a_seed)
{
    // Use seed as base for deterministic key generation
    uint8_t l_seed_buf[32];
    for (size_t i = 0; i < sizeof(l_seed_buf); i++) {
        l_seed_buf[i] = (uint8_t)((a_seed + i) & 0xFF);
    }
    
    dap_enc_key_t *l_key = dap_enc_key_new_generate(
        a_key_type, l_seed_buf, sizeof(l_seed_buf), NULL, 0, 0);
    
    if (!l_key) {
        log_it(L_ERROR, "Failed to generate test key with seed %u", a_seed);
    }
    
    return l_key;
}

int dap_udp_test_generate_kyber_keypair(
    dap_enc_key_t **a_out_alice_key,
    dap_enc_key_t **a_out_bob_key)
{
    if (!a_out_alice_key || !a_out_bob_key) {
        return -1;
    }
    
    // Generate Alice's key pair
    *a_out_alice_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!*a_out_alice_key) {
        log_it(L_ERROR, "Failed to generate Alice's Kyber512 key");
        return -1;
    }
    
    // Generate Bob's key pair
    *a_out_bob_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!*a_out_bob_key) {
        log_it(L_ERROR, "Failed to generate Bob's Kyber512 key");
        dap_enc_key_delete(*a_out_alice_key);
        *a_out_alice_key = NULL;
        return -1;
    }
    
    log_it(L_DEBUG, "Generated Kyber512 key pairs for Alice and Bob");
    return 0;
}

// ============================================================================
// PACKET VALIDATION HELPERS
// ============================================================================

int dap_udp_test_decrypt_and_parse_packet(
    const uint8_t *a_packet,
    size_t a_packet_size,
    dap_enc_key_t *a_key,
    uint8_t *a_out_type,
    uint32_t *a_out_seq_num,
    uint64_t *a_out_session_id,
    uint8_t **a_out_payload,
    size_t *a_out_payload_size)
{
    if (!a_packet || !a_key || !a_out_type || !a_out_seq_num || 
        !a_out_session_id || !a_out_payload || !a_out_payload_size) {
        return -1;
    }
    
    // Decrypt packet
    size_t l_decrypt_buf_size = a_packet_size + 256;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypt_buf_size);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -1;
    }
    
    size_t l_decrypted_size = dap_enc_decode(a_key, a_packet, a_packet_size, 
                                          l_decrypted, l_decrypt_buf_size,
                                          DAP_ENC_DATA_TYPE_RAW);
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "Failed to decrypt packet");
        DAP_DELETE(l_decrypted);
        return -1;
    }
    
    // Validate minimum size for internal header
    if (l_decrypted_size < sizeof(dap_stream_trans_udp_encrypted_header_t)) {
        log_it(L_ERROR, "Decrypted packet too small: %zu < %zu",
               l_decrypted_size, (size_t)sizeof(dap_stream_trans_udp_encrypted_header_t));
        DAP_DELETE(l_decrypted);
        return -1;
    }
    
    // Parse internal header
    dap_stream_trans_udp_encrypted_header_t *l_hdr = 
        (dap_stream_trans_udp_encrypted_header_t*)l_decrypted;
    
    *a_out_type = l_hdr->type;
    *a_out_seq_num = be32toh(l_hdr->seq_num);
    *a_out_session_id = be64toh(l_hdr->session_id);
    
    // Extract payload
    size_t l_payload_size = l_decrypted_size - sizeof(dap_stream_trans_udp_encrypted_header_t);
    if (l_payload_size > 0) {
        *a_out_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
        if (!*a_out_payload) {
            log_it(L_ERROR, "Failed to allocate payload buffer");
            DAP_DELETE(l_decrypted);
            return -1;
        }
        memcpy(*a_out_payload, l_decrypted + sizeof(dap_stream_trans_udp_encrypted_header_t), 
               l_payload_size);
        *a_out_payload_size = l_payload_size;
    } else {
        *a_out_payload = NULL;
        *a_out_payload_size = 0;
    }
    
    DAP_DELETE(l_decrypted);
    
    log_it(L_DEBUG, "Decrypted packet: type=%u, seq=%u, session=0x%" PRIx64 ", payload=%zu bytes",
           *a_out_type, *a_out_seq_num, *a_out_session_id, *a_out_payload_size);
    
    return 0;
}

bool dap_udp_test_validate_encrypted_packet(
    const uint8_t *a_packet,
    size_t a_packet_size,
    dap_enc_key_t *a_key,
    uint8_t a_expected_type)
{
    if (!a_packet || !a_key || a_packet_size == 0) {
        return false;
    }
    
    // Try to decrypt and parse
    uint8_t l_type;
    uint32_t l_seq_num;
    uint64_t l_session_id;
    uint8_t *l_payload = NULL;
    size_t l_payload_size;
    
    int l_ret = dap_udp_test_decrypt_and_parse_packet(
        a_packet, a_packet_size, a_key,
        &l_type, &l_seq_num, &l_session_id,
        &l_payload, &l_payload_size);
    
    if (l_ret != 0) {
        return false;
    }
    
    // Validate packet type if specified
    bool l_valid = true;
    if (a_expected_type != 0xFF && l_type != a_expected_type) {
        log_it(L_WARNING, "Packet type mismatch: got %u, expected %u",
               l_type, a_expected_type);
        l_valid = false;
    }
    
    // Cleanup
    if (l_payload) {
        DAP_DELETE(l_payload);
    }
    
    return l_valid;
}

int dap_udp_test_create_encrypted_packet(
    uint8_t a_type,
    uint32_t a_seq_num,
    uint64_t a_session_id,
    const uint8_t *a_payload,
    size_t a_payload_size,
    dap_enc_key_t *a_key,
    uint8_t *a_out_packet,
    size_t *a_out_packet_size)
{
    if (!a_key || !a_out_packet || !a_out_packet_size) {
        return -1;
    }
    
    // Calculate plaintext size
    size_t l_plaintext_size = sizeof(dap_stream_trans_udp_encrypted_header_t) + a_payload_size;
    uint8_t *l_plaintext = DAP_NEW_Z_SIZE(uint8_t, l_plaintext_size);
    if (!l_plaintext) {
        log_it(L_ERROR, "Failed to allocate plaintext buffer");
        return -1;
    }
    
    // Build internal header
    dap_stream_trans_udp_encrypted_header_t *l_hdr = 
        (dap_stream_trans_udp_encrypted_header_t*)l_plaintext;
    l_hdr->type = a_type;
    l_hdr->seq_num = htobe32(a_seq_num);
    l_hdr->session_id = htobe64(a_session_id);
    
    // Copy payload
    if (a_payload && a_payload_size > 0) {
        memcpy(l_plaintext + sizeof(dap_stream_trans_udp_encrypted_header_t), 
               a_payload, a_payload_size);
    }
    
    // Encrypt
    size_t l_encrypt_buf_size = l_plaintext_size + 256;
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypt_buf_size);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_plaintext);
        return -1;
    }
    
    size_t l_encrypted_size = dap_enc_code(a_key, l_plaintext, l_plaintext_size,
                                        l_encrypted, l_encrypt_buf_size,
                                        DAP_ENC_DATA_TYPE_RAW);
    DAP_DELETE(l_plaintext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt packet");
        DAP_DELETE(l_encrypted);
        return -1;
    }
    
    // Copy to output
    if (l_encrypted_size > DAP_UDP_TEST_MAX_PACKET_SIZE) {
        log_it(L_ERROR, "Encrypted packet too large: %zu > %u",
               l_encrypted_size, DAP_UDP_TEST_MAX_PACKET_SIZE);
        DAP_DELETE(l_encrypted);
        return -1;
    }
    
    memcpy(a_out_packet, l_encrypted, l_encrypted_size);
    *a_out_packet_size = l_encrypted_size;
    
    DAP_DELETE(l_encrypted);
    
    log_it(L_DEBUG, "Created encrypted packet: type=%u, seq=%u, session=0x%" PRIx64 ", size=%zu",
           a_type, a_seq_num, a_session_id, *a_out_packet_size);
    
    return 0;
}

// ============================================================================
// HANDSHAKE TEST HELPERS
// ============================================================================

/**
 * @brief Simulate full Kyber512 handshake using new KEM API
 */
int dap_udp_test_simulate_kyber_handshake(
    dap_enc_key_t **a_out_handshake_key,
    uint8_t **a_out_alice_pubkey,
    size_t *a_out_alice_pubkey_size,
    uint8_t **a_out_bob_ciphertext,
    size_t *a_out_bob_ciphertext_size)
{
    if (!a_out_handshake_key || !a_out_alice_pubkey || !a_out_alice_pubkey_size ||
        !a_out_bob_ciphertext || !a_out_bob_ciphertext_size) {
        return -1;
    }
    
    // STEP 1: Alice generates keypair
    dap_enc_kem_result_t *l_alice_kem = dap_enc_kem_alice_generate_keypair(DAP_ENC_KEY_TYPE_KEM_KYBER512);
    if (!l_alice_kem) {
        log_it(L_ERROR, "Failed to generate Alice's Kyber512 keypair");
        return -1;
    }
    
    // Export Alice's public key
    *a_out_alice_pubkey_size = l_alice_kem->public_data_size;
    *a_out_alice_pubkey = DAP_NEW_SIZE(uint8_t, *a_out_alice_pubkey_size);
    if (!*a_out_alice_pubkey) {
        log_it(L_ERROR, "Failed to allocate Alice's public key buffer");
        dap_enc_kem_result_free(l_alice_kem);
        return -1;
    }
    memcpy(*a_out_alice_pubkey, l_alice_kem->public_data, *a_out_alice_pubkey_size);
    
    // STEP 2: Bob encapsulates shared secret
    dap_enc_kem_result_t *l_bob_kem = dap_enc_kem_bob_encapsulate(
        DAP_ENC_KEY_TYPE_KEM_KYBER512,
        *a_out_alice_pubkey,
        *a_out_alice_pubkey_size
    );
    
    if (!l_bob_kem) {
        log_it(L_ERROR, "Failed to encapsulate shared secret");
        DAP_DELETE(*a_out_alice_pubkey);
        *a_out_alice_pubkey = NULL;
        dap_enc_kem_result_free(l_alice_kem);
        return -1;
    }
    
    // Export Bob's ciphertext
    *a_out_bob_ciphertext_size = l_bob_kem->public_data_size;
    *a_out_bob_ciphertext = DAP_NEW_SIZE(uint8_t, *a_out_bob_ciphertext_size);
    if (!*a_out_bob_ciphertext) {
        log_it(L_ERROR, "Failed to allocate Bob's ciphertext buffer");
        DAP_DELETE(*a_out_alice_pubkey);
        *a_out_alice_pubkey = NULL;
        dap_enc_kem_result_free(l_bob_kem);
        dap_enc_kem_result_free(l_alice_kem);
        return -1;
    }
    memcpy(*a_out_bob_ciphertext, l_bob_kem->public_data, *a_out_bob_ciphertext_size);
    
    // STEP 3: Alice decapsulates to get shared secret
    int l_ret = dap_enc_kem_alice_decapsulate(
        l_alice_kem,
        *a_out_bob_ciphertext,
        *a_out_bob_ciphertext_size
    );
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to decapsulate shared secret");
        DAP_DELETE(*a_out_alice_pubkey);
        DAP_DELETE(*a_out_bob_ciphertext);
        *a_out_alice_pubkey = NULL;
        *a_out_bob_ciphertext = NULL;
        dap_enc_kem_result_free(l_bob_kem);
        dap_enc_kem_result_free(l_alice_kem);
        return -1;
    }
    
    // STEP 4: Derive handshake key from shared secret
    *a_out_handshake_key = dap_enc_kem_derive_key(
        l_alice_kem->shared_secret,
        l_alice_kem->shared_secret_size,
        "udp_handshake",
        0,  // counter = 0 for handshake
        DAP_ENC_KEY_TYPE_SALSA2012,
        32  // SALSA2012 key size
    );
    
    if (!*a_out_handshake_key) {
        log_it(L_ERROR, "Failed to derive handshake key");
        DAP_DELETE(*a_out_alice_pubkey);
        DAP_DELETE(*a_out_bob_ciphertext);
        *a_out_alice_pubkey = NULL;
        *a_out_bob_ciphertext = NULL;
        dap_enc_kem_result_free(l_bob_kem);
        dap_enc_kem_result_free(l_alice_kem);
        return -1;
    }
    
    // Cleanup
    dap_enc_kem_result_free(l_bob_kem);
    dap_enc_kem_result_free(l_alice_kem);
    
    log_it(L_DEBUG, "Simulated Kyber512 handshake: alice_pubkey=%zu bytes, bob_ciphertext=%zu bytes",
           *a_out_alice_pubkey_size, *a_out_bob_ciphertext_size);
    
    return 0;
}

// ============================================================================
// SESSION TEST HELPERS
// ============================================================================

/**
 * @brief Derive session key using new KEM API
 */
dap_enc_key_t* dap_udp_test_derive_session_key(
    dap_enc_key_t *a_handshake_key,
    uint64_t a_counter,
    const char *a_context)
{
    if (!a_handshake_key || !a_context) {
        return NULL;
    }
    
    // Get handshake key data
    if (!a_handshake_key->priv_key_data || a_handshake_key->priv_key_data_size == 0) {
        log_it(L_ERROR, "Handshake key has no private key data");
        return NULL;
    }
    
    // Derive session key using new API
    dap_enc_key_t *l_session_key = dap_enc_kem_derive_key(
        a_handshake_key->priv_key_data,
        a_handshake_key->priv_key_data_size,
        a_context,
        a_counter,
        DAP_ENC_KEY_TYPE_SALSA2012,
        32  // SALSA2012 key size
    );
    
    if (!l_session_key) {
        log_it(L_ERROR, "Failed to derive session key");
        return NULL;
    }
    
    log_it(L_DEBUG, "Derived session key: context=%s, counter=%" PRIu64, a_context, a_counter);
    
    return l_session_key;
}

// ============================================================================
// OBFUSCATION TEST HELPERS
// ============================================================================

int dap_udp_test_obfuscation_roundtrip(
    const uint8_t *a_handshake,
    size_t a_handshake_size,
    uint8_t **a_out_obfuscated,
    size_t *a_out_obfuscated_size,
    uint8_t **a_out_deobfuscated,
    size_t *a_out_deobfuscated_size)
{
    if (!a_handshake || !a_out_obfuscated || !a_out_obfuscated_size ||
        !a_out_deobfuscated || !a_out_deobfuscated_size) {
        return -1;
    }
    
    // Obfuscate
    int l_ret = dap_transport_obfuscate_handshake(
        a_handshake, a_handshake_size, 
        a_out_obfuscated, a_out_obfuscated_size);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to obfuscate handshake: %d", l_ret);
        return -1;
    }
    
    // Deobfuscate
    l_ret = dap_transport_deobfuscate_handshake(
        *a_out_obfuscated, *a_out_obfuscated_size,
        a_out_deobfuscated, a_out_deobfuscated_size);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to deobfuscate handshake: %d", l_ret);
        DAP_DELETE(*a_out_obfuscated);
        *a_out_obfuscated = NULL;
        return -1;
    }
    
    // Verify roundtrip
    if (*a_out_deobfuscated_size != a_handshake_size ||
        memcmp(*a_out_deobfuscated, a_handshake, a_handshake_size) != 0) {
        log_it(L_ERROR, "Obfuscation roundtrip failed: size mismatch or data corruption");
        DAP_DELETE(*a_out_obfuscated);
        DAP_DELETE(*a_out_deobfuscated);
        *a_out_obfuscated = NULL;
        *a_out_deobfuscated = NULL;
        return -1;
    }
    
    log_it(L_DEBUG, "Obfuscation roundtrip success: %zu -> %zu -> %zu bytes",
           a_handshake_size, *a_out_obfuscated_size, *a_out_deobfuscated_size);
    
    return 0;
}

// ============================================================================
// DAP_MOCK_CUSTOM FOR dap_events_socket_write_unsafe
// ============================================================================

/**
 * @brief Custom mock wrapper for dap_events_socket_write_unsafe
 * 
 * This mock captures UDP packets written to the socket for validation in tests.
 * Uses proper DAP_MOCK_WRAPPER_CUSTOM pattern as per DAP SDK mock framework standards.
 */
DAP_MOCK_WRAPPER_CUSTOM(ssize_t, dap_events_socket_write_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(const void*, a_data),
    PARAM(size_t, a_size)
)
{
    // Validate input
    if (!a_data || a_size == 0 || a_size > DAP_UDP_TEST_MAX_PACKET_SIZE) {
        log_it(L_ERROR, "Mock write_unsafe: Invalid params: data=%p, size=%zu", a_data, a_size);
        return -1;
    }
    
    // Capture packet data
    memcpy(s_captured_packet.data, a_data, a_size);
    s_captured_packet.size = a_size;
    s_captured_packet.is_valid = true;
    
    // Copy destination address if esocket is provided
    if (a_es && a_es->addr_size > 0) {
        memcpy(&s_captured_packet.dest_addr, &a_es->addr_storage, a_es->addr_size);
        s_captured_packet.dest_addr_len = a_es->addr_size;
    }
    
    log_it(L_DEBUG, "Mock write_unsafe: Captured UDP packet: %zu bytes", a_size);
    
    // Simulate successful write - return number of bytes written
    return (ssize_t)a_size;
}

/**
 * @brief Custom mock wrapper for dap_events_socket_sendto_unsafe
 * 
 * This mock captures UDP packets with explicit destination address.
 * UDP transport uses sendto_unsafe instead of write_unsafe for datagram sockets.
 */
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_events_socket_sendto_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(const void*, a_data),
    PARAM(size_t, a_size),
    PARAM(const struct sockaddr_storage*, a_addr),
    PARAM(socklen_t, a_addr_len)
)
{
    // Validate input
    if (!a_data || a_size == 0 || a_size > DAP_UDP_TEST_MAX_PACKET_SIZE || !a_addr) {
        log_it(L_ERROR, "Mock sendto_unsafe: Invalid params: data=%p, size=%zu, addr=%p", a_data, a_size, a_addr);
        return 0;
    }
    
    // Capture packet data
    memcpy(s_captured_packet.data, a_data, a_size);
    s_captured_packet.size = a_size;
    s_captured_packet.is_valid = true;
    
    // Copy destination address from explicit parameter
    memcpy(&s_captured_packet.dest_addr, a_addr, a_addr_len);
    s_captured_packet.dest_addr_len = a_addr_len;
    
    log_it(L_DEBUG, "Mock sendto_unsafe: Captured UDP packet: %zu bytes to explicit address", a_size);
    
    // Simulate successful write - return number of bytes written
    return a_size;
}
