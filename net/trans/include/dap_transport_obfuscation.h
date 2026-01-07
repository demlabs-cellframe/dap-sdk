/**
 * @file dap_transport_obfuscation.h
 * @brief Transport-agnostic packet obfuscation for DPI resistance
 * 
 * This module provides TRANSPORT-LEVEL MASKING for handshake packets.
 * 
 * IMPORTANT: Obfuscation is NOT part of cryptographic chain!
 * - Purpose: Hide packet structure from DPI (Deep Packet Inspection)
 * - Method: Lightweight XOR/cipher with size-derived ephemeral keys
 * - After deobfuscation: Discard obfuscation key, use inner crypto only!
 * 
 * Cryptographic security comes from inner protocol (Kyber, etc).
 * Obfuscation just makes packets look random to network observers.
 * 
 * Purpose: Combat DPI and protocol fingerprinting to protect privacy rights.
 * 
 * @copyright (c) 2025 DeM Labs Inc.
 * @author DeM Labs Inc.   https://demlabs.net
 * @license SPDX: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Minimum obfuscated handshake packet size (bytes)
 * 
 * Must be large enough to contain actual handshake data plus minimal padding.
 * Recommended: 600 bytes minimum for good obfuscation.
 */
#define DAP_TRANSPORT_OBFUSCATION_MIN_SIZE 600

/**
 * @brief Maximum obfuscated handshake packet size (bytes)
 * 
 * Should stay within reasonable UDP MTU limits.
 * Recommended: 900 bytes maximum.
 */
#define DAP_TRANSPORT_OBFUSCATION_MAX_SIZE 900

/**
 * @brief Static seed for size-based key derivation
 * 
 * This constant is used to derive encryption keys from packet sizes.
 * Must be same on client and server for compatibility.
 * 
 * Changing this breaks compatibility with older versions!
 */
#define DAP_TRANSPORT_OBFUSCATION_SEED "cellframe-transport-obfuscation-v1-2025"

/**
 * @brief Obfuscate handshake packet
 * 
 * Encrypts handshake data with padding to variable size (600-900 bytes).
 * Encryption key is derived from final packet size using KDF.
 * 
 * Algorithm:
 * 1. Add random padding (0-300 bytes) to handshake data
 * 2. Derive encryption key: KDF-SHAKE256(SEED, packet_size)
 * 3. Encrypt entire packet with this key
 * 4. Result: encrypted blob of random size 600-900 bytes
 * 
 * @param a_handshake_data Raw handshake data (e.g. Kyber public key)
 * @param a_handshake_size Size of handshake data
 * @param[out] a_obfuscated_data Allocated buffer for obfuscated packet
 * @param[out] a_obfuscated_size Size of obfuscated packet
 * @return 0 on success, negative on error
 * 
 * @note Caller must free a_obfuscated_data with DAP_DELETE()
 * 
 * Example:
 * ```c
 * uint8_t *l_obf = NULL;
 * size_t l_obf_size = 0;
 * int ret = dap_transport_obfuscate_handshake(kyber_key, 800, &l_obf, &l_obf_size);
 * // l_obf now contains 600-900 bytes of encrypted data
 * send(socket, l_obf, l_obf_size);
 * DAP_DELETE(l_obf);
 * ```
 */
int dap_transport_obfuscate_handshake(const uint8_t *a_handshake_data,
                                       size_t a_handshake_size,
                                       uint8_t **a_obfuscated_data,
                                       size_t *a_obfuscated_size);

/**
 * @brief Deobfuscate handshake packet
 * 
 * Attempts to decrypt obfuscated handshake packet using size-derived key.
 * Returns original handshake data if decryption succeeds.
 * 
 * Algorithm:
 * 1. Derive decryption key: KDF-SHAKE256(SEED, packet_size)
 * 2. Decrypt packet with this key
 * 3. Validate structure (check magic/padding)
 * 4. Extract original handshake data
 * 
 * @param a_obfuscated_data Obfuscated packet data
 * @param a_obfuscated_size Size of obfuscated packet
 * @param[out] a_handshake_data Allocated buffer for original handshake
 * @param[out] a_handshake_size Size of original handshake data
 * @return 0 on success, negative on error
 * 
 * @note Caller must free a_handshake_data with DAP_DELETE()
 * 
 * Example:
 * ```c
 * uint8_t *l_handshake = NULL;
 * size_t l_handshake_size = 0;
 * int ret = dap_transport_deobfuscate_handshake(packet, packet_size, 
 *                                                &l_handshake, &l_handshake_size);
 * if (ret == 0) {
 *     // l_handshake contains original Kyber key
 *     process_handshake(l_handshake, l_handshake_size);
 *     DAP_DELETE(l_handshake);
 * }
 * ```
 */
int dap_transport_deobfuscate_handshake(const uint8_t *a_obfuscated_data,
                                         size_t a_obfuscated_size,
                                         uint8_t **a_handshake_data,
                                         size_t *a_handshake_size);

/**
 * @brief Check if packet could be obfuscated handshake
 * 
 * Quick check based on size without decryption.
 * Use this for early filtering before attempting deobfuscation.
 * 
 * @param a_packet_size Size of received packet
 * @return true if size is in valid range for obfuscated handshake
 */
static inline bool dap_transport_is_obfuscated_handshake_size(size_t a_packet_size)
{
    return (a_packet_size >= DAP_TRANSPORT_OBFUSCATION_MIN_SIZE &&
            a_packet_size <= DAP_TRANSPORT_OBFUSCATION_MAX_SIZE);
}

#ifdef __cplusplus
}
#endif

