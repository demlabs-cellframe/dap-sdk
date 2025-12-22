/*
 * Authors:
 * Cellframe Team <admin@cellframe.net>
 * 
 * Copyright  (c) 2017-2025 Cellframe Team
 * All rights reserved.
 */

#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>  // For htobe64 (network byte order)

#include "dap_enc_kdf.h"
#include "dap_common.h"
#include "KeccakHash.h"  // For shake256

#define LOG_TAG "dap_enc_kdf"

// SHAKE256 wrapper (from XKCP library)
extern void shake256(unsigned char *output, size_t outlen, const unsigned char *input, size_t inlen);

/**
 * @brief Extract shared secret from dap_enc_key_t (universal for all KEM types)
 */
static int s_extract_shared_secret(const dap_enc_key_t *a_key, 
                                    const void **a_secret_out, size_t *a_secret_size_out)
{
    if (!a_key) {
        log_it(L_ERROR, "KDF: key is NULL");
        return -1;
    }
    
    // Priority 1: Check shared_key (KEM result storage)
    if (a_key->shared_key && a_key->shared_key_size > 0) {
        *a_secret_out = a_key->shared_key;
        *a_secret_size_out = a_key->shared_key_size;
        return 0;
    }
    
    // Priority 2: Check priv_key_data (fallback for some implementations)
    if (a_key->priv_key_data && a_key->priv_key_data_size > 0) {
        *a_secret_out = a_key->priv_key_data;
        *a_secret_size_out = a_key->priv_key_data_size;
        return 0;
    }
    
    log_it(L_ERROR, "KDF: no shared secret found in key (type=%d)", a_key->type);
    return -2;
}

/**
 * @brief Derive a key from dap_enc_key_t shared secret
 */
int dap_enc_kdf_derive_from_key(const dap_enc_key_t *a_kem_key,
                                 const char *a_context, size_t a_context_size,
                                 uint64_t a_counter,
                                 void *a_derived_key, size_t a_derived_key_size)
{
    // Extract shared secret
    const void *l_secret = NULL;
    size_t l_secret_size = 0;
    
    int ret = s_extract_shared_secret(a_kem_key, &l_secret, &l_secret_size);
    if (ret != 0) {
        return ret;
    }
    
    // Use low-level derive function
    return dap_enc_kdf_derive(l_secret, l_secret_size,
                              a_context, a_context_size,
                              a_counter,
                              a_derived_key, a_derived_key_size);
}

/**
 * @brief Derive a key using SHAKE256 KDF (low-level)
 */
int dap_enc_kdf_derive(const void *a_base_secret, size_t a_base_secret_size,
                       const char *a_context, size_t a_context_size,
                       uint64_t a_counter,
                       void *a_derived_key, size_t a_derived_key_size)
{
    // Validate inputs
    if (!a_base_secret || a_base_secret_size == 0) {
        log_it(L_ERROR, "KDF: base secret is NULL or empty");
        return -1;
    }
    
    if (!a_derived_key || a_derived_key_size == 0) {
        log_it(L_ERROR, "KDF: output buffer is NULL or zero size");
        return -2;
    }
    
    // Context is optional
    if (a_context_size > 0 && !a_context) {
        log_it(L_ERROR, "KDF: context size specified but context is NULL");
        return -3;
    }
    
    // Prepare input buffer: base_secret || context || counter_be64
    size_t l_input_size = a_base_secret_size + a_context_size + sizeof(uint64_t);
    uint8_t *l_input = DAP_NEW_SIZE(uint8_t, l_input_size);
    if (!l_input) {
        log_it(L_ERROR, "KDF: failed to allocate %zu bytes for input buffer", l_input_size);
        return -4;
    }
    
    // Copy base secret
    memcpy(l_input, a_base_secret, a_base_secret_size);
    
    // Copy context (if any)
    if (a_context_size > 0) {
        memcpy(l_input + a_base_secret_size, a_context, a_context_size);
    }
    
    // Append counter in big-endian format (platform-independent)
    uint64_t l_counter_be = htobe64(a_counter);
    memcpy(l_input + a_base_secret_size + a_context_size, &l_counter_be, sizeof(uint64_t));
    
    // Derive key using SHAKE256
    shake256((unsigned char *)a_derived_key, a_derived_key_size, 
             (const unsigned char *)l_input, l_input_size);
    
    // Zero out input buffer (contains sensitive data)
    memset(l_input, 0, l_input_size);
    DAP_DELETE(l_input);
    
    return 0;
}

/**
 * @brief Create symmetric encryption key from KEM shared secret
 */
dap_enc_key_t* dap_enc_kdf_create_cipher_key(const dap_enc_key_t *a_kem_key,
                                              dap_enc_key_type_t a_cipher_type,
                                              const char *a_context, size_t a_context_size,
                                              uint64_t a_counter,
                                              size_t a_key_size)
{
    if (!a_kem_key) {
        log_it(L_ERROR, "KDF: KEM key is NULL");
        return NULL;
    }
    
    // Derive seed using KDF
    uint8_t *l_seed = DAP_NEW_SIZE(uint8_t, a_key_size);
    if (!l_seed) {
        log_it(L_ERROR, "KDF: failed to allocate seed buffer");
        return NULL;
    }
    
    int ret = dap_enc_kdf_derive_from_key(a_kem_key, a_context, a_context_size,
                                           a_counter, l_seed, a_key_size);
    if (ret != 0) {
        log_it(L_ERROR, "KDF: failed to derive seed");
        DAP_DELETE(l_seed);
        return NULL;
    }
    
    // Create cipher key from seed
    dap_enc_key_t *l_cipher_key = dap_enc_key_new_generate(a_cipher_type,
                                                            l_seed, a_key_size,
                                                            NULL, 0,
                                                            a_key_size);
    
    // Zero out seed (sensitive data)
    memset(l_seed, 0, a_key_size);
    DAP_DELETE(l_seed);
    
    if (!l_cipher_key) {
        log_it(L_ERROR, "KDF: failed to create cipher key (type=%d)", a_cipher_type);
        return NULL;
    }
    
    return l_cipher_key;
}

/**
 * @brief Derive multiple keys at once
 */
int dap_enc_kdf_derive_multiple(const void *a_base_secret, size_t a_base_secret_size,
                                 const char *a_context, size_t a_context_size,
                                 uint64_t a_start_counter,
                                 void **a_derived_keys, size_t a_num_keys, size_t a_key_size)
{
    if (!a_derived_keys || a_num_keys == 0) {
        log_it(L_ERROR, "KDF: invalid output keys array");
        return -1;
    }
    
    for (size_t i = 0; i < a_num_keys; i++) {
        if (!a_derived_keys[i]) {
            log_it(L_ERROR, "KDF: output key buffer %zu is NULL", i);
            return -2;
        }
        
        int ret = dap_enc_kdf_derive(a_base_secret, a_base_secret_size,
                                      a_context, a_context_size,
                                      a_start_counter + i,
                                      a_derived_keys[i], a_key_size);
        if (ret != 0) {
            log_it(L_ERROR, "KDF: failed to derive key %zu", i);
            return ret;
        }
    }
    
    return 0;
}

/**
 * @brief HKDF-like: Extract + Expand
 * 
 * Extract: PRK = SHAKE256(salt || IKM)
 * Expand: OKM = SHAKE256(PRK || info || 0x01)
 */
int dap_enc_kdf_hkdf(const void *a_salt, size_t a_salt_size,
                     const void *a_ikm, size_t a_ikm_size,
                     const void *a_info, size_t a_info_size,
                     void *a_okm, size_t a_okm_size)
{
    if (!a_ikm || a_ikm_size == 0) {
        log_it(L_ERROR, "HKDF: IKM is NULL or empty");
        return -1;
    }
    
    if (!a_okm || a_okm_size == 0) {
        log_it(L_ERROR, "HKDF: OKM buffer is NULL or zero size");
        return -2;
    }
    
    // EXTRACT: PRK = SHAKE256(salt || IKM)
    // If no salt, use zero-filled salt of length equal to hash output
    size_t l_extract_input_size = a_salt_size + a_ikm_size;
    uint8_t *l_extract_input = DAP_NEW_SIZE(uint8_t, l_extract_input_size);
    if (!l_extract_input) {
        log_it(L_ERROR, "HKDF: failed to allocate extract input buffer");
        return -3;
    }
    
    if (a_salt_size > 0 && a_salt) {
        memcpy(l_extract_input, a_salt, a_salt_size);
    } else {
        // No salt: use zeros
        memset(l_extract_input, 0, 32);  // 32-byte zero salt
        a_salt_size = 32;
    }
    memcpy(l_extract_input + a_salt_size, a_ikm, a_ikm_size);
    
    // PRK should be at least 32 bytes (SHAKE256 standard strength)
    uint8_t l_prk[64];  // Use 64 bytes for extra strength
    shake256(l_prk, sizeof(l_prk), l_extract_input, l_extract_input_size);
    
    memset(l_extract_input, 0, l_extract_input_size);
    DAP_DELETE(l_extract_input);
    
    // EXPAND: OKM = SHAKE256(PRK || info || counter)
    // For simplicity, we use counter=1 (HKDF typically uses multiple iterations)
    size_t l_expand_input_size = sizeof(l_prk) + a_info_size + 1;  // +1 for counter byte
    uint8_t *l_expand_input = DAP_NEW_SIZE(uint8_t, l_expand_input_size);
    if (!l_expand_input) {
        log_it(L_ERROR, "HKDF: failed to allocate expand input buffer");
        memset(l_prk, 0, sizeof(l_prk));
        return -4;
    }
    
    memcpy(l_expand_input, l_prk, sizeof(l_prk));
    if (a_info_size > 0 && a_info) {
        memcpy(l_expand_input + sizeof(l_prk), a_info, a_info_size);
    }
    l_expand_input[l_expand_input_size - 1] = 0x01;  // Counter = 1
    
    shake256((unsigned char *)a_okm, a_okm_size, l_expand_input, l_expand_input_size);
    
    // Zero out sensitive data
    memset(l_prk, 0, sizeof(l_prk));
    memset(l_expand_input, 0, l_expand_input_size);
    DAP_DELETE(l_expand_input);
    
    return 0;
}

