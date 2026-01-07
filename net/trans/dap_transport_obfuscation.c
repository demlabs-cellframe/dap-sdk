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

// SALSA2012 direct access for deterministic encryption
#include "salsa2012/crypto_stream_salsa2012.h"

#define LOG_TAG "dap_transport_obfuscation"
#define SALSA20_NONCE_SIZE 8

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
 * @param a_nonce_out Output buffer for deterministic nonce (8 bytes), or NULL if not needed
 * @return dap_enc_key_t ready to use, or NULL on error
 */
static dap_enc_key_t* s_get_cipher_key_for_size(size_t a_packet_size, dap_enc_key_t *a_existing_key, uint8_t *a_nonce_out)
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
    
    // Generate deterministic nonce (CRITICAL for reproducibility!)
    if (a_nonce_out) {
        uint8_t l_nonce_full[16];  // Get more than needed, then truncate
        l_ret = dap_enc_kdf_derive(
            DAP_TRANSPORT_OBFUSCATION_SEED,
            strlen(DAP_TRANSPORT_OBFUSCATION_SEED),
            "nonce",
            strlen("nonce"),
            l_size_counter,  // Same counter!
            l_nonce_full,
            sizeof(l_nonce_full)
        );
        
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to derive nonce for size %zu", a_packet_size);
            return NULL;
        }
        
        // Copy first 8 bytes as SALSA2012 nonce
        memcpy(a_nonce_out, l_nonce_full, 8);
        
        // DEBUG: Print nonce
        log_it(L_DEBUG, "Nonce for size %zu: %02x%02x%02x%02x %02x%02x%02x%02x",
               a_packet_size,
               a_nonce_out[0], a_nonce_out[1], a_nonce_out[2], a_nonce_out[3],
               a_nonce_out[4], a_nonce_out[5], a_nonce_out[6], a_nonce_out[7]);
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
    
    // Create cipher key + deterministic nonce
    uint8_t l_nonce[SALSA20_NONCE_SIZE];
    log_it(L_DEBUG, "OBFUSCATE: final_size=%zu, handshake=%zu, padding=%zu", 
           l_final_size, a_handshake_size, l_padding_size);
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(l_final_size, NULL, l_nonce);
    if (!l_key) {
        DAP_DELETE(l_cleartext);
        return -4;
    }
    
    // DEBUG: Print first 32 bytes of cleartext BEFORE encryption
    log_it(L_DEBUG, "Cleartext BEFORE encrypt (first 32 bytes): "
           "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
           "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
           l_cleartext[0], l_cleartext[1], l_cleartext[2], l_cleartext[3],
           l_cleartext[4], l_cleartext[5], l_cleartext[6], l_cleartext[7],
           l_cleartext[8], l_cleartext[9], l_cleartext[10], l_cleartext[11],
           l_cleartext[12], l_cleartext[13], l_cleartext[14], l_cleartext[15],
           l_cleartext[16], l_cleartext[17], l_cleartext[18], l_cleartext[19],
           l_cleartext[20], l_cleartext[21], l_cleartext[22], l_cleartext[23],
           l_cleartext[24], l_cleartext[25], l_cleartext[26], l_cleartext[27],
           l_cleartext[28], l_cleartext[29], l_cleartext[30], l_cleartext[31]);
    
    // Encrypt entire packet with deterministic nonce
    // Format: [nonce(8)] + [encrypted_cleartext]
    size_t l_encrypted_size = SALSA20_NONCE_SIZE + l_final_size;
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_size);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_cleartext);
        return -5;
    }
    
    // Write deterministic nonce
    memcpy(l_encrypted, l_nonce, SALSA20_NONCE_SIZE);
    
    // Encrypt with SALSA2012 XOR (deterministic!)
    int l_xor_ret = crypto_stream_salsa2012_xor(
        l_encrypted + SALSA20_NONCE_SIZE,  // Output after nonce
        l_cleartext,                        // Input cleartext
        l_final_size,                       // Input size
        l_nonce,                            // Deterministic nonce
        l_key->priv_key_data                // Key
    );
    
    dap_enc_key_delete(l_key);
    DAP_DELETE(l_cleartext);
    
    if (l_xor_ret != 0) {
        log_it(L_ERROR, "Failed to encrypt obfuscated handshake (XOR failed)");
        DAP_DELETE(l_encrypted);
        return -6;
    }
    
    // DEBUG: Print first 32 bytes of ciphertext AFTER encryption
    if (l_encrypted_size >= 32) {
        log_it(L_DEBUG, "Ciphertext AFTER encrypt (first 32 bytes): "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
               l_encrypted[0], l_encrypted[1], l_encrypted[2], l_encrypted[3],
               l_encrypted[4], l_encrypted[5], l_encrypted[6], l_encrypted[7],
               l_encrypted[8], l_encrypted[9], l_encrypted[10], l_encrypted[11],
               l_encrypted[12], l_encrypted[13], l_encrypted[14], l_encrypted[15],
               l_encrypted[16], l_encrypted[17], l_encrypted[18], l_encrypted[19],
               l_encrypted[20], l_encrypted[21], l_encrypted[22], l_encrypted[23],
               l_encrypted[24], l_encrypted[25], l_encrypted[26], l_encrypted[27],
               l_encrypted[28], l_encrypted[29], l_encrypted[30], l_encrypted[31]);
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
    
    // Ensure packet has nonce
    if (a_obfuscated_size < SALSA20_NONCE_SIZE) {
        return -3;
    }
    
    // Extract nonce from packet (first 8 bytes)
    uint8_t l_nonce[SALSA20_NONCE_SIZE];
    memcpy(l_nonce, a_obfuscated_data, SALSA20_NONCE_SIZE);
    
    // CRITICAL: KDF is based on CLEARTEXT size, NOT encrypted size!
    // encrypted_size = cleartext_size + SALSA20_NONCE_SIZE
    // So: cleartext_size = encrypted_size - SALSA20_NONCE_SIZE
    size_t l_cleartext_size = a_obfuscated_size - SALSA20_NONCE_SIZE;
    
    // Verify nonce matches expected (deterministic from CLEARTEXT size!)
    uint8_t l_expected_nonce[SALSA20_NONCE_SIZE];
    log_it(L_DEBUG, "DEOBFUSCATE: encrypted_size=%zu, cleartext_size=%zu", 
           a_obfuscated_size, l_cleartext_size);
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(l_cleartext_size, NULL, l_expected_nonce);
    if (!l_key) {
        return -4;
    }
    
    // DEBUG: Compare nonces
    if (memcmp(l_nonce, l_expected_nonce, SALSA20_NONCE_SIZE) != 0) {
        log_it(L_WARNING, "Nonce mismatch: received %02x%02x%02x%02x %02x%02x%02x%02x, "
               "expected %02x%02x%02x%02x %02x%02x%02x%02x",
               l_nonce[0], l_nonce[1], l_nonce[2], l_nonce[3],
               l_nonce[4], l_nonce[5], l_nonce[6], l_nonce[7],
               l_expected_nonce[0], l_expected_nonce[1], l_expected_nonce[2], l_expected_nonce[3],
               l_expected_nonce[4], l_expected_nonce[5], l_expected_nonce[6], l_expected_nonce[7]);
        // Continue anyway - might be old packet
    }
    
    // DEBUG: Print first 32 bytes of ciphertext BEFORE decryption
    if (a_obfuscated_size >= 32) {
        log_it(L_DEBUG, "Ciphertext BEFORE decrypt (first 32 bytes): "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
               a_obfuscated_data[0], a_obfuscated_data[1], a_obfuscated_data[2], a_obfuscated_data[3],
               a_obfuscated_data[4], a_obfuscated_data[5], a_obfuscated_data[6], a_obfuscated_data[7],
               a_obfuscated_data[8], a_obfuscated_data[9], a_obfuscated_data[10], a_obfuscated_data[11],
               a_obfuscated_data[12], a_obfuscated_data[13], a_obfuscated_data[14], a_obfuscated_data[15],
               a_obfuscated_data[16], a_obfuscated_data[17], a_obfuscated_data[18], a_obfuscated_data[19],
               a_obfuscated_data[20], a_obfuscated_data[21], a_obfuscated_data[22], a_obfuscated_data[23],
               a_obfuscated_data[24], a_obfuscated_data[25], a_obfuscated_data[26], a_obfuscated_data[27],
               a_obfuscated_data[28], a_obfuscated_data[29], a_obfuscated_data[30], a_obfuscated_data[31]);
    }
    
    // Decrypt with SALSA2012 XOR
    size_t l_ciphertext_size = a_obfuscated_size - SALSA20_NONCE_SIZE;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_ciphertext_size);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        dap_enc_key_delete(l_key);
        return -5;
    }
    
    int l_xor_ret = crypto_stream_salsa2012_xor(
        l_decrypted,                                    // Output cleartext
        a_obfuscated_data + SALSA20_NONCE_SIZE,       // Input ciphertext (after nonce)
        l_ciphertext_size,                             // Size
        l_nonce,                                        // Nonce from packet
        l_key->priv_key_data                           // Key
    );
    
    dap_enc_key_delete(l_key);
    
    if (l_xor_ret != 0) {
        log_it(L_ERROR, "Failed to decrypt obfuscated handshake (XOR failed)");
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    size_t l_decrypted_size = l_ciphertext_size;
    
    // DEBUG: Print first 32 bytes of decrypted data AFTER decryption
    if (l_decrypted_size >= 32) {
        log_it(L_DEBUG, "Decrypted AFTER decrypt (first 32 bytes): "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
               "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
               l_decrypted[0], l_decrypted[1], l_decrypted[2], l_decrypted[3],
               l_decrypted[4], l_decrypted[5], l_decrypted[6], l_decrypted[7],
               l_decrypted[8], l_decrypted[9], l_decrypted[10], l_decrypted[11],
               l_decrypted[12], l_decrypted[13], l_decrypted[14], l_decrypted[15],
               l_decrypted[16], l_decrypted[17], l_decrypted[18], l_decrypted[19],
               l_decrypted[20], l_decrypted[21], l_decrypted[22], l_decrypted[23],
               l_decrypted[24], l_decrypted[25], l_decrypted[26], l_decrypted[27],
               l_decrypted[28], l_decrypted[29], l_decrypted[30], l_decrypted[31]);
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
    
    // DEBUG: Show what we parsed
    log_it(L_DEBUG, "DEOBFUSCATE: parsed l_hs_size=%zu from bytes %02x%02x (net order=%04x)",
           l_hs_size, l_decrypted[0], l_decrypted[1], l_hs_size_net);
    
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

