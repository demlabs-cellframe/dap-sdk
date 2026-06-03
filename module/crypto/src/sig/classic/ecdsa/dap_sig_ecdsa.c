/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
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

/**
 * @file dap_sig_ecdsa.c
 * @brief ECDSA signature implementation using native secp256k1 code
 */

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_sig_ecdsa.h"
#include "dap_enc_key.h"
#include "dap_rand.h"
#include "dap_hash_sha3.h"

// Native ECDSA implementation
#include "ecdsa_impl.h"

#define LOG_TAG "dap_sig_ecdsa"

// =============================================================================
// Context Management (native implementation needs no context)
// =============================================================================

// For compatibility: context is just a marker that native impl is initialized
typedef struct dap_sig_ecdsa_context {
    uint8_t initialized;
    uint8_t randomness[32];
} dap_sig_ecdsa_context_impl_t;

static _Thread_local bool s_initialized = false;

static void s_ensure_init(void) {
    if (!s_initialized) {
        ecdsa_ecmult_gen_init();
        s_initialized = true;
    }
}

// =============================================================================
// Public API: Context
// =============================================================================

dap_sig_ecdsa_context_t *dap_sig_ecdsa_context_create(unsigned int a_flags)
{
    (void)a_flags;
    s_ensure_init();
    
    dap_sig_ecdsa_context_impl_t *l_ctx = DAP_NEW_Z(dap_sig_ecdsa_context_impl_t);
    if (l_ctx) {
        l_ctx->initialized = 1;
        dap_random_bytes(l_ctx->randomness, 32);
    }
    return (dap_sig_ecdsa_context_t *)l_ctx;
}

void dap_sig_ecdsa_context_destroy(dap_sig_ecdsa_context_t *a_ctx)
{
    if (a_ctx) {
        memset_safe(a_ctx, 0, sizeof(dap_sig_ecdsa_context_impl_t));
        DAP_DELETE(a_ctx);
    }
}

int dap_sig_ecdsa_context_randomize(dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seed32)
{
    if (!a_ctx || !a_seed32) return 0;
    dap_sig_ecdsa_context_impl_t *l_ctx = (dap_sig_ecdsa_context_impl_t *)a_ctx;
    memcpy(l_ctx->randomness, a_seed32, 32);
    return 1;
}

// =============================================================================
// Public API: Keys
// =============================================================================

int dap_sig_ecdsa_seckey_verify(const dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seckey)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_seckey) return 0;
    return ecdsa_scalar_check_seckey(a_seckey) ? 1 : 0;
}

int dap_sig_ecdsa_pubkey_create(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_seckey)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_pubkey || !a_seckey) return 0;
    
    // Parse private key as scalar
    ecdsa_scalar_t l_seckey;
    int overflow = 0;
    ecdsa_scalar_set_b32(&l_seckey, a_seckey, &overflow);
    if (overflow || ecdsa_scalar_is_zero(&l_seckey)) {
        return 0;
    }
    
    // Generate public key: P = seckey * G
    ecdsa_ge_t l_pubkey;
    if (!ecdsa_pubkey_create(&l_pubkey, &l_seckey)) {
        return 0;
    }
    
    // Store as x || y (64 bytes)
    ecdsa_field_normalize(&l_pubkey.x);
    ecdsa_field_normalize(&l_pubkey.y);
    ecdsa_field_get_b32(a_pubkey->data, &l_pubkey.x);
    ecdsa_field_get_b32(a_pubkey->data + 32, &l_pubkey.y);
    
    // Clear sensitive data
    ecdsa_scalar_clear(&l_seckey);
    
    return 1;
}

int dap_sig_ecdsa_pubkey_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output,
    size_t *a_outputlen,
    const dap_sig_ecdsa_pubkey_t *a_pubkey,
    unsigned int a_flags)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_output || !a_outputlen || !a_pubkey) return 0;
    
    // Reconstruct point from stored x || y
    ecdsa_field_t x, y;
    ecdsa_field_set_b32(&x, a_pubkey->data);
    ecdsa_field_set_b32(&y, a_pubkey->data + 32);
    
    ecdsa_ge_t l_point;
    l_point.infinity = false;
    l_point.x = x;
    l_point.y = y;
    
    bool compressed = (a_flags & DAP_SIG_ECDSA_EC_COMPRESSED) != 0;
    return ecdsa_ge_serialize(&l_point, compressed, a_output, a_outputlen) ? 1 : 0;
}

int dap_sig_ecdsa_pubkey_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_input,
    size_t a_inputlen)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_pubkey || !a_input) return 0;
    
    ecdsa_ge_t l_point;
    if (!ecdsa_ge_parse(&l_point, a_input, a_inputlen)) {
        return 0;
    }
    
    // Store as x || y (64 bytes)
    ecdsa_field_normalize(&l_point.x);
    ecdsa_field_normalize(&l_point.y);
    ecdsa_field_get_b32(a_pubkey->data, &l_point.x);
    ecdsa_field_get_b32(a_pubkey->data + 32, &l_point.y);
    
    return 1;
}

// =============================================================================
// Public API: Signatures
// =============================================================================

int dap_sig_ecdsa_sign(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const uint8_t *a_seckey,
    dap_sig_ecdsa_nonce_func_t a_noncefp,
    const void *a_ndata)
{
    (void)a_ctx;
    (void)a_noncefp;
    (void)a_ndata;
    s_ensure_init();
    
    if (!a_sig || !a_msghash32 || !a_seckey) return 0;
    
    // Parse private key
    ecdsa_scalar_t l_seckey;
    int overflow = 0;
    ecdsa_scalar_set_b32(&l_seckey, a_seckey, &overflow);
    if (overflow || ecdsa_scalar_is_zero(&l_seckey)) {
        return 0;
    }
    
    // Generate nonce using RFC6979
    ecdsa_scalar_t l_nonce;
    unsigned int counter = 0;
    while (!ecdsa_nonce_rfc6979(&l_nonce, a_msghash32, a_seckey, NULL, NULL, 0, counter)) {
        counter++;
        if (counter > 100) {
            ecdsa_scalar_clear(&l_seckey);
            return 0;  // Should never happen
        }
    }
    
    // Sign
    ecdsa_sig_t l_sig;
    if (!ecdsa_sign_inner(&l_sig, a_msghash32, &l_seckey, &l_nonce)) {
        ecdsa_scalar_clear(&l_seckey);
        ecdsa_scalar_clear(&l_nonce);
        return 0;
    }
    
    // Serialize to compact format
    ecdsa_sig_serialize(a_sig->data, &l_sig);
    
    // Clear sensitive data
    ecdsa_scalar_clear(&l_seckey);
    ecdsa_scalar_clear(&l_nonce);
    
    return 1;
}

int dap_sig_ecdsa_verify(
    const dap_sig_ecdsa_context_t *a_ctx,
    const dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const dap_sig_ecdsa_pubkey_t *a_pubkey)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_sig || !a_msghash32 || !a_pubkey) return 0;
    
    // Parse signature
    ecdsa_sig_t l_sig;
    if (!ecdsa_sig_parse(&l_sig, a_sig->data)) {
        return 0;
    }
    
    // Reconstruct public key point from stored x || y
    ecdsa_field_t x, y;
    ecdsa_field_set_b32(&x, a_pubkey->data);
    ecdsa_field_set_b32(&y, a_pubkey->data + 32);
    
    ecdsa_ge_t l_pubkey;
    l_pubkey.infinity = false;
    l_pubkey.x = x;
    l_pubkey.y = y;
    
    // Verify point is on curve
    if (!ecdsa_ge_is_valid(&l_pubkey)) {
        return 0;
    }
    
    // Verify signature
    return ecdsa_verify_inner(&l_sig, a_msghash32, &l_pubkey) ? 1 : 0;
}

int dap_sig_ecdsa_signature_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output64,
    const dap_sig_ecdsa_signature_t *a_sig)
{
    (void)a_ctx;
    if (!a_output64 || !a_sig) return 0;
    
    // Already in compact format
    memcpy(a_output64, a_sig->data, 64);
    return 1;
}

int dap_sig_ecdsa_signature_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_input64)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_sig || !a_input64) return 0;
    
    // Validate by parsing
    ecdsa_sig_t l_sig;
    if (!ecdsa_sig_parse(&l_sig, a_input64)) {
        return 0;
    }
    
    memcpy(a_sig->data, a_input64, 64);
    return 1;
}

int dap_sig_ecdsa_signature_normalize(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sigout,
    const dap_sig_ecdsa_signature_t *a_sigin)
{
    (void)a_ctx;
    s_ensure_init();
    
    if (!a_sigin) return 0;
    
    ecdsa_sig_t l_sig;
    if (!ecdsa_sig_parse(&l_sig, a_sigin->data)) {
        return 0;
    }
    
    int was_high = ecdsa_sig_normalize(&l_sig);
    
    if (a_sigout) {
        ecdsa_sig_serialize(a_sigout->data, &l_sig);
    }
    
    return was_high ? 1 : 0;
}

// =============================================================================
// Default nonce functions
// =============================================================================

const dap_sig_ecdsa_nonce_func_t dap_sig_ecdsa_nonce_rfc6979 = NULL;  // NULL = use default
const dap_sig_ecdsa_nonce_func_t dap_sig_ecdsa_nonce_default = NULL;

// =============================================================================
// Integration with dap_enc_key system
// =============================================================================

void dap_sig_ecdsa_key_new(dap_enc_key_t *a_key) 
{
    if (!a_key) return;
    s_ensure_init();
    
    *a_key = (dap_enc_key_t) {
        .type = DAP_ENC_KEY_TYPE_SIG_ECDSA,
        .sign_get = dap_sig_ecdsa_get_sign,
        .sign_verify = dap_sig_ecdsa_verify_sign
    };
}

void dap_sig_ecdsa_key_new_generate(
    dap_enc_key_t *a_key,
    UNUSED_ARG const void *a_kex_buf,
    UNUSED_ARG size_t a_kex_size,
    const void *a_seed,
    size_t a_seed_size,
    UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(!a_key);
    s_ensure_init();
    
    // Allocate key storage
    a_key->priv_key_data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, DAP_SIG_ECDSA_PRIVKEY_SIZE);
    a_key->pub_key_data = DAP_NEW_Z_RET_IF_FAIL(dap_sig_ecdsa_pubkey_t, a_key->priv_key_data);
    
    // Generate private key
    if (a_seed && a_seed_size > 0) {
        // Derive from seed using SHA3-256
        dap_hash_sha3_256_raw(a_key->priv_key_data, (const uint8_t *)a_seed, a_seed_size);
        if (!dap_sig_ecdsa_seckey_verify(NULL, a_key->priv_key_data)) {
            log_it(L_ERROR, "Invalid ECDSA private key from seed");
            DAP_DEL_Z(a_key->priv_key_data);
            DAP_DEL_Z(a_key->pub_key_data);
            return;
        }
    } else {
        // Generate random private key
        do {
            dap_random_bytes(a_key->priv_key_data, DAP_SIG_ECDSA_PRIVKEY_SIZE);
        } while (!dap_sig_ecdsa_seckey_verify(NULL, a_key->priv_key_data));
    }
    
    // Generate public key
    if (!dap_sig_ecdsa_pubkey_create(NULL, a_key->pub_key_data, a_key->priv_key_data)) {
        log_it(L_CRITICAL, "Failed to generate ECDSA public key");
        DAP_DEL_Z(a_key->priv_key_data);
        DAP_DEL_Z(a_key->pub_key_data);
        return;
    }
    
    a_key->priv_key_data_size = DAP_SIG_ECDSA_PRIVKEY_SIZE;
    a_key->pub_key_data_size = sizeof(dap_sig_ecdsa_pubkey_t);
}

int dap_sig_ecdsa_get_sign(
    struct dap_enc_key *a_key,
    const void *a_msg,
    const size_t a_msg_size,
    void *a_sig,
    const size_t a_sig_size)
{
    dap_return_val_if_pass(!a_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(dap_sig_ecdsa_signature_t), -2, 
                               "Invalid signature size");
    dap_return_val_if_pass_err(a_key->priv_key_data_size != DAP_SIG_ECDSA_PRIVKEY_SIZE, -3,
                               "Invalid private key size");
    
    // Hash message
    uint8_t l_msghash[32];
    dap_hash_sha3_256_raw(l_msghash, a_msg, a_msg_size);
    
    // Sign
    if (!dap_sig_ecdsa_sign(NULL, a_sig, l_msghash, a_key->priv_key_data, NULL, NULL)) {
        log_it(L_ERROR, "Failed to sign message");
        return -4;
    }
    return 0;
}

int dap_sig_ecdsa_verify_sign(
    struct dap_enc_key *a_key,
    const void *a_msg,
    const size_t a_msg_size,
    void *a_sig,
    const size_t a_sig_size)
{
    dap_return_val_if_pass(!a_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(dap_sig_ecdsa_signature_t), -2,
                               "Invalid signature size");
    dap_return_val_if_pass_err(a_key->pub_key_data_size != sizeof(dap_sig_ecdsa_pubkey_t), -3,
                               "Invalid public key size");
    
    // Hash message
    uint8_t l_msghash[32];
    dap_hash_sha3_256_raw(l_msghash, a_msg, a_msg_size);
    
    // Verify
    if (!dap_sig_ecdsa_verify(NULL, a_sig, l_msghash, a_key->pub_key_data)) {
        log_it(L_ERROR, "Signature verification failed");
        return -4;
    }
    return 0;
}

uint8_t *dap_sig_ecdsa_write_public_key(const void *a_pubkey, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_pubkey, NULL);
    
    uint8_t *l_buf = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED, NULL);
    size_t l_len = DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED;
    
    if (!dap_sig_ecdsa_pubkey_serialize(NULL, l_buf, &l_len, a_pubkey, DAP_SIG_ECDSA_EC_UNCOMPRESSED) ||
        l_len != DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED) {
        log_it(L_CRITICAL, "Failed to serialize public key");
        DAP_DEL_Z(l_buf);
        return NULL;
    }
    
    if (a_buflen_out) *a_buflen_out = DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED;
    return l_buf;
}

void *dap_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen != DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED, NULL);
    
    dap_sig_ecdsa_pubkey_t *l_pubkey = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_sig_ecdsa_pubkey_t, NULL);
    
    if (!dap_sig_ecdsa_pubkey_parse(NULL, l_pubkey, a_buf, a_buflen)) {
        log_it(L_CRITICAL, "Failed to parse public key");
        DAP_DELETE(l_pubkey);
        return NULL;
    }
    return l_pubkey;
}

uint8_t *dap_sig_ecdsa_write_signature(const void *a_sig, size_t *a_sig_len)
{
    dap_return_val_if_pass(!a_sig || !a_sig_len, NULL);
    
    uint8_t *l_buf = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, DAP_SIG_ECDSA_SIGNATURE_SIZE, NULL);
    
    if (!dap_sig_ecdsa_signature_serialize(NULL, l_buf, a_sig)) {
        log_it(L_ERROR, "Failed to serialize signature");
        DAP_DEL_Z(l_buf);
        return NULL;
    }
    
    *a_sig_len = DAP_SIG_ECDSA_SIGNATURE_SIZE;
    return l_buf;
}

void *dap_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen != DAP_SIG_ECDSA_SIGNATURE_SIZE, NULL);
    
    dap_sig_ecdsa_signature_t *l_sig = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_sig_ecdsa_signature_t, NULL);
    
    if (!dap_sig_ecdsa_signature_parse(NULL, l_sig, a_buf)) {
        log_it(L_ERROR, "Failed to parse signature");
        DAP_DEL_Z(l_sig);
        return NULL;
    }
    return l_sig;
}

void dap_sig_ecdsa_signature_delete(void *a_sig)
{
    if (a_sig) {
        memset_safe(a_sig, 0, sizeof(dap_sig_ecdsa_signature_t));
        DAP_DELETE(a_sig);
    }
}

void dap_sig_ecdsa_private_key_delete(void *a_privkey)
{
    if (a_privkey) {
        memset_safe(a_privkey, 0, DAP_SIG_ECDSA_PRIVKEY_SIZE);
        DAP_DELETE(a_privkey);
    }
}

void dap_sig_ecdsa_public_key_delete(void *a_pubkey)
{
    if (a_pubkey) {
        memset_safe(a_pubkey, 0, sizeof(dap_sig_ecdsa_pubkey_t));
        DAP_DELETE(a_pubkey);
    }
}

void dap_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t *a_key)
{
    if (!a_key) return;
    dap_sig_ecdsa_private_key_delete(a_key->priv_key_data);
    dap_sig_ecdsa_public_key_delete(a_key->pub_key_data);
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
}

void dap_sig_ecdsa_deinit(void)
{
    if (s_initialized) {
        ecdsa_ecmult_gen_deinit();
        s_initialized = false;
    }
}
