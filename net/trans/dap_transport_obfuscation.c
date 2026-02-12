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
#include "dap_common.h"
#include "dap_transport_obfuscation.h"
#include "dap_enc_kdf.h"
#include "dap_enc_key.h"
#include "dap_rand.h"
#include "dap_enc.h"
#include "dap_serialize.h"

#define LOG_TAG "dap_transport_obfuscation"
#define SALSA20_NONCE_SIZE 8

// Obfuscated packet cleartext structure (serialized with dap_serialize)
typedef struct {
    uint16_t cleartext_total_size;  // Total cleartext size (for KDF)
    uint16_t handshake_size;        // Actual handshake data size
    // Followed by: handshake_data + random padding
} DAP_ALIGN_PACKED obfuscated_header_t;

// Serialization schema for obfuscated header (LE byte order by default)
static dap_serialize_field_t s_obfuscated_header_fields[] = {
    {
        .name = "cleartext_total_size",
        .type = DAP_SERIALIZE_TYPE_UINT16,
        .offset = offsetof(obfuscated_header_t, cleartext_total_size),
        .size = sizeof(uint16_t),
        .flags = DAP_SERIALIZE_FLAG_LITTLE_ENDIAN  // Explicit LE
    },
    {
        .name = "handshake_size",
        .type = DAP_SERIALIZE_TYPE_UINT16,
        .offset = offsetof(obfuscated_header_t, handshake_size),
        .size = sizeof(uint16_t),
        .flags = DAP_SERIALIZE_FLAG_LITTLE_ENDIAN  // Explicit LE
    }
};

static dap_serialize_schema_t s_obfuscated_header_schema = {
    .magic = DAP_SERIALIZE_MAGIC_NUMBER,
    .version = 1,
    .name = "obfuscated_header",
    .fields = s_obfuscated_header_fields,
    .field_count = sizeof(s_obfuscated_header_fields) / sizeof(s_obfuscated_header_fields[0]),
    .struct_size = sizeof(obfuscated_header_t),
    .validate_func = NULL
};

static bool s_debug_more = false;
/**
 * @brief Get header size using dap_serialize RAW (no metadata)
 */
static size_t s_get_header_size(void)
{
    // Calculate RAW size (fields only, no magic/version/field_count header)
    return dap_serialize_calc_size_raw(&s_obfuscated_header_schema, NULL, NULL, NULL);
}

/**
 * @brief Create cipher key from KDF (with embedded nonce for SALSA2012)
 * 
 * For SALSA2012: KDF generates 40 bytes = [nonce(8)] + [key(32)]
 * The cipher's new_from_data_private_callback extracts nonce automatically.
 * 
 * @param a_packet_size Packet size (used as KDF counter)
 * @param a_existing_key Existing key to update, or NULL to create new
 * @return dap_enc_key_t ready to use, or NULL on error
 */
static dap_enc_key_t* s_get_cipher_key_for_size(size_t a_packet_size, dap_enc_key_t *a_existing_key)
{
    // Generate KDF: 40 bytes for SALSA2012 (8 nonce + 32 key)
    uint8_t l_kdf_key[40];
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
        // SALSA2012 will extract nonce from bytes [0:7], key from [8:39]
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
    // CRITICAL: Account for SALSA2012 nonce (8 bytes) that will be prepended during encryption
    // So cleartext size should be [MIN - 8, MAX - 8] to get encrypted size in [MIN, MAX]
    const size_t SALSA_NONCE_SIZE = 8;
    size_t l_min_cleartext = DAP_TRANSPORT_OBFUSCATION_MIN_SIZE - SALSA_NONCE_SIZE;
    size_t l_max_cleartext = DAP_TRANSPORT_OBFUSCATION_MAX_SIZE - SALSA_NONCE_SIZE;
    
    uint32_t l_size_range = l_max_cleartext - l_min_cleartext;
    uint32_t l_random_offset;
    randombytes((uint8_t*)&l_random_offset, sizeof(l_random_offset));
    l_random_offset = l_random_offset % (l_size_range + 1);
    
    size_t l_final_size = l_min_cleartext + l_random_offset;
    
    // Get header size from serializer
    size_t l_header_size = s_get_header_size();
    
    // Ensure we have enough space for header + handshake
    size_t l_required_size = l_header_size + a_handshake_size;
    if (l_final_size < l_required_size) {
        log_it(L_ERROR, "Obfuscation: final size %zu too small for handshake %zu (header=%zu)",
               l_final_size, a_handshake_size, l_header_size);
        return -2;
    }
    
    // Build cleartext packet: [header] + [handshake] + [padding]
    size_t l_padding_size = l_final_size - l_required_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_final_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -3;
    }
    
    // Serialize header using dap_serialize (platform-independent, LE)
    obfuscated_header_t l_header = {
        .cleartext_total_size = (uint16_t)l_final_size,
        .handshake_size = (uint16_t)a_handshake_size
    };
    
    dap_serialize_result_t l_ser_result = dap_serialize_to_buffer_raw(
        &s_obfuscated_header_schema,
        &l_header,
        l_cleartext,
        l_header_size,
        NULL
    );
    
    if (l_ser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to serialize obfuscated header: %s", l_ser_result.error_message);
        DAP_DELETE(l_cleartext);
        return -4;
    }
    
    // Copy handshake data
    memcpy(l_cleartext + l_header_size, a_handshake_data, a_handshake_size);
    
    // Fill random padding
    if (l_padding_size > 0) {
        randombytes(l_cleartext + l_required_size, l_padding_size);
    }
    
    // Create cipher key (KDF based on cleartext_total_size)
    debug_if(s_debug_more,L_DEBUG, "OBFUSCATE: final_size=%zu, handshake=%zu, padding=%zu", 
           l_final_size, a_handshake_size, l_padding_size);
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(l_final_size, NULL);
    if (!l_key) {
        DAP_DELETE(l_cleartext);
        return -5;
    }
    
    // Encrypt with SALSA2012
    size_t l_encrypted_max = l_final_size + 256;
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_cleartext);
        return -6;
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
        return -7;
    }
    
    // Return encrypted packet
    *a_obfuscated_data = l_encrypted;
    *a_obfuscated_size = l_encrypted_size;
    
    debug_if(s_debug_more,L_DEBUG, "Obfuscated handshake: %zu bytes → %zu bytes (padding=%zu), cleartext=%zu",
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
        return -2;
    }
    
    // Calculate cleartext size: encrypted = cleartext + SALSA20_NONCE_SIZE (prepended)
    size_t l_cleartext_size = a_obfuscated_size - SALSA20_NONCE_SIZE;
    
    // Create cipher key for decryption using cleartext_size for KDF
    debug_if(s_debug_more,L_DEBUG, "DEOBFUSCATE: encrypted_size=%zu, cleartext_size=%zu", 
           a_obfuscated_size, l_cleartext_size);
    
    dap_enc_key_t *l_key = s_get_cipher_key_for_size(l_cleartext_size, NULL);
    if (!l_key) {
        return -3;
    }
    
    // Decrypt with SALSA2012
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
        log_it(L_WARNING, "Decryption failed");
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Get header size from serializer
    size_t l_header_size = s_get_header_size();
    
    // Parse cleartext header using dap_serialize
    if (l_decrypted_size < l_header_size) {
        debug_if(s_debug_more, L_WARNING, "Decrypted packet too small: %zu bytes (need %zu for header)", 
               l_decrypted_size, l_header_size);
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    // Deserialize header
    obfuscated_header_t l_header;
    dap_deserialize_result_t l_deser_result = dap_deserialize_from_buffer_raw(
        &s_obfuscated_header_schema,
        l_decrypted,
        l_header_size,
        &l_header,
        NULL
    );
    
    if (l_deser_result.error_code != 0) {
        debug_if(s_debug_more,L_WARNING, "Failed to deserialize header: %s", l_deser_result.error_message);
        DAP_DELETE(l_decrypted);
        return -7;
    }
    
    // Validate cleartext_total_size matches actual decrypted size
    if (l_header.cleartext_total_size != l_decrypted_size) {
        debug_if(s_debug_more, L_WARNING, "Cleartext size mismatch: header=%u, actual=%zu",
               l_header.cleartext_total_size, l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -8;
    }
    
    // Validate handshake_size
    if (l_header.handshake_size == 0 || l_header.handshake_size > (l_decrypted_size - l_header_size)) {
        log_it(L_WARNING, "Invalid handshake size: %u (packet=%zu)", 
               l_header.handshake_size, l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -9;
    }
    
    // Extract handshake data
    uint8_t *l_handshake = DAP_NEW_SIZE(uint8_t, l_header.handshake_size);
    if (!l_handshake) {
        log_it(L_ERROR, "Failed to allocate handshake buffer");
        DAP_DELETE(l_decrypted);
        return -10;
    }
    
    memcpy(l_handshake, l_decrypted + l_header_size, l_header.handshake_size);
    DAP_DELETE(l_decrypted);
    
    // Return handshake
    *a_handshake_data = l_handshake;
    *a_handshake_size = l_header.handshake_size;
    
    debug_if(s_debug_more,L_DEBUG, "Deobfuscated: %zu bytes → %u bytes (cleartext_total=%u)",
           a_obfuscated_size, l_header.handshake_size, l_header.cleartext_total_size);
    
    return 0;
}
