/*
 * Authors:
 * [Developer Name] <email@demlabs.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
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

#include "dap_enc_chipmunk_ring.h"

#include <errno.h>

#include "dap_common.h"
#include "dap_crypto_common.h"
#include "chipmunk/chipmunk.h"

// Детальное логирование для Chipmunk Ring модуля
static bool s_debug_more = true;
#include "dap_enc_key.h"
#include "chipmunk/chipmunk_ring.h"
#include "chipmunk/chipmunk_hash.h"

#define LOG_TAG "dap_enc_chipmunk_ring"

/**
 * @brief Initialize Chipmunk_Ring module
 */
int dap_enc_chipmunk_ring_init(void) {
    // Initialize Chipmunk (underlying signature scheme)
    if (chipmunk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk for Chipmunk_Ring");
        return -1;
    }

    log_it(L_INFO, "Chipmunk_Ring initialized successfully");
    return 0;
}

/**
 * @brief Generate new Chipmunk_Ring keypair
 */
int dap_enc_chipmunk_ring_key_new(struct dap_enc_key *a_key) {
    dap_return_val_if_fail(a_key, -EINVAL);

    // Just set the key type - key generation happens in key_new_generate callback
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING;
    return 0;
}

/**
 * @brief Generate keypair from seed
 */
int dap_enc_chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size) {
    log_it(L_DEBUG, "Generating new Chipmunk_Ring key with seed size: %zu, key size: %zu",
           a_seed_size, a_key_size);

    dap_return_val_if_fail(a_key, -EINVAL);
    // Allow NULL seed for random generation, but require valid seed_size if seed provided
    if (a_seed && a_seed_size != 32) {
        log_it(L_ERROR, "Invalid seed size: expected 32, got %zu", a_seed_size);
        return -EINVAL;
    }

    // Set key type
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING;

    // Set key sizes to match Chipmunk specifications
    a_key->pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;   // 4128 bytes
    a_key->priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE; // 4208 bytes

    // Allocate key data
    a_key->pub_key_data = calloc(1, a_key->pub_key_data_size);
    a_key->priv_key_data = calloc(1, a_key->priv_key_data_size);

    if (!a_key->pub_key_data || !a_key->priv_key_data) {
        log_it(L_ERROR, "Failed to allocate key data");
        if (a_key->pub_key_data) free(a_key->pub_key_data);
        if (a_key->priv_key_data) free(a_key->priv_key_data);
        return -1;
    }

    // Generate keys based on whether a seed is provided
    if (a_seed) {
        // Deterministic key generation
        if (chipmunk_keypair_from_seed(a_seed,
                                       a_key->pub_key_data, a_key->pub_key_data_size,
                                       a_key->priv_key_data, a_key->priv_key_data_size) != 0) {
            log_it(L_ERROR, "Failed to generate deterministic Chipmunk_Ring key");
            free(a_key->pub_key_data);
            free(a_key->priv_key_data);
        return -1;
        }
    } else {
        // Random key generation
        if (chipmunk_keypair(a_key->pub_key_data, a_key->pub_key_data_size,
                             a_key->priv_key_data, a_key->priv_key_data_size) != 0) {
            log_it(L_ERROR, "Failed to generate random Chipmunk_Ring key");
            free(a_key->pub_key_data);
            free(a_key->priv_key_data);
            return -1;
        }
    }

    log_it(L_DEBUG, "Chipmunk_Ring key generated successfully");
    return 0;
}

/**
 * @brief Delete Chipmunk_Ring key
 */
void dap_enc_chipmunk_ring_key_delete(struct dap_enc_key *a_key) {
    if (a_key) {
        // Use standard cleanup for sensitive data
        memset(a_key->priv_key_data, 0, a_key->priv_key_data_size);
        memset(a_key->pub_key_data, 0, a_key->pub_key_data_size);
    }
}

/**
 * @brief Get signature size for given ring size
 */
size_t dap_enc_chipmunk_ring_get_signature_size(size_t a_ring_size) {
    if (a_ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        return 0;
    }

    return sizeof(uint32_t) + // ring_size
           sizeof(uint32_t) + // signer_index
           32 +              // linkability_tag
           32 +              // challenge
           a_ring_size * (32 + 32) + // commitments (value + randomness)
           a_ring_size * 32 +  // responses
           CHIPMUNK_SIGNATURE_SIZE; // chipmunk_signature
}

/**
 * @brief Create Chipmunk_Ring signature
 */
int dap_enc_chipmunk_ring_sign(const void *a_priv_key,
                              const void *a_data,
                              size_t a_data_size,
                              uint8_t **a_ring_pub_keys,
                              size_t a_ring_size,
                              size_t a_signer_index,
                              uint8_t *a_signature,
                              size_t a_signature_size)
{
    if (!a_priv_key || !a_data || !a_ring_pub_keys || !a_signature) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk_Ring signature");
        return -EINVAL;
    }

    if (a_ring_size == 0 || a_signer_index >= a_ring_size) {
        log_it(L_ERROR, "Invalid ring size or signer index");
        return -EINVAL;
    }

    if (a_ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        log_it(L_ERROR, "Ring size exceeds maximum allowed: %zu > %d",
               a_ring_size, CHIPMUNK_RING_MAX_RING_SIZE);
        return -EINVAL;
    }

    // Expected signature size
    size_t expected_size = dap_enc_chipmunk_ring_get_signature_size(a_ring_size);
    if (a_signature_size < expected_size) {
        log_it(L_ERROR, "Signature buffer too small: %zu < %zu",
               a_signature_size, expected_size);
        return -EINVAL;
    }

    // Initialize Chipmunk_Ring if not already done
    if (chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk_Ring");
        return -EFAULT;
    }

    // Convert private key
    chipmunk_ring_private_key_t l_priv_key;
    if (sizeof(l_priv_key.data) != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Private key size mismatch");
        return -EFAULT;
    }
    memcpy(l_priv_key.data, a_priv_key, CHIPMUNK_PRIVATE_KEY_SIZE);

    // Create ring container
    // debug_if(s_debug_more, L_INFO, "Creating ring container for ring_size=%zu", a_ring_size);
    chipmunk_ring_container_t l_ring;
    memset(&l_ring, 0, sizeof(l_ring));
    l_ring.size = (uint32_t)a_ring_size;

    size_t key_size = sizeof(chipmunk_ring_public_key_t);
    size_t total_size = key_size * a_ring_size;

    // Check for potential issues
    if (a_ring_size == 0) {
        debug_if(s_debug_more, L_ERROR, "Ring size is 0 - invalid parameter");
        return -EINVAL;
    }
    if (key_size == 0) {
        debug_if(s_debug_more, L_ERROR, "Key size is 0 - struct definition problem");
        return -EINVAL;
    }
    if (total_size / key_size != a_ring_size) {
        debug_if(s_debug_more, L_ERROR, "Integer overflow in size calculation: %zu * %zu != %zu",
                 key_size, a_ring_size, total_size);
        return -EINVAL;
    }

    debug_if(s_debug_more, L_INFO, "About to allocate %zu public keys, key_size=%zu, total_size=%zu",
              a_ring_size, key_size, total_size);

    l_ring.public_keys = (chipmunk_ring_public_key_t*)DAP_NEW_SIZE(chipmunk_ring_public_key_t, a_ring_size);
    debug_if(s_debug_more, L_INFO, "Allocation result: %p", l_ring.public_keys);

    if (!l_ring.public_keys) {
        debug_if(s_debug_more, L_ERROR, "Failed to allocate memory for ring public keys");
        return -ENOMEM;
    }

    // Copy public keys to ring container
    for (size_t i = 0; i < a_ring_size; i++) {
        if (!a_ring_pub_keys[i]) {
            log_it(L_ERROR, "Null public key at index %zu", i);
            DAP_DEL_MULTY(l_ring.public_keys);
            return -EINVAL;
        }
        memcpy(l_ring.public_keys[i].data, a_ring_pub_keys[i], CHIPMUNK_PUBLIC_KEY_SIZE);
    }

    // Generate ring hash for verification consistency
    // Hash all public keys together to create unique ring identifier
    uint8_t *l_combined_keys = DAP_NEW_Z_SIZE(uint8_t, a_ring_size * CHIPMUNK_PUBLIC_KEY_SIZE);
    if (!l_combined_keys) {
        log_it(L_ERROR, "Failed to allocate memory for combined keys");
        DAP_DEL_MULTY(l_ring.public_keys);
        return -ENOMEM;
    }

    // Concatenate all public keys
    for (size_t i = 0; i < a_ring_size; i++) {
        memcpy(l_combined_keys + i * CHIPMUNK_PUBLIC_KEY_SIZE,
               a_ring_pub_keys[i], CHIPMUNK_PUBLIC_KEY_SIZE);
    }

    // Hash all keys together
    int l_hash_result = dap_chipmunk_hash_sha3_256(l_ring.ring_hash, l_combined_keys,
                                                  a_ring_size * CHIPMUNK_PUBLIC_KEY_SIZE);
    DAP_DEL_MULTY(l_combined_keys);

    if (l_hash_result != 0) {
        log_it(L_ERROR, "Failed to hash ring public keys");
        DAP_DEL_MULTY(l_ring.public_keys);
        return -EFAULT;
    }

    // Create signature
    chipmunk_ring_signature_t l_ring_sig;
    memset(&l_ring_sig, 0, sizeof(l_ring_sig));

    int l_result = chipmunk_ring_sign(&l_priv_key, a_data, a_data_size,
                                     &l_ring, (uint32_t)a_signer_index, &l_ring_sig);

    // Clean up ring container
    DAP_DEL_MULTY(l_ring.public_keys);

    if (l_result != 0) {
        log_it(L_ERROR, "Chipmunk_Ring signature creation failed: %d", l_result);
        chipmunk_ring_signature_free(&l_ring_sig);
        return l_result;
    }

    // Serialize signature to output buffer
    l_result = chipmunk_ring_signature_to_bytes(&l_ring_sig, a_signature, a_signature_size);

    // Clean up signature
    chipmunk_ring_signature_free(&l_ring_sig);

    if (l_result != 0) {
        log_it(L_ERROR, "Failed to serialize Chipmunk_Ring signature: %d", l_result);
        return l_result;
    }

    log_it(L_INFO, "Chipmunk_Ring signature created successfully (ring size: %zu, signer: %zu)",
           a_ring_size, a_signer_index);

    return 0;
}

/* ===== CALLBACK FUNCTIONS ===== */

void dap_enc_chipmunk_ring_key_new_callback(struct dap_enc_key *a_key) {
    dap_enc_chipmunk_ring_key_new(a_key);
}

void dap_enc_chipmunk_ring_key_generate_callback(struct dap_enc_key *a_key, const void *a_kex_buf,
                                               size_t a_kex_size, const void *a_seed,
                                               size_t a_seed_size, size_t a_key_size) {
    dap_enc_chipmunk_ring_key_new_generate(a_key, a_seed, a_seed_size, a_key_size);
}


int dap_enc_chipmunk_ring_get_sign(struct dap_enc_key *a_key, const void *a_data,
                                  size_t a_data_size, void *a_output, size_t a_output_size) {
    log_it(L_ERROR, "Chipmunk_Ring signing not implemented via this callback");
    return -1;
}

int dap_enc_chipmunk_ring_verify_sign(struct dap_enc_key *a_key, const void *a_data,
                                     size_t a_data_size, void *a_sign, size_t a_sign_size) {
    log_it(L_ERROR, "Chipmunk_Ring verification not implemented via this callback");
    return -1;
}

size_t dap_enc_chipmunk_ring_write_signature(const void *a_sign, size_t a_sign_size, uint8_t *a_buf) {
    log_it(L_ERROR, "Chipmunk_Ring signature serialization not implemented");
    return 0;
}

size_t dap_enc_chipmunk_ring_write_private_key(const void *a_private_key, size_t a_private_key_size, uint8_t *a_buf) {
    log_it(L_ERROR, "Chipmunk_Ring private key serialization not implemented");
    return 0;
}

size_t dap_enc_chipmunk_ring_write_public_key(const void *a_public_key, size_t a_public_key_size, uint8_t *a_buf) {
    log_it(L_ERROR, "Chipmunk_Ring public key serialization not implemented");
    return 0;
}

size_t dap_enc_chipmunk_ring_ser_private_key_size(struct dap_enc_key *a_key) {
    return a_key->priv_key_data_size;
}

size_t dap_enc_chipmunk_ring_ser_public_key_size(struct dap_enc_key *a_key) {
    return a_key->pub_key_data_size;
}

uint8_t* dap_enc_chipmunk_ring_read_signature(const uint8_t *a_buf, size_t a_buf_size) {
    log_it(L_ERROR, "Chipmunk_Ring signature deserialization not implemented");
    return NULL;
}

uint8_t* dap_enc_chipmunk_ring_read_private_key(const uint8_t *a_buf, size_t a_buf_size) {
    log_it(L_ERROR, "Chipmunk_Ring private key deserialization not implemented");
    return NULL;
}

uint8_t* dap_enc_chipmunk_ring_read_public_key(const uint8_t *a_buf, size_t a_buf_size) {
    log_it(L_ERROR, "Chipmunk_Ring public key deserialization not implemented");
    return NULL;
}

size_t dap_enc_chipmunk_ring_deser_sig_size(struct dap_enc_key *a_key) {
    return 0;
}

size_t dap_enc_chipmunk_ring_deser_public_key_size(struct dap_enc_key *a_key) {
    return a_key->pub_key_data_size;
}

size_t dap_enc_chipmunk_ring_deser_private_key_size(struct dap_enc_key *a_key) {
    return a_key->priv_key_data_size;
}

void dap_enc_chipmunk_ring_signature_delete(uint8_t *a_sign) {
    DAP_DELETE(a_sign);
}

void dap_enc_chipmunk_ring_public_key_delete(uint8_t *a_pub_key) {
    DAP_DELETE(a_pub_key);
}

void dap_enc_chipmunk_ring_private_key_delete(uint8_t *a_priv_key) {
    DAP_DELETE(a_priv_key);
}