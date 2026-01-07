/**
 * @file dap_transport_obfuscation.c
 * @brief Transport-agnostic packet obfuscation implementation
 * 
 * TRANSPORT-LEVEL MASKING ONLY!
 * - Obfuscation keys are ephemeral, derived from packet size
 * - Used ONLY to hide packet structure from DPI
 * - NOT part of cryptographic chain!
 * - After deobfuscation, discard key and use inner crypto (Kyber, etc)
 * 
 * Think of it as "gift wrapping" - hides the box, not what's inside!
 * Real security comes from Kyber shared secret → KDF → session keys.
 * 
 * @copyright (c) 2025 DeM Labs Inc.
 */

#include <string.h>
#include <time.h>
#include <arpa/inet.h>  // For htons/ntohs
#include "dap_common.h"
#include "dap_transport_obfuscation.h"
#include "dap_enc_kdf.h"
#include "dap_enc_key.h"  // For dap_enc_key_new_from_raw_bytes
#include "dap_rand.h"  // For randombytes
#include "dap_enc.h"

#define LOG_TAG "dap_transport_obfuscation"

// Internal structure for obfuscated packet
// Format: [handshake_size(2)] + [handshake_data] + [padding]
// All encrypted with size-derived key

/**
 * @brief Create or update cipher key with KDF from packet size
 * 
 * OPTIMIZED: Uses dap_enc_key_new_from_raw_bytes (NO Keccak hashing!)
 * 
 * @param a_packet_size Packet size (used as KDF counter)
 * @param a_existing_key Existing key to update, or NULL to create new
 * @return dap_enc_key_t ready to use, or NULL on error
 */
static dap_enc_key_t* s_get_cipher_key_for_size(size_t a_packet_size, dap_enc_key_t *a_existing_key)
{
    // Generate KDF from packet size
    uint8_t l_kdf_key[32];  // 256-bit key for SALSA2012
    uint64_t l_size_counter = (uint64_t)a_packet_size;
    
    int l_ret = dap_enc_kdf_derive(
        DAP_TRANSPORT_OBFUSCATION_SEED,
        strlen(DAP_TRANSPORT_OBFUSCATION_SEED),
        "obfuscation",
        strlen("obfuscation"),
        l_size_counter,
        l_kdf_key,
        sizeof(l_kdf_key)
    );
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to derive KDF for size %zu", a_packet_size);
        return NULL;
    }
    
    // DEBUG: Print KDF
    log_it(L_DEBUG, "KDF for size %zu: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
           a_packet_size,
           l_kdf_key[0], l_kdf_key[1], l_kdf_key[2], l_kdf_key[3],
           l_kdf_key[4], l_kdf_key[5], l_kdf_key[6], l_kdf_key[7],
           l_kdf_key[8], l_kdf_key[9], l_kdf_key[10], l_kdf_key[11],
           l_kdf_key[12], l_kdf_key[13], l_kdf_key[14], l_kdf_key[15]);
    
    dap_enc_key_t *l_key;
    
    if (a_existing_key) {
        // Update existing key (FAST path - no alloc!)
        if (dap_enc_key_update_from_raw_bytes(a_existing_key, l_kdf_key, sizeof(l_kdf_key)) == 0) {
            l_key = a_existing_key;
        } else {
            log_it(L_ERROR, "Failed to update key from raw bytes");
            l_key = NULL;
        }
    } else {
        // Create new key from raw KDF bytes (NO Keccak!)
        l_key = dap_enc_key_new_from_raw_bytes(DAP_ENC_KEY_TYPE_SALSA2012, 
                                                l_kdf_key, sizeof(l_kdf_key));
        if (!l_key) {
            log_it(L_ERROR, "Failed to create key from raw bytes");
        }
    }
    
    // Zero out KDF buffer
    memset(l_kdf_key, 0, sizeof(l_kdf_key));
    
    return l_key;
}

int dap_transport_obfuscate_handshake(const uint8_t *a_handshake_data,
                                       size_t a_handshake_size,
                                       uint8_t **a_obfuscated_data,
                                       size_t *a_obfuscated_size)
{
    if (!a_handshake_data || a_handshake_size == 0 || 
        !a_obfuscated_data || !a_obfuscated_size) {
        return -1;
    }
    
    // Choose random final size in range [MIN, MAX]
    uint32_t l_size_range = DAP_TRANSPORT_OBFUSCATION_MAX_SIZE - 
                            DAP_TRANSPORT_OBFUSCATION_MIN_SIZE;
    uint32_t l_random_offset;
    randombytes((uint8_t*)&l_random_offset, sizeof(l_random_offset));
    l_random_offset = l_random_offset % (l_size_range + 1);
    
    size_t l_final_size = DAP_TRANSPORT_OBFUSCATION_MIN_SIZE + l_random_offset;
    
    // Ensure we have enough space for handshake + size header
    size_t l_required_size = sizeof(uint16_t) + a_handshake_size;
    if (l_final_size < l_required_size) {
        log_it(L_ERROR, "Obfuscation: final size %zu too small for handshake %zu",
               l_final_size, a_handshake_size);
        return -2;
    }
    
    // Build cleartext packet: [size(2)] + [handshake] + [padding]
    size_t l_padding_size = l_final_size - l_required_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_final_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -3;
    }
    
    // Write handshake size (network byte order)
    uint16_t l_hs_size_net = htons((uint16_t)a_handshake_size);
    memcpy(l_cleartext, &l_hs_size_net, sizeof(l_hs_size_net));
    
    // Write handshake data
    memcpy(l_cleartext + sizeof(uint16_t), a_handshake_data, a_handshake_size);
    
    // Write random padding
    if (l_padding_size > 0) {
        randombytes(l_cleartext + l_required_size, l_padding_size);
    }
    
    // Create cipher key from KDF (NO Keccak hashing!)
    log_it(L_DEBUG, "OBFUSCATE: final_size=%zu, handshake=%zu, padding=%zu", 
           l_final_size, a_handshake_size, l_padding_size);
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(l_final_size, NULL);
    if (!l_key) {
        DAP_DELETE(l_cleartext);
        return -4;
    }
    
    // Encrypt entire packet
    size_t l_encrypted_max = l_final_size + 256;  // Extra for encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_cleartext);
        return -5;
    }
    
    size_t l_encrypted_size = dap_enc_code(l_key,
                                           l_cleartext, l_final_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    dap_enc_key_delete(l_key);
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt obfuscated handshake");
        DAP_DELETE(l_encrypted);
        return -6;
    }
    
    // Return encrypted packet
    *a_obfuscated_data = l_encrypted;
    *a_obfuscated_size = l_encrypted_size;
    
    log_it(L_DEBUG, "Obfuscated handshake: %zu bytes → %zu bytes (padding=%zu), final_plaintext=%zu",
           a_handshake_size, l_encrypted_size, l_padding_size, l_final_size);
    
    return 0;
}

int dap_transport_deobfuscate_handshake(const uint8_t *a_obfuscated_data,
                                         size_t a_obfuscated_size,
                                         uint8_t **a_handshake_data,
                                         size_t *a_handshake_size)
{
    if (!a_obfuscated_data || a_obfuscated_size == 0 ||
        !a_handshake_data || !a_handshake_size) {
        return -1;
    }
    
    // Quick size check
    if (!dap_transport_is_obfuscated_handshake_size(a_obfuscated_size)) {
        return -2;  // Size out of range, not obfuscated handshake
    }
    
    // Create cipher key from KDF for decryption
    log_it(L_DEBUG, "DEOBFUSCATE: packet_size=%zu", a_obfuscated_size);
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(a_obfuscated_size, NULL);
    if (!l_key) {
        return -3;
    }
    
    // Decrypt packet
    size_t l_decrypted_max = a_obfuscated_size + 256;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        dap_enc_key_delete(l_key);
        return -4;
    }
    
    size_t l_decrypted_size = dap_enc_decode(l_key,
                                             a_obfuscated_data, a_obfuscated_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    
    dap_enc_key_delete(l_key);
    
    if (l_decrypted_size == 0) {
        // Decryption failed - not an obfuscated handshake or wrong key
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Parse cleartext: [size(2)] + [handshake] + [padding]
    if (l_decrypted_size < sizeof(uint16_t)) {
        log_it(L_WARNING, "Decrypted packet too small: %zu bytes", l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    // Read handshake size
    uint16_t l_hs_size_net;
    memcpy(&l_hs_size_net, l_decrypted, sizeof(l_hs_size_net));
    size_t l_hs_size = ntohs(l_hs_size_net);
    
    // Validate handshake size
    if (l_hs_size == 0 || l_hs_size > (l_decrypted_size - sizeof(uint16_t))) {
        log_it(L_WARNING, "Invalid handshake size: %zu (packet=%zu)", 
               l_hs_size, l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -7;
    }
    
    // Extract handshake data
    uint8_t *l_handshake = DAP_NEW_SIZE(uint8_t, l_hs_size);
    if (!l_handshake) {
        log_it(L_ERROR, "Failed to allocate handshake buffer");
        DAP_DELETE(l_decrypted);
        return -8;
    }
    
    memcpy(l_handshake, l_decrypted + sizeof(uint16_t), l_hs_size);
    DAP_DELETE(l_decrypted);
    
    // Return original handshake
    *a_handshake_data = l_handshake;
    *a_handshake_size = l_hs_size;
    
    log_it(L_DEBUG, "Deobfuscated handshake: %zu bytes → %zu bytes",
           a_obfuscated_size, l_hs_size);
    
    return 0;
}

