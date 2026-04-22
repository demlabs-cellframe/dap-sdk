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
#include "dap_memwipe.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_ring_serialize_schema.h"
#include "dap_serialize.h"

// Детальное логирование для Chipmunk Ring модуля
static bool s_debug_more = false;
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
    // Initialize Chipmunk_Ring if not already done
    if (chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk_Ring");
        return -EFAULT;
    }


    debug_if(s_debug_more, L_INFO, "Chipmunk_Ring initialized successfully");
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
    debug_if(s_debug_more, L_DEBUG, "Generating new Chipmunk_Ring key with seed size: %zu, key size: %zu",
           a_seed_size, a_key_size);

    dap_return_val_if_fail(a_key, -EINVAL);
    // Allow NULL seed for random generation, but require valid seed_size if seed provided
    if (a_seed && a_seed_size != 32) {
        log_it(L_ERROR, "Invalid seed size: expected 32, got %zu", a_seed_size);
        return -EINVAL;
    }

    // Set key type
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING;

    // CR-D15.C: ring keypair is now a Chipmunk hypertree keypair.
    a_key->pub_key_data_size  = CHIPMUNK_RING_PUBLIC_KEY_SIZE;   // CHIPMUNK_HT_PUBLIC_KEY_SIZE (~2112 B)
    a_key->priv_key_data_size = CHIPMUNK_RING_PRIVATE_KEY_SIZE;  // CHIPMUNK_HT_PRIVATE_KEY_SIZE (~4260 B)

    // Allocate key data
    a_key->pub_key_data = DAP_NEW_Z_SIZE(byte_t, a_key->pub_key_data_size);
    a_key->priv_key_data = DAP_NEW_Z_SIZE(byte_t, a_key->priv_key_data_size);

    if (!a_key->pub_key_data || !a_key->priv_key_data) {
        log_it(L_ERROR, "Failed to allocate key data");
        if (a_key->pub_key_data) free(a_key->pub_key_data);
        if (a_key->priv_key_data) free(a_key->priv_key_data);
        return -1;
    }

    // CR-D15.C: delegate to chipmunk_ring_key_new_generate / chipmunk_ring_key_new,
    // which build a hypertree keypair via chipmunk_ht_keypair(_from_seed) and
    // serialise the canonical byte layout into the caller's buffers.
    int l_rc;
    if (a_seed) {
        l_rc = chipmunk_ring_key_new_generate(a_key, a_seed, a_seed_size, a_key_size);
    } else {
        l_rc = chipmunk_ring_key_new(a_key);
    }
    if (l_rc != 0) {
        log_it(L_ERROR, "Failed to generate Chipmunk_Ring hypertree key (rc=%d)", l_rc);
        free(a_key->pub_key_data);
        a_key->pub_key_data = NULL;
        free(a_key->priv_key_data);
        a_key->priv_key_data = NULL;
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "Chipmunk_Ring key generated successfully");
    return 0;
}

/**
 * @brief Delete Chipmunk_Ring key
 */
void dap_enc_chipmunk_ring_key_delete(struct dap_enc_key *a_key) {
    if (a_key) {
        // Securely zero sensitive data before freeing
        if (a_key->priv_key_data) {
            memset(a_key->priv_key_data, 0, a_key->priv_key_data_size);
            DAP_DELETE(a_key->priv_key_data);
            a_key->priv_key_data = NULL;
            a_key->priv_key_data_size = 0;
        }
        if (a_key->pub_key_data) {
            memset(a_key->pub_key_data, 0, a_key->pub_key_data_size);
            DAP_DELETE(a_key->pub_key_data);
            a_key->pub_key_data = NULL;
            a_key->pub_key_data_size = 0;
        }
    }
}

/**
 * @brief Get signature size for given ring parameters
 */
size_t dap_enc_chipmunk_ring_get_signature_size(size_t a_ring_size, uint32_t a_required_signers, bool a_use_embedded_keys) {
    // Use the quantum-resistant signature size calculation with all parameters
    return chipmunk_ring_get_signature_size(a_ring_size, a_required_signers, a_use_embedded_keys);
}

/**
 * @brief Create Chipmunk_Ring signature
 */
int dap_enc_chipmunk_ring_sign(void *a_priv_key,
                              const void *a_data,
                              size_t a_data_size,
                              uint8_t **a_ring_pub_keys,
                              size_t a_ring_size,
                              uint32_t a_required_signers,
                              uint8_t *a_signature,
                              size_t a_signature_size)
{
    debug_if(s_debug_more, L_INFO, "=== dap_enc_chipmunk_ring_sign START ===");
    debug_if(s_debug_more, L_INFO, "priv_key=%p, data=%p, data_size=%zu", a_priv_key, a_data, a_data_size);
    debug_if(s_debug_more, L_INFO, "ring_pub_keys=%p, ring_size=%zu, required_signers=%u", 
             a_ring_pub_keys, a_ring_size, a_required_signers);
    debug_if(s_debug_more, L_INFO, "signature=%p, signature_size=%zu", a_signature, a_signature_size);

    if (!a_priv_key || !a_ring_pub_keys || !a_signature) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk_Ring signature");
        return -EINVAL;
    }
    
    if (a_required_signers < 1 || a_required_signers > a_ring_size) {
        log_it(L_ERROR, "Invalid required_signers: %u (ring_size=%zu)", a_required_signers, a_ring_size);
        return -EINVAL;
    }
    
    // Allow empty messages (a_data can be NULL if a_data_size is 0)
    if (a_data_size > 0 && !a_data) {
        log_it(L_ERROR, "Non-zero data size but NULL data pointer");
        return -EINVAL;
    }

    if (a_ring_size == 0) {
        log_it(L_ERROR, "Invalid ring size");
        return -EINVAL;
    }

    if (a_ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        log_it(L_ERROR, "Ring size exceeds maximum allowed: %zu > %d",
               a_ring_size, CHIPMUNK_RING_MAX_RING_SIZE);
        return -EINVAL;
    }

    // Expected signature size (assume embedded keys for this interface)
    size_t expected_size = dap_enc_chipmunk_ring_get_signature_size(a_ring_size, a_required_signers, true);
    if (a_signature_size < expected_size) {
        log_it(L_ERROR, "Signature buffer too small: %zu < %zu",
               a_signature_size, expected_size);
        return -EINVAL;
    }

    /*
     * CR-D15.C: chipmunk_ring_sign bumps leaf_index inside a_private_key
     * and serialises the result back.  The caller's buffer MUST receive
     * that persistent update — otherwise every sign call would reuse the
     * same leaf_index and CR-D3's one-time exhaustion guard would fire on
     * the second call (at best) or allow silent key recovery (at worst).
     *
     * We therefore stage the bytes into a local chipmunk_ring_private_key_t
     * (so we can pass a typed pointer into the pure-C API), invoke the
     * signer, and flush the mutated bytes back into the caller's buffer
     * via memcpy on success.  On failure we still flush if the signer
     * managed to observe the leaf (its contract is "buffer reflects the
     * post-sign state on return"), which prevents the buffer drifting
     * out of sync with the actual tree state.
     */
    if (sizeof(((chipmunk_ring_private_key_t*)0)->data) != CHIPMUNK_RING_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR,
               "CR-D15.C: chipmunk_ring_private_key_t.data size mismatch at compile-time (have %zu, expected %d)",
               sizeof(((chipmunk_ring_private_key_t*)0)->data),
               (int)CHIPMUNK_RING_PRIVATE_KEY_SIZE);
        return -EFAULT;
    }
    chipmunk_ring_private_key_t l_priv_key;
    memcpy(l_priv_key.data, a_priv_key, CHIPMUNK_RING_PRIVATE_KEY_SIZE);

    // Create ring container
    // debug_if(s_debug_more, L_INFO, "Creating ring container for ring_size=%zu", a_ring_size);
    chipmunk_ring_container_t l_ring;
    memset(&l_ring, 0, sizeof(l_ring));
    l_ring.size = (uint32_t)a_ring_size;
    
    // Allocate memory for ring hash (dynamic size)
    l_ring.ring_hash_size = CHIPMUNK_RING_RING_HASH_SIZE;
    l_ring.ring_hash = DAP_NEW_Z_SIZE(uint8_t, l_ring.ring_hash_size);
    if (!l_ring.ring_hash) {
        log_it(L_ERROR, "Failed to allocate memory for ring hash");
        return -ENOMEM;
    }

    /*
     * Size bookkeeping for the ring-member public keys.  Post CR-D15.C
     * these are serialised chipmunk_ht_public_key_t blobs
     * (CHIPMUNK_RING_PUBLIC_KEY_SIZE bytes = rho_seed||hasher_seed||root).
     * The overflow check below keeps us fail-fast on absurd ring sizes
     * rather than allocating a wrapped buffer.
     */
    const size_t key_size = CHIPMUNK_RING_PUBLIC_KEY_SIZE;
    if (key_size == 0) {
        log_it(L_ERROR, "Internal error: CHIPMUNK_RING_PUBLIC_KEY_SIZE resolved to zero");
        DAP_DELETE(l_ring.ring_hash);
        return -EINVAL;
    }
    const size_t total_size = key_size * a_ring_size;
    if (total_size / key_size != a_ring_size) {
        log_it(L_ERROR,
               "Integer overflow in ring pk allocation: %zu * %zu != %zu",
               key_size, a_ring_size, total_size);
        DAP_DELETE(l_ring.ring_hash);
        return -EINVAL;
    }

    l_ring.public_keys = (chipmunk_ring_public_key_t*)calloc(a_ring_size,
                                                             sizeof(chipmunk_ring_public_key_t));
    if (!l_ring.public_keys) {
        log_it(L_ERROR, "Failed to allocate %zu ring public keys", a_ring_size);
        DAP_DELETE(l_ring.ring_hash);
        return -ENOMEM;
    }

    for (size_t i = 0; i < a_ring_size; i++) {
        if (!a_ring_pub_keys[i]) {
            log_it(L_ERROR, "Null public key at index %zu", i);
            free(l_ring.public_keys);
            DAP_DELETE(l_ring.ring_hash);
            return -EINVAL;
        }
        memcpy(l_ring.public_keys[i].data, a_ring_pub_keys[i], key_size);
    }

    /*
     * Ring-hash = SHA3-256(pk_0 || pk_1 || ... || pk_{N-1}).  We build the
     * concatenated buffer in a single scratch allocation and wipe it
     * afterwards — the hash itself is a public identifier so no secret
     * material is leaked, but keeping scratch tight matches the rest of
     * the module's hygiene.
     */
    uint8_t *l_combined_keys = DAP_NEW_SIZE(uint8_t, total_size);
    if (!l_combined_keys) {
        log_it(L_ERROR, "Failed to allocate %zu bytes of scratch for ring hash", total_size);
        free(l_ring.public_keys);
        DAP_DELETE(l_ring.ring_hash);
        return -ENOMEM;
    }
    for (size_t i = 0; i < a_ring_size; i++) {
        memcpy(l_combined_keys + i * key_size, a_ring_pub_keys[i], key_size);
    }
    int l_hash_result = dap_chipmunk_hash_sha3_256(l_ring.ring_hash,
                                                   l_combined_keys, total_size);
    DAP_DEL_MULTY(l_combined_keys);
    if (l_hash_result != 0) {
        log_it(L_ERROR, "Failed to hash ring public keys");
        free(l_ring.public_keys);
        DAP_DELETE(l_ring.ring_hash);
        return -EFAULT;
    }

    chipmunk_ring_signature_t l_ring_sig;
    memset(&l_ring_sig, 0, sizeof(l_ring_sig));

    // Always embed ring public keys in the signature so that chipmunk_ring_verify
    // is self-contained (no external resolver needed).
    const bool use_embedded_keys = true;

    int l_result = chipmunk_ring_sign(&l_priv_key, a_data, a_data_size,
                                     &l_ring, a_required_signers, use_embedded_keys, &l_ring_sig);

    free(l_ring.public_keys);
    DAP_DELETE(l_ring.ring_hash);

    /*
     * CR-D15.C: flush the (possibly-mutated) private-key bytes back into
     * the caller's buffer REGARDLESS of success, because chipmunk_ring_sign
     * may have consumed a leaf even on a later-stage failure.  Leaving the
     * caller's buffer with a stale leaf_index is the worst outcome — it
     * would enable silent reuse of the same HOTS one-time key.
     */
    memcpy(a_priv_key, l_priv_key.data, CHIPMUNK_RING_PRIVATE_KEY_SIZE);
    dap_memwipe(l_priv_key.data, sizeof(l_priv_key.data));

    if (l_result != 0) {
        log_it(L_ERROR, "Chipmunk_Ring signature creation failed: %d", l_result);
        return l_result;
    }

    l_result = chipmunk_ring_signature_to_bytes(&l_ring_sig, a_signature, a_signature_size);
    chipmunk_ring_signature_free(&l_ring_sig);
    if (l_result != 0) {
        log_it(L_ERROR, "Failed to serialize Chipmunk_Ring signature: %d", l_result);
        return l_result;
    }

    debug_if(s_debug_more, L_INFO,
             "Chipmunk_Ring signature created successfully (ring size: %zu, required_signers: %u, embedded_keys: %s)",
             a_ring_size, a_required_signers, use_embedded_keys ? "true" : "false");

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


/*
 * Ring signatures fundamentally need the full ring (set of public keys) at
 * sign/verify time.  The plain dap_enc_key::sign_get / sign_verify callbacks
 * accept only a message + key, which is insufficient to distinguish "ring
 * of 1 (signer only)" from "ring of N".  Callers MUST go through the
 * higher-level dap_enc_chipmunk_ring_sign / chipmunk_ring_verify entry
 * points that take an explicit ring argument.  The callbacks below are
 * retained for symmetry with other signature types (so dap_enc_key
 * dispatch tables stay uniform) and return a loud, documented error.
 */
int dap_enc_chipmunk_ring_get_sign(struct dap_enc_key *a_key, const void *a_data,
                                  size_t a_data_size, void *a_output, size_t a_output_size) {
    (void)a_key; (void)a_data; (void)a_data_size;
    (void)a_output; (void)a_output_size;
    log_it(L_ERROR,
           "Chipmunk_Ring signing cannot go through the plain dap_enc_key sign callback — "
           "ring signatures need the explicit ring (use dap_enc_chipmunk_ring_sign())");
    return -ENOTSUP;
}

int dap_enc_chipmunk_ring_verify_sign(struct dap_enc_key *a_key, const void *a_data,
                                     size_t a_data_size, void *a_sign, size_t a_sign_size) {
    (void)a_key; (void)a_data; (void)a_data_size;
    (void)a_sign; (void)a_sign_size;
    log_it(L_ERROR,
           "Chipmunk_Ring verification cannot go through the plain dap_enc_key verify callback — "
           "ring signatures need the explicit ring (use chipmunk_ring_verify())");
    return -ENOTSUP;
}

size_t dap_enc_chipmunk_ring_write_signature(const void *a_sign, size_t a_sign_size, uint8_t *a_buf) {
    if (!a_sign || !a_buf) {
        log_it(L_ERROR, "Invalid parameters for ChipmunkRing signature serialization");
        return 0;
    }
    
    const chipmunk_ring_signature_t *l_signature = (const chipmunk_ring_signature_t *)a_sign;
    
    // Validate structure before serialization
    if (!l_signature->signature || l_signature->signature_size == 0) {
        log_it(L_ERROR, "Invalid signature field: ptr=%p, size=%zu", l_signature->signature, l_signature->signature_size);
        return 0;
    }
    
    if (!l_signature->challenge || l_signature->challenge_size == 0) {
        log_it(L_ERROR, "Invalid challenge field: ptr=%p, size=%zu", l_signature->challenge, l_signature->challenge_size);
        return 0;
    }
    
    // Skip size calculation to avoid use-after-free in acorn_proofs
    // The serializer will check buffer size internally
    
    // Use universal serializer
    dap_serialize_result_t l_result = dap_serialize_to_buffer(&chipmunk_ring_signature_schema, 
                                                             l_signature, 
                                                             a_buf, 
                                                             a_sign_size, 
                                                             NULL);
    
    if (l_result.error_code != DAP_SERIALIZE_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize Chipmunk_Ring signature: %d", l_result.error_code);
        return 0;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Chipmunk_Ring signature serialized: %zu bytes", l_result.bytes_written);
    return l_result.bytes_written;
}

/*
 * CR-D15.C / CR-D10 alignment: chipmunk_ring priv_key_data and pub_key_data
 * already hold the canonical, fixed-layout hypertree bytes (see
 * chipmunk_hypertree.c for the exact serialisation).  The read/write
 * helpers below are therefore simple framed copies — no ad-hoc format,
 * no re-encoding of internal fields.  We DO NOT try to paste a magic
 * header or version tag here because dap_sign.c already carries an
 * envelope around the payload (signature type id + length).  Adding a
 * second wrapper would make the wire format strictly longer without
 * improving parseability.
 *
 * Return conventions for the dap_enc_key callback table:
 *   write_* : returns number of bytes written on success, 0 on failure.
 *   read_*  : returns malloc'd copy on success, NULL on failure.
 *             Caller is responsible for DAP_DELETE.
 */
size_t dap_enc_chipmunk_ring_write_private_key(const void *a_private_key,
                                               size_t a_private_key_size,
                                               uint8_t *a_buf) {
    if (!a_private_key || !a_buf) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk_Ring private key serialization");
        return 0;
    }
    if (a_private_key_size != CHIPMUNK_RING_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR,
               "Chipmunk_Ring private key size mismatch on write: have %zu, expected %d",
               a_private_key_size, (int)CHIPMUNK_RING_PRIVATE_KEY_SIZE);
        return 0;
    }
    memcpy(a_buf, a_private_key, CHIPMUNK_RING_PRIVATE_KEY_SIZE);
    debug_if(s_debug_more, L_DEBUG, "Chipmunk_Ring private key serialised: %d bytes",
             (int)CHIPMUNK_RING_PRIVATE_KEY_SIZE);
    return CHIPMUNK_RING_PRIVATE_KEY_SIZE;
}

size_t dap_enc_chipmunk_ring_write_public_key(const void *a_public_key,
                                              size_t a_public_key_size,
                                              uint8_t *a_buf) {
    if (!a_public_key || !a_buf) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk_Ring public key serialization");
        return 0;
    }
    if (a_public_key_size != CHIPMUNK_RING_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR,
               "Chipmunk_Ring public key size mismatch on write: have %zu, expected %d",
               a_public_key_size, (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE);
        return 0;
    }
    memcpy(a_buf, a_public_key, CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    debug_if(s_debug_more, L_DEBUG, "Chipmunk_Ring public key serialised: %d bytes",
             (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    return CHIPMUNK_RING_PUBLIC_KEY_SIZE;
}

size_t dap_enc_chipmunk_ring_ser_private_key_size(struct dap_enc_key *a_key) {
    (void)a_key;
    return CHIPMUNK_RING_PRIVATE_KEY_SIZE;
}

size_t dap_enc_chipmunk_ring_ser_public_key_size(struct dap_enc_key *a_key) {
    (void)a_key;
    return CHIPMUNK_RING_PUBLIC_KEY_SIZE;
}

/*
 * The ring signature itself is variable-length (depends on ring size,
 * embedded keys, acorn proofs, etc.) and is serialised by
 * chipmunk_ring_signature_to_bytes on the sign side via the universal
 * dap_serialize schema.  The read helper below therefore simply hands
 * back a freshly-allocated bytewise clone — actual parsing happens at
 * verify time (chipmunk_ring_signature_from_bytes).  If a buffer cannot
 * round-trip through dap_serialize at verify, the caller will see a
 * clean rc<0 from chipmunk_ring_verify; no corrupted output reaches
 * downstream code.
 */
uint8_t* dap_enc_chipmunk_ring_read_signature(const uint8_t *a_buf, size_t a_buf_size) {
    if (!a_buf || a_buf_size == 0) {
        log_it(L_ERROR, "Invalid buffer for Chipmunk_Ring signature deserialization");
        return NULL;
    }
    uint8_t *l_sign = DAP_NEW_SIZE(uint8_t, a_buf_size);
    if (!l_sign) {
        log_it(L_ERROR, "Failed to allocate %zu bytes for Chipmunk_Ring signature", a_buf_size);
        return NULL;
    }
    memcpy(l_sign, a_buf, a_buf_size);
    return l_sign;
}

uint8_t* dap_enc_chipmunk_ring_read_private_key(const uint8_t *a_buf, size_t a_buf_size) {
    if (!a_buf || a_buf_size != CHIPMUNK_RING_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR,
               "Invalid buffer for Chipmunk_Ring private key deserialization (size=%zu, expected=%d)",
               a_buf_size, (int)CHIPMUNK_RING_PRIVATE_KEY_SIZE);
        return NULL;
    }
    uint8_t *l_priv_key = DAP_NEW_SIZE(uint8_t, CHIPMUNK_RING_PRIVATE_KEY_SIZE);
    if (!l_priv_key) {
        log_it(L_ERROR, "Failed to allocate %d bytes for Chipmunk_Ring private key",
               (int)CHIPMUNK_RING_PRIVATE_KEY_SIZE);
        return NULL;
    }
    memcpy(l_priv_key, a_buf, CHIPMUNK_RING_PRIVATE_KEY_SIZE);
    return l_priv_key;
}

uint8_t* dap_enc_chipmunk_ring_read_public_key(const uint8_t *a_buf, size_t a_buf_size) {
    if (!a_buf || a_buf_size != CHIPMUNK_RING_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR,
               "Invalid buffer for Chipmunk_Ring public key deserialization (size=%zu, expected=%d)",
               a_buf_size, (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE);
        return NULL;
    }
    uint8_t *l_pub_key = DAP_NEW_SIZE(uint8_t, CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    if (!l_pub_key) {
        log_it(L_ERROR, "Failed to allocate %d bytes for Chipmunk_Ring public key",
               (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE);
        return NULL;
    }
    memcpy(l_pub_key, a_buf, CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    return l_pub_key;
}

size_t dap_enc_chipmunk_ring_deser_sig_size(struct dap_enc_key *a_key) {
    // Ring signature is variable-length: total size is carried in the
    // dap_sign envelope.  The dap_enc_key dispatcher uses this hook to
    // probe fixed-size signature types (e.g. pure Chipmunk) — returning
    // 0 tells it to read the length from the wire instead.
    (void)a_key;
    return 0;
}

size_t dap_enc_chipmunk_ring_deser_public_key_size(struct dap_enc_key *a_key) {
    (void)a_key;
    return CHIPMUNK_RING_PUBLIC_KEY_SIZE;
}

size_t dap_enc_chipmunk_ring_deser_private_key_size(struct dap_enc_key *a_key) {
    (void)a_key;
    return CHIPMUNK_RING_PRIVATE_KEY_SIZE;
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

/**
 * @brief Get current post-quantum parameters (wrapper)
 */
int dap_enc_chipmunk_ring_get_params(chipmunk_ring_pq_params_t *params) {
    return chipmunk_ring_get_params(params);
}

/**
 * @brief Set post-quantum parameters (wrapper)  
 */
int dap_enc_chipmunk_ring_set_params(const chipmunk_ring_pq_params_t *params) {
    return chipmunk_ring_set_params(params);
}

// REMOVED: dap_enc_chipmunk_ring_get_layer_sizes - quantum layers replaced by Acorn Verification

/**
 * @brief Reset parameters to defaults (wrapper)
 */
int dap_enc_chipmunk_ring_reset_params(void) {
    return chipmunk_ring_reset_params();
}
