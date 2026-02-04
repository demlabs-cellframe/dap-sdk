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
 * @brief ECDSA signature implementation using secp256k1 curve
 */

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_sig_ecdsa.h"
#include "dap_enc_key.h"
#include "rand/dap_rand.h"
#include "dap_hash_sha3.h"

// Currently using 3rdparty secp256k1, will be replaced with native implementation
#include "secp256k1.h"
#include "secp256k1_preallocated.h"

#define LOG_TAG "dap_sig_ecdsa"

// =============================================================================
// Internal types (will be replaced with native ecdsa_* types)
// =============================================================================

// Thread-local context for side-channel protection
static _Thread_local secp256k1_context *s_context = NULL;

// =============================================================================
// Context Management
// =============================================================================

static void s_context_destructor(UNUSED_ARG void *a_context) 
{
    if (s_context) {
        secp256k1_context_destroy(s_context);
        log_it(L_DEBUG, "ECDSA context destroyed @%p", s_context);
        s_context = NULL;
    }
}

static pthread_key_t s_context_key;
static pthread_once_t s_key_once = PTHREAD_ONCE_INIT;

static void s_key_init(void) {
    pthread_key_create(&s_context_key, s_context_destructor);
}

static secp256k1_context *s_context_get(void) 
{
    if (!s_context) {
        s_context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        if (!s_context) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }
        // Register destructor for thread cleanup
        pthread_once(&s_key_once, s_key_init);
        pthread_setspecific(s_context_key, s_context);
        log_it(L_DEBUG, "ECDSA context created @%p", s_context);
    }
    // Randomize on each use for side-channel protection
    uint8_t l_seed[32];
    randombytes(l_seed, sizeof(l_seed));
    if (secp256k1_context_randomize(s_context, l_seed) != 1) {
        log_it(L_ERROR, "Failed to randomize ECDSA context");
        secp256k1_context_destroy(s_context);
        s_context = NULL;
        return NULL;
    }
    return s_context;
}

// =============================================================================
// Public API: Context
// =============================================================================

dap_sig_ecdsa_context_t *dap_sig_ecdsa_context_create(unsigned int a_flags)
{
    (void)a_flags;
    secp256k1_context *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    return (dap_sig_ecdsa_context_t *)l_ctx;
}

void dap_sig_ecdsa_context_destroy(dap_sig_ecdsa_context_t *a_ctx)
{
    if (a_ctx) {
        secp256k1_context_destroy((secp256k1_context *)a_ctx);
    }
}

int dap_sig_ecdsa_context_randomize(dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seed32)
{
    if (!a_ctx) return 0;
    return secp256k1_context_randomize((secp256k1_context *)a_ctx, a_seed32);
}

// =============================================================================
// Public API: Keys
// =============================================================================

int dap_sig_ecdsa_seckey_verify(const dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seckey)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_seckey) return 0;
    return secp256k1_ec_seckey_verify(l_ctx, a_seckey);
}

int dap_sig_ecdsa_pubkey_create(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_seckey)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_pubkey || !a_seckey) return 0;
    return secp256k1_ec_pubkey_create(l_ctx, (secp256k1_pubkey *)a_pubkey, a_seckey);
}

int dap_sig_ecdsa_pubkey_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output,
    size_t *a_outputlen,
    const dap_sig_ecdsa_pubkey_t *a_pubkey,
    unsigned int a_flags)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_output || !a_outputlen || !a_pubkey) return 0;
    
    unsigned int l_flags = (a_flags & DAP_SIG_ECDSA_EC_COMPRESSED) 
        ? SECP256K1_EC_COMPRESSED 
        : SECP256K1_EC_UNCOMPRESSED;
    
    return secp256k1_ec_pubkey_serialize(l_ctx, a_output, a_outputlen, 
                                         (const secp256k1_pubkey *)a_pubkey, l_flags);
}

int dap_sig_ecdsa_pubkey_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_input,
    size_t a_inputlen)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_pubkey || !a_input) return 0;
    return secp256k1_ec_pubkey_parse(l_ctx, (secp256k1_pubkey *)a_pubkey, a_input, a_inputlen);
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
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_sig || !a_msghash32 || !a_seckey) return 0;
    return secp256k1_ecdsa_sign(l_ctx, (secp256k1_ecdsa_signature *)a_sig, 
                                a_msghash32, a_seckey,
                                (secp256k1_nonce_function)a_noncefp, a_ndata);
}

int dap_sig_ecdsa_verify(
    const dap_sig_ecdsa_context_t *a_ctx,
    const dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const dap_sig_ecdsa_pubkey_t *a_pubkey)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_sig || !a_msghash32 || !a_pubkey) return 0;
    return secp256k1_ecdsa_verify(l_ctx, (const secp256k1_ecdsa_signature *)a_sig,
                                  a_msghash32, (const secp256k1_pubkey *)a_pubkey);
}

int dap_sig_ecdsa_signature_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output64,
    const dap_sig_ecdsa_signature_t *a_sig)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_output64 || !a_sig) return 0;
    return secp256k1_ecdsa_signature_serialize_compact(l_ctx, a_output64,
                                                       (const secp256k1_ecdsa_signature *)a_sig);
}

int dap_sig_ecdsa_signature_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_input64)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_sig || !a_input64) return 0;
    return secp256k1_ecdsa_signature_parse_compact(l_ctx, (secp256k1_ecdsa_signature *)a_sig, a_input64);
}

int dap_sig_ecdsa_signature_normalize(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sigout,
    const dap_sig_ecdsa_signature_t *a_sigin)
{
    secp256k1_context *l_ctx = a_ctx ? (secp256k1_context *)a_ctx : s_context_get();
    if (!l_ctx || !a_sigin) return 0;
    return secp256k1_ecdsa_signature_normalize(l_ctx, 
                                               (secp256k1_ecdsa_signature *)a_sigout,
                                               (const secp256k1_ecdsa_signature *)a_sigin);
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
    
    // Allocate key storage
    a_key->priv_key_data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, DAP_SIG_ECDSA_PRIVKEY_SIZE);
    a_key->pub_key_data = DAP_NEW_Z_RET_IF_FAIL(dap_sig_ecdsa_pubkey_t, a_key->priv_key_data);
    
    secp256k1_context *l_ctx = s_context_get();
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to get ECDSA context");
        DAP_DEL_Z(a_key->priv_key_data);
        DAP_DEL_Z(a_key->pub_key_data);
        return;
    }
    
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
            randombytes(a_key->priv_key_data, DAP_SIG_ECDSA_PRIVKEY_SIZE);
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
    s_context_destructor(NULL);
}
