/*
 * Authors:
 * Dmitriy A. Gearasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_enc_base64.h"
#include "dap_hash_sha3.h"
#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "chipmunk_hots.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "dap_rand.h"
#include "dap_memwipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>

#define LOG_TAG "chipmunk"

static bool s_debug_more = false;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define CHIPMUNK_ETA 2  // Error distribution parameter η

static volatile int g_initialized = 0;
static pthread_once_t s_chipmunk_once = PTHREAD_ONCE_INIT;
static int s_chipmunk_init_result = 0;

static void secure_clean(void *data, size_t size);

/* CR-D3 helpers — forward declared so chipmunk_keypair and chipmunk_sign,
 * which both need the per-leaf HOTS derivation, can call them regardless of
 * definition order further down this file.
 *
 * CR-D15.B: the derivation helpers are also exposed via chipmunk_internal.h
 * so that chipmunk_hypertree.c uses EXACTLY the same secret→pk pipeline as
 * chipmunk_sign — otherwise the root committed to by keygen would not match
 * the leaf that verify re-derives. */
#include "chipmunk_internal.h"

static inline int s_chipmunk_derive_hots_leaf_secret(const uint8_t a_key_seed[32],
                                                     uint32_t a_leaf_index,
                                                     chipmunk_hots_sk_t *a_hots_sk_out)
{
    return dap_chipmunk_derive_hots_leaf_secret_internal(a_key_seed, a_leaf_index, a_hots_sk_out);
}

static inline void s_chipmunk_compute_hots_pk(const chipmunk_hots_params_t *a_params,
                                              const chipmunk_hots_sk_t *a_hots_sk,
                                              chipmunk_hots_pk_t *a_hots_pk_out)
{
    dap_chipmunk_compute_hots_pk_internal(a_params, a_hots_sk, a_hots_pk_out);
}

static void s_chipmunk_init_impl(void)
{
    if (dap_chipmunk_hash_init() != 0) {
        log_it(L_ERROR, "Failed to initialize chipmunk hash functions");
        s_chipmunk_init_result = CHIPMUNK_ERROR_INIT_FAILED;
        return;
    }
    g_initialized = 1;
    s_chipmunk_init_result = CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_init(void) {
    pthread_once(&s_chipmunk_once, s_chipmunk_init_impl);
    return s_chipmunk_init_result;
}

/**
 * @brief Generate a Chipmunk keypair
 * 
 * @param[out] a_public_key Public key buffer
 * @param[in] a_public_key_size Public key buffer size
 * @param[out] a_private_key Private key buffer
 * @param[in] a_private_key_size Private key buffer size
 * @return int CHIPMUNK_ERROR_SUCCESS if successful, error code otherwise
 */
int chipmunk_keypair(uint8_t *a_public_key, size_t a_public_key_size,
                    uint8_t *a_private_key, size_t a_private_key_size) {
    chipmunk_private_key_t *l_sk = NULL;
    chipmunk_public_key_t  *l_pk = NULL;
    uint8_t                *l_pk_bytes = NULL;
    chipmunk_hots_params_t *l_hots_params = NULL;
    chipmunk_hots_pk_t     *l_hots_pk = NULL;
    chipmunk_hots_sk_t     *l_hots_sk = NULL;
    uint8_t l_key_seed[32];
    uint8_t l_rho_seed[32];
    uint8_t l_rho_source[36];
    dap_hash_sha3_256_t l_rho_hash;
    int l_result = CHIPMUNK_ERROR_SUCCESS;

    memset(l_key_seed, 0, sizeof(l_key_seed));
    memset(l_rho_seed, 0, sizeof(l_rho_seed));

    debug_if(s_debug_more, L_DEBUG, "chipmunk_keypair: Starting HOTS key generation");

    if (!a_public_key || !a_private_key) {
        log_it(L_ERROR, "NULL key buffers in chipmunk_keypair");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (a_public_key_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "Public key size mismatch! Expected %d, got %zu",
               CHIPMUNK_PUBLIC_KEY_SIZE, a_public_key_size);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    if (a_private_key_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Private key size mismatch! Expected %d, got %zu",
               CHIPMUNK_PRIVATE_KEY_SIZE, a_private_key_size);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }

    // HOTS structures total ~52KB; allocate on the heap to avoid blowing the stack.
    l_sk          = DAP_NEW_Z(chipmunk_private_key_t);
    l_pk          = DAP_NEW_Z(chipmunk_public_key_t);
    l_pk_bytes    = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_PUBLIC_KEY_SIZE);
    l_hots_params = DAP_NEW_Z(chipmunk_hots_params_t);
    l_hots_pk     = DAP_NEW_Z(chipmunk_hots_pk_t);
    l_hots_sk     = DAP_NEW_Z(chipmunk_hots_sk_t);
    if (!l_sk || !l_pk || !l_pk_bytes || !l_hots_params || !l_hots_pk || !l_hots_sk) {
        log_it(L_ERROR, "Failed to allocate memory for key structures");
        l_result = CHIPMUNK_ERROR_MEMORY;
        goto cleanup;
    }

    // CR-D4 fix (Round-3): seed master entropy from /dev/urandom via dap_random_bytes.
    // Previously used `time(NULL) + static counter`, which exposed key generation to
    // process-fork / fast-clock / multi-machine collisions and is predictable enough
    // to enumerate the seed space.  Cryptographic keypair generation MUST use the OS RNG.
    if (dap_random_bytes(l_key_seed, sizeof(l_key_seed)) != 0) {
        log_it(L_ERROR, "dap_random_bytes failed while drawing %zu bytes for Chipmunk key seed",
               sizeof(l_key_seed));
        l_result = CHIPMUNK_ERROR_INIT_FAILED;
        goto cleanup;
    }

    // rho (public-matrix seed) is derived deterministically from the secret key seed
    // via SHA3-256 with a fixed domain separator: this lets any verifier re-sample
    // the public matrix A from rho_seed alone.
    memcpy(l_rho_source, l_key_seed, 32);
    const uint32_t l_rho_nonce = 0xDEADBEEFu;
    memcpy(l_rho_source + 32, &l_rho_nonce, 4);
    dap_hash_sha3_256(l_rho_source, 36, &l_rho_hash);
    memcpy(l_rho_seed, &l_rho_hash, 32);
    secure_clean(l_rho_source, sizeof(l_rho_source));
    secure_clean(&l_rho_hash, sizeof(l_rho_hash));

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params->a[i].coeffs, l_rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate public matrix polynomial A[%d]", i);
            l_result = CHIPMUNK_ERROR_HASH_FAILED;
            goto cleanup;
        }
        chipmunk_poly_ntt(&l_hots_params->a[i]);
    }

    /* CR-D3: derive the leaf-0 HOTS secret with the same domain-separated
     * derivation that chipmunk_sign uses, then compute the matching public
     * key from (s0, s1, A).  This invariant — keygen and sign agree on the
     * HOTS secret for every leaf_index — is what makes the stored public
     * key verify against signatures produced later. */
    if (s_chipmunk_derive_hots_leaf_secret(l_key_seed, 0u, l_hots_sk) != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to derive HOTS leaf-0 secret");
        l_result = CHIPMUNK_ERROR_INTERNAL;
        goto cleanup;
    }
    s_chipmunk_compute_hots_pk(l_hots_params, l_hots_sk, l_hots_pk);

    l_sk->leaf_index = 0u;
    memcpy(l_sk->key_seed, l_key_seed, 32);
    memcpy(l_pk->rho_seed, l_rho_seed, 32);
    memcpy(&l_pk->v0, &l_hots_pk->v0, sizeof(chipmunk_poly_t));
    memcpy(&l_pk->v1, &l_hots_pk->v1, sizeof(chipmunk_poly_t));
    memcpy(&l_sk->pk, l_pk, sizeof(*l_pk));

    l_result = chipmunk_public_key_to_bytes(l_pk_bytes, l_pk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key for tr commitment");
        goto cleanup;
    }

    l_result = dap_chipmunk_hash_sha3_384(l_sk->tr, l_pk_bytes, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to compute public key tr hash");
        goto cleanup;
    }

    l_result = chipmunk_private_key_to_bytes(a_private_key, l_sk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize private key");
        goto cleanup;
    }

    l_result = chipmunk_public_key_to_bytes(a_public_key, l_pk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        goto cleanup;
    }

    debug_if(s_debug_more, L_DEBUG, "Successfully generated Chipmunk HOTS keypair");

cleanup:
    // CR-D13 fix (Round-3): every buffer that ever touched the secret seed must be
    // wiped before release, on the success path AND on every failure path.
    if (l_hots_sk) secure_clean(l_hots_sk, sizeof(*l_hots_sk));
    if (l_sk)      secure_clean(l_sk,      sizeof(*l_sk));
    secure_clean(l_key_seed, sizeof(l_key_seed));
    secure_clean(l_rho_seed, sizeof(l_rho_seed));

    DAP_DEL_Z(l_sk);
    DAP_DEL_Z(l_pk);
    DAP_DEL_Z(l_pk_bytes);
    DAP_DEL_Z(l_hots_params);
    DAP_DEL_Z(l_hots_pk);
    DAP_DEL_Z(l_hots_sk);
    return l_result;
}

/**
 * @brief Sign a message
 * 
 * @param[in] a_private_key Private key buffer
 * @param[in] a_message Message to sign
 * @param[in] a_message_len Message length
 * @param[out] a_signature Output signature buffer
 * @return Returns 0 on success, negative on error
 */
/* CR-D3: domain-separation tag for per-leaf HOTS key derivation.  Binding
 * the master key_seed plus the monotonic leaf_index under a fixed label
 * guarantees that every leaf_index produces an independent (s0, s1); thus
 * two signatures taken at different counter values share no linear relation
 * that could be exploited for HOTS key recovery. */
static const uint8_t s_chipmunk_leaf_ds[16] = {
    'C','H','I','P','M','U','N','K','-','L','E','A','F','-','v','1'
};

static pthread_mutex_t s_chipmunk_sign_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Derive a HOTS secret (s0, s1) for the specified leaf_index.
 *
 * Both chipmunk_keypair (for leaf_index=0 during keygen) and chipmunk_sign
 * (for the current leaf_index) call this helper, so that the public key
 * computed at keygen time is guaranteed to match the secret key that is
 * re-derived at signing time.  The derivation is:
 *
 *     leaf_seed = SHA3-256( "CHIPMUNK-LEAF-v1" || key_seed || BE32(leaf_index) )
 *     s0[i]     = poly_uniform_mod_p( leaf_seed || BE32(i),               phi         )
 *     s1[i]     = poly_uniform_mod_p( leaf_seed || BE32(GAMMA + i),       phi*alpha_H )
 *
 * The domain label prevents any other SHA3-256 use of key_seed from
 * colliding with this derivation.  The leaf_index is included under the
 * hash so distinct leaves produce independent (s0, s1) pairs — the
 * invariant that, together with the exhaustion check in chipmunk_sign,
 * closes the classical HOTS key-recovery attack.
 */
int dap_chipmunk_derive_hots_leaf_secret_internal(const uint8_t a_key_seed[32],
                                                  uint32_t a_leaf_index,
                                                  chipmunk_hots_sk_t *a_hots_sk_out)
{
    if (!a_key_seed || !a_hots_sk_out) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    uint8_t l_leaf_source[sizeof(s_chipmunk_leaf_ds) + 32 + 4];
    uint8_t l_leaf_seed[32];
    dap_hash_sha3_256_t l_hash_out;

    memcpy(l_leaf_source, s_chipmunk_leaf_ds, sizeof(s_chipmunk_leaf_ds));
    memcpy(l_leaf_source + sizeof(s_chipmunk_leaf_ds), a_key_seed, 32);
    l_leaf_source[sizeof(s_chipmunk_leaf_ds) + 32 + 0] = (uint8_t)((a_leaf_index >> 24) & 0xFFu);
    l_leaf_source[sizeof(s_chipmunk_leaf_ds) + 32 + 1] = (uint8_t)((a_leaf_index >> 16) & 0xFFu);
    l_leaf_source[sizeof(s_chipmunk_leaf_ds) + 32 + 2] = (uint8_t)((a_leaf_index >> 8)  & 0xFFu);
    l_leaf_source[sizeof(s_chipmunk_leaf_ds) + 32 + 3] = (uint8_t)( a_leaf_index        & 0xFFu);
    dap_hash_sha3_256(l_leaf_source, sizeof(l_leaf_source), &l_hash_out);
    memcpy(l_leaf_seed, &l_hash_out, 32);

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        uint8_t l_s_seed[36];
        memcpy(l_s_seed, l_leaf_seed, 32);

        uint32_t l_s0_nonce = (uint32_t)i;
        memcpy(l_s_seed + 32, &l_s0_nonce, 4);
        chipmunk_poly_uniform_mod_p(&a_hots_sk_out->s0[i], l_s_seed, CHIPMUNK_PHI);
        chipmunk_ntt(a_hots_sk_out->s0[i].coeffs);

        uint32_t l_s1_nonce = (uint32_t)(CHIPMUNK_GAMMA + i);
        memcpy(l_s_seed + 32, &l_s1_nonce, 4);
        chipmunk_poly_uniform_mod_p(&a_hots_sk_out->s1[i], l_s_seed, CHIPMUNK_PHI_ALPHA_H);
        chipmunk_ntt(a_hots_sk_out->s1[i].coeffs);

        dap_memwipe(l_s_seed, sizeof(l_s_seed));
    }

    dap_memwipe(l_leaf_source, sizeof(l_leaf_source));
    dap_memwipe(l_leaf_seed, sizeof(l_leaf_seed));
    dap_memwipe(&l_hash_out, sizeof(l_hash_out));
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Compute HOTS public key v0, v1 from a fully-populated HOTS secret
 *        and the public matrix A in NTT domain.
 *
 * Replicates the identity used by chipmunk_hots_keygen:
 *     v0 = INVNTT( Σ_i  A[i] * s0[i] )   (stored in time domain)
 *     v1 = INVNTT( Σ_i  A[i] * s1[i] )
 * but with the caller-provided HOTS secret so we can plug in the CR-D3
 * domain-separated derivation.
 */
void dap_chipmunk_compute_hots_pk_internal(const chipmunk_hots_params_t *a_params,
                                           const chipmunk_hots_sk_t *a_hots_sk,
                                           chipmunk_hots_pk_t *a_hots_pk_out)
{
    chipmunk_poly_t l_v0_time_sum;
    chipmunk_poly_t l_v1_time_sum;
    memset(&l_v0_time_sum, 0, sizeof(l_v0_time_sum));
    memset(&l_v1_time_sum, 0, sizeof(l_v1_time_sum));

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t l_term_v0_ntt;
        chipmunk_poly_mul_ntt(&l_term_v0_ntt, &a_params->a[i], &a_hots_sk->s0[i]);

        chipmunk_poly_t l_term_v1_ntt;
        chipmunk_poly_mul_ntt(&l_term_v1_ntt, &a_params->a[i], &a_hots_sk->s1[i]);

        chipmunk_poly_t l_term_v0_time = l_term_v0_ntt;
        chipmunk_poly_t l_term_v1_time = l_term_v1_ntt;
        chipmunk_invntt(l_term_v0_time.coeffs);
        chipmunk_invntt(l_term_v1_time.coeffs);

        if (i == 0) {
            l_v0_time_sum = l_term_v0_time;
            l_v1_time_sum = l_term_v1_time;
        } else {
            chipmunk_poly_add(&l_v0_time_sum, &l_v0_time_sum, &l_term_v0_time);
            chipmunk_poly_add(&l_v1_time_sum, &l_v1_time_sum, &l_term_v1_time);
        }
    }

    a_hots_pk_out->v0 = l_v0_time_sum;
    a_hots_pk_out->v1 = l_v1_time_sum;
}

int chipmunk_sign(uint8_t *a_private_key, const uint8_t *a_message,
                  size_t a_message_len, uint8_t *a_signature) {
    chipmunk_private_key_t l_sk = {0};
    chipmunk_hots_params_t l_hots_params = {0};
    chipmunk_hots_sk_t l_hots_sk = {0};
    chipmunk_hots_signature_t l_hots_sig = {0};
    chipmunk_signature_t l_sig = {0};
    bool l_locked = false;
    int l_result = CHIPMUNK_ERROR_SUCCESS;

    debug_if(s_debug_more, L_DEBUG, "Starting Chipmunk HOTS signature generation (CR-D3 strict one-time)");

    if (!a_private_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_sign");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for signing");
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }

    /* CR-D3: serialize the entire check-and-increment of leaf_index so that
     * concurrent chipmunk_sign callers against the same private-key buffer
     * cannot both observe leaf_index=0 and both consume it (which would
     * immediately expose the HOTS secret key). */
    if (pthread_mutex_lock(&s_chipmunk_sign_mutex) != 0) {
        log_it(L_ERROR, "Failed to acquire chipmunk sign mutex");
        return CHIPMUNK_ERROR_INTERNAL;
    }
    l_locked = true;

    if (chipmunk_private_key_from_bytes(&l_sk, a_private_key) != 0) {
        log_it(L_ERROR, "Failed to parse private key");
        l_result = CHIPMUNK_ERROR_INVALID_PARAM;
        goto sign_cleanup;
    }

    /* CR-D3 fail-fast: a Chipmunk/HOTS private key is strictly single-use
     * until CR-D15 introduces the Merkle-tree authentication path.  Any
     * attempt to sign a second message under the same key would leak s0
     * and s1; refuse loudly so the caller rotates to a fresh keypair. */
    if (l_sk.leaf_index >= CHIPMUNK_MAX_SIGNATURES) {
        log_it(L_ERROR,
               "CR-D3: Chipmunk private key exhausted (leaf_index=%u, max=%u). "
               "Refusing to sign — this key has already produced a signature "
               "and reusing it would leak the HOTS secret. Rotate to a fresh keypair.",
               l_sk.leaf_index, CHIPMUNK_MAX_SIGNATURES);
        l_result = CHIPMUNK_ERROR_KEY_EXHAUSTED;
        goto sign_cleanup;
    }

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params.a[i].coeffs, l_sk.pk.rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate public matrix polynomial A[%d]", i);
            l_result = CHIPMUNK_ERROR_HASH_FAILED;
            goto sign_cleanup;
        }
        chipmunk_poly_ntt(&l_hots_params.a[i]);
    }

    /* CR-D3: derive (s0, s1) for the current leaf_index using the
     * domain-separated SHA3 derivation shared with chipmunk_keypair so that
     * the stored public key necessarily matches this HOTS secret. */
    const uint32_t l_leaf_index = l_sk.leaf_index;
    l_result = s_chipmunk_derive_hots_leaf_secret(l_sk.key_seed, l_leaf_index, &l_hots_sk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to derive HOTS leaf secret for leaf_index=%u", l_leaf_index);
        goto sign_cleanup;
    }

    l_result = chipmunk_hots_sign(&l_hots_sk, a_message, a_message_len, &l_hots_sig);
    if (l_result != 0) {
        log_it(L_ERROR, "HOTS signature failed with error %d", l_result);
        goto sign_cleanup;
    }

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        memcpy(&l_sig.sigma[i], &l_hots_sig.sigma[i], sizeof(chipmunk_poly_t));
    }

    if (chipmunk_signature_to_bytes(a_signature, &l_sig) != 0) {
        log_it(L_ERROR, "Failed to serialize signature");
        l_result = CHIPMUNK_ERROR_INTERNAL;
        goto sign_cleanup;
    }

    /* CR-D3: advance and persist the counter only after the signature has
     * been fully produced and serialised.  Any earlier failure must leave
     * leaf_index untouched so the caller can retry (on transient errors)
     * or rotate the key (on hard errors) without accidentally consuming
     * the one-time slot. */
    l_sk.leaf_index = l_leaf_index + 1u;
    if (chipmunk_private_key_to_bytes(a_private_key, &l_sk) != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to persist updated leaf_index counter in private key buffer");
        l_result = CHIPMUNK_ERROR_INTERNAL;
        goto sign_cleanup;
    }

    debug_if(s_debug_more, L_DEBUG,
             "HOTS signature successfully generated; leaf_index advanced to %u",
             l_sk.leaf_index);
    l_result = CHIPMUNK_ERROR_SUCCESS;

sign_cleanup:
    /* CR-D13 fix: wipe every transient secret derived from the master key
     * seed; otherwise pieces of the HOTS secret key (which directly
     * recovers the master key under HOTS reuse) survive in stack memory. */
    secure_clean(&l_sk, sizeof(l_sk));
    secure_clean(&l_hots_sk, sizeof(l_hots_sk));
    secure_clean(&l_hots_sig, sizeof(l_hots_sig));
    if (l_locked) {
        pthread_mutex_unlock(&s_chipmunk_sign_mutex);
    }
    return l_result;
}

/**
 * @brief Verify a signature
 * 
 * @param[in] a_public_key Public key buffer
 * @param[in] a_message Message that was signed
 * @param[in] a_message_len Message length
 * @param[in] a_signature Signature to verify
 * @return Returns 0 if signature is valid, negative on error
 */
int chipmunk_verify(const uint8_t *a_public_key, const uint8_t *a_message, 
                    size_t a_message_len, const uint8_t *a_signature) {
    debug_if(s_debug_more, L_DEBUG, "Starting HOTS signature verification");
    
    debug_if(s_debug_more, L_INFO, "chipmunk_verify: pub_key=%p, message=%p, msg_len=%zu, signature=%p",
           a_public_key, a_message, a_message_len, a_signature);
    
    if (s_debug_more && a_public_key) {
        dump_it(a_public_key, "chipmunk_verify INPUT PUBLIC KEY", CHIPMUNK_PUBLIC_KEY_SIZE);
    }
    if (s_debug_more && a_message && a_message_len > 0) {
        dump_it(a_message, "chipmunk_verify INPUT MESSAGE", a_message_len);
    }
    if (s_debug_more && a_signature) {
        dump_it(a_signature, "chipmunk_verify INPUT SIGNATURE", CHIPMUNK_SIGNATURE_SIZE);
    }
    
    if (!a_public_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_verify");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Проверка на максимальный размер сообщения
    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for verification");
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    
    // Парсим публичный ключ
    chipmunk_public_key_t l_pk = {0};
    int l_pk_result = chipmunk_public_key_from_bytes(&l_pk, a_public_key);
    if (l_pk_result != 0) {
        log_it(L_ERROR, "Failed to parse public key, error code: %d", l_pk_result);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    debug_if(s_debug_more, L_INFO, "chipmunk_verify: public key parsed successfully");
    
    // Парсим подпись
    chipmunk_signature_t l_sig = {0};
    int l_sig_result = chipmunk_signature_from_bytes(&l_sig, a_signature);
    if (l_sig_result != 0) {
        log_it(L_ERROR, "Failed to parse signature, error code: %d", l_sig_result);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    debug_if(s_debug_more, L_INFO, "chipmunk_verify: signature parsed successfully");
    
    // Генерируем HOTS параметры из rho_seed
    chipmunk_hots_params_t l_hots_params = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params.a[i].coeffs, l_pk.rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate polynomial A[%d]", i);
            return CHIPMUNK_ERROR_HASH_FAILED;
        }
        // Преобразуем в NTT домен
        chipmunk_poly_ntt(&l_hots_params.a[i]);
    }
    
    // Создаем HOTS публичный ключ
    chipmunk_hots_pk_t l_hots_pk = {0};
    memcpy(&l_hots_pk.v0, &l_pk.v0, sizeof(chipmunk_poly_t));
    memcpy(&l_hots_pk.v1, &l_pk.v1, sizeof(chipmunk_poly_t));
    
    // Создаем HOTS подпись
    chipmunk_hots_signature_t l_hots_sig = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        memcpy(&l_hots_sig.sigma[i], &l_sig.sigma[i], sizeof(chipmunk_poly_t));
    }
    
    // Используем HOTS функцию верификации
    debug_if(s_debug_more, L_INFO, "chipmunk_verify: calling chipmunk_hots_verify with msg_len=%zu", a_message_len);
    int l_result = chipmunk_hots_verify(&l_hots_pk, a_message, a_message_len, 
                                        &l_hots_sig, &l_hots_params);
    
    debug_if(s_debug_more, L_INFO, "chipmunk_verify: chipmunk_hots_verify returned %d", l_result);
    
    if (l_result != 0) {  // Стандартное C соглашение: 0 для успеха, отрицательное для ошибки
        debug_if(s_debug_more, L_DEBUG, "HOTS signature verification failed: %d", l_result);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }
    
    debug_if(s_debug_more, L_DEBUG, "HOTS signature verified successfully");
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Serialize public key to bytes
 */
int chipmunk_public_key_to_bytes(uint8_t *a_output, const chipmunk_public_key_t *a_key) {
    if (!a_output || !a_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_public_key_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    size_t l_expected_size = 32 + (CHIPMUNK_N * 4 * 2); // rho_seed + v0 + v1
    
    debug_if(s_debug_more, L_INFO, "=== chipmunk_public_key_to_bytes DEBUG ===");
    debug_if(s_debug_more, L_INFO, "Expected size: %zu (should be %d)", l_expected_size, CHIPMUNK_PUBLIC_KEY_SIZE);
    debug_if(s_debug_more, L_INFO, "Output buffer: %p", a_output);
    debug_if(s_debug_more, L_INFO, "CHIPMUNK_N = %d", CHIPMUNK_N);
    
    // Write rho_seed (32 bytes)
    debug_if(s_debug_more, L_INFO, "Writing rho_seed at offset %zu", l_offset);
    memcpy(a_output + l_offset, a_key->rho_seed, 32);
    l_offset += 32;
    
    // Write v0 polynomial (CHIPMUNK_N * 4 bytes)
    debug_if(s_debug_more, L_INFO, "Writing v0 polynomial at offset %zu (size %d)", l_offset, CHIPMUNK_N * 4);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Apply same modulo operation as in deserialization for consistency
        int32_t l_coeff = ((a_key->v0.coeffs[i] % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
        a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
        a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
        a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
        a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
        l_offset += 4;
    }
    
    // Write v1 polynomial (CHIPMUNK_N * 4 bytes)
    debug_if(s_debug_more, L_INFO, "Writing v1 polynomial at offset %zu (size %d)", l_offset, CHIPMUNK_N * 4);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Apply same modulo operation as in deserialization for consistency
        int32_t l_coeff = ((a_key->v1.coeffs[i] % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
        a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
        a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
        a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
        a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
        l_offset += 4;
    }
    
    debug_if(s_debug_more, L_INFO, "Total bytes written: %zu", l_offset);
    debug_if(s_debug_more, L_INFO, "===========================================");
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Serialize private key to bytes
 */
int chipmunk_private_key_to_bytes(uint8_t *a_output, const chipmunk_private_key_t *a_key) {
    if (!a_output || !a_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_private_key_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    size_t l_offset = 0;
    size_t l_total_size = CHIPMUNK_LEAF_INDEX_SIZE + 32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE;

    debug_if(s_debug_more, L_INFO, "=== chipmunk_private_key_to_bytes DEBUG ===");
    debug_if(s_debug_more, L_INFO, "Expected total size: %zu", l_total_size);
    debug_if(s_debug_more, L_INFO, "Output buffer pointer: %p", a_output);

    /* CR-D3: write monotonic leaf_index big-endian as the very first field so
     * that the on-disk ordering matches the natural ordering of the counter. */
    a_output[l_offset + 0] = (uint8_t)((a_key->leaf_index >> 24) & 0xFFu);
    a_output[l_offset + 1] = (uint8_t)((a_key->leaf_index >> 16) & 0xFFu);
    a_output[l_offset + 2] = (uint8_t)((a_key->leaf_index >> 8)  & 0xFFu);
    a_output[l_offset + 3] = (uint8_t)( a_key->leaf_index        & 0xFFu);
    l_offset += CHIPMUNK_LEAF_INDEX_SIZE;

    // Write key_seed (32 bytes)
    debug_if(s_debug_more, L_INFO, "Writing key_seed at offset %zu", l_offset);
    memcpy(a_output + l_offset, a_key->key_seed, 32);
    l_offset += 32;

    // Write tr (48 bytes)
    debug_if(s_debug_more, L_INFO, "Writing tr at offset %zu", l_offset);
    memcpy(a_output + l_offset, a_key->tr, 48);
    l_offset += 48;

    // Write public key
    debug_if(s_debug_more, L_INFO, "Writing public key at offset %zu", l_offset);
    debug_if(s_debug_more, L_INFO, "Calling chipmunk_public_key_to_bytes with buffer at %p", a_output + l_offset);
    int result = chipmunk_public_key_to_bytes(a_output + l_offset, &a_key->pk);

    debug_if(s_debug_more, L_INFO, "chipmunk_public_key_to_bytes returned %d", result);
    debug_if(s_debug_more, L_INFO, "===========================================");

    return result;
}

/**
 * @brief Serialize signature to bytes
 */
int chipmunk_signature_to_bytes(uint8_t *a_output, const chipmunk_signature_t *a_sig) {
    if (!a_output || !a_sig) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_signature_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    
    // Write all GAMMA sigma polynomials
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            int32_t l_coeff = a_sig->sigma[i].coeffs[j];
            a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
            a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
            a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
            a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
            l_offset += 4;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize public key from bytes
 */
int chipmunk_public_key_from_bytes(chipmunk_public_key_t *a_key, const uint8_t *a_input) {
    if (!a_key || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_public_key_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Read rho_seed (32 bytes)
    memcpy(a_key->rho_seed, a_input, 32);
    
    // Read v0 polynomial (CHIPMUNK_N * 4 bytes)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // ИСПРАВЛЕНО: читаем как знаковое число для корректной обработки отрицательных коэффициентов
        uint32_t l_raw = ((uint32_t)a_input[32 + i*4]) | 
                        (((uint32_t)a_input[32 + i*4 + 1]) << 8) | 
                        (((uint32_t)a_input[32 + i*4 + 2]) << 16) |
                        (((uint32_t)a_input[32 + i*4 + 3]) << 24);
        
        // Интерпретируем как знаковое число и приводим к диапазону [0, Q-1]
        int32_t l_signed = (int32_t)l_raw;
        a_key->v0.coeffs[i] = ((l_signed % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
    }
    
    // Read v1 polynomial (CHIPMUNK_N * 4 bytes)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // ИСПРАВЛЕНО: читаем как знаковое число для корректной обработки отрицательных коэффициентов
        uint32_t l_raw = ((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4]) | 
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 1]) << 8) | 
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 2]) << 16) |
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 3]) << 24);
        
        // Интерпретируем как знаковое число и приводим к диапазону [0, Q-1]
        int32_t l_signed = (int32_t)l_raw;
        a_key->v1.coeffs[i] = ((l_signed % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize private key from bytes
 */
int chipmunk_private_key_from_bytes(chipmunk_private_key_t *a_key, const uint8_t *a_input) {
    if (!a_key || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_private_key_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    size_t l_offset = 0;

    /* CR-D3: leaf_index is stored big-endian as the very first 4 bytes. */
    a_key->leaf_index = ((uint32_t)a_input[l_offset + 0] << 24) |
                        ((uint32_t)a_input[l_offset + 1] << 16) |
                        ((uint32_t)a_input[l_offset + 2] << 8)  |
                        ((uint32_t)a_input[l_offset + 3]);
    l_offset += CHIPMUNK_LEAF_INDEX_SIZE;

    // Read key_seed (32 bytes)
    memcpy(a_key->key_seed, a_input + l_offset, 32);
    l_offset += 32;

    // Read tr (48 bytes)
    memcpy(a_key->tr, a_input + l_offset, 48);
    l_offset += 48;

    // Read public key
    int l_result = chipmunk_public_key_from_bytes(&a_key->pk, a_input + l_offset);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to deserialize public key part in private key");
        return l_result;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize signature from bytes
 */
int chipmunk_signature_from_bytes(chipmunk_signature_t *a_sig, const uint8_t *a_input) {
    if (!a_sig || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_signature_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    
    // Clear structure before filling
    memset(a_sig, 0, sizeof(chipmunk_signature_t));
    
    // Read all GAMMA sigma polynomials
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            a_sig->sigma[i].coeffs[j] = (int32_t)(
                (uint32_t)a_input[l_offset] | 
                ((uint32_t)a_input[l_offset + 1] << 8) | 
                ((uint32_t)a_input[l_offset + 2] << 16) | 
                ((uint32_t)a_input[l_offset + 3] << 24)
            );
            l_offset += 4;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
} 

/**
 * @brief Securely wipe a buffer, defeating dead-store elimination.
 *
 * CR-D25 fix (Round-3): the previous hand-rolled loop on `volatile uint8_t *`
 * relied on the standard's allowance to eliminate stores to non-volatile
 * memory and offered no compiler-asm barrier.  `dap_memwipe` is the project's
 * canonical secure-erase primitive (uses `explicit_bzero` / `memset_s` /
 * inline asm fence depending on the platform) and must be used everywhere
 * a key, seed, nonce or signature input touches RAM.
 */
static void secure_clean(void *data, size_t size) {
    if (!data || !size) {
        return;
    }
    dap_memwipe(data, size);
}


// Удалена неиспользуемая функция copy_to_hash_buffer

/**
 * @brief Generate a Chipmunk keypair deterministically from seed
 * 
 * @param[in] a_seed 32-byte seed for deterministic key generation
 * @param[out] a_public_key Buffer to store public key
 * @param[in] a_public_key_size Size of public key buffer
 * @param[out] a_private_key Buffer to store private key
 * @param[in] a_private_key_size Size of private key buffer
 * @return int CHIPMUNK_ERROR_SUCCESS if successful, error code otherwise
 */
int chipmunk_keypair_from_seed(const uint8_t a_seed[32],
                               uint8_t *a_public_key, size_t a_public_key_size,
                               uint8_t *a_private_key, size_t a_private_key_size) {
    chipmunk_private_key_t *l_sk = NULL;
    chipmunk_public_key_t  *l_pk = NULL;
    uint8_t                *l_pk_bytes = NULL;
    chipmunk_hots_params_t *l_hots_params = NULL;
    chipmunk_hots_pk_t     *l_hots_pk = NULL;
    chipmunk_hots_sk_t     *l_hots_sk = NULL;
    uint8_t l_key_seed[32];
    uint8_t l_rho_seed[32];
    uint8_t l_rho_source[36];
    dap_hash_sha3_256_t l_rho_hash;
    int l_result = CHIPMUNK_ERROR_SUCCESS;

    memset(l_key_seed, 0, sizeof(l_key_seed));
    memset(l_rho_seed, 0, sizeof(l_rho_seed));

    debug_if(s_debug_more, L_DEBUG, "chipmunk_keypair_from_seed: Starting deterministic key generation");

    if (!a_seed || !a_public_key || !a_private_key) {
        log_it(L_ERROR, "NULL parameters in chipmunk_keypair_from_seed");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (a_public_key_size != CHIPMUNK_PUBLIC_KEY_SIZE ||
        a_private_key_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Invalid key buffer sizes in chipmunk_keypair_from_seed: pub %zu (expected %d), priv %zu (expected %d)",
               a_public_key_size, CHIPMUNK_PUBLIC_KEY_SIZE,
               a_private_key_size, CHIPMUNK_PRIVATE_KEY_SIZE);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }

    l_sk          = DAP_NEW_Z(chipmunk_private_key_t);
    l_pk          = DAP_NEW_Z(chipmunk_public_key_t);
    l_pk_bytes    = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_PUBLIC_KEY_SIZE);
    l_hots_params = DAP_NEW_Z(chipmunk_hots_params_t);
    l_hots_pk     = DAP_NEW_Z(chipmunk_hots_pk_t);
    l_hots_sk     = DAP_NEW_Z(chipmunk_hots_sk_t);
    if (!l_sk || !l_pk || !l_pk_bytes || !l_hots_params || !l_hots_pk || !l_hots_sk) {
        log_it(L_ERROR, "Failed to allocate memory for key structures");
        l_result = CHIPMUNK_ERROR_MEMORY;
        goto cleanup;
    }

    memcpy(l_key_seed, a_seed, 32);

    // Distinct domain separator from chipmunk_keypair so that, given the same
    // raw seed, the deterministic and OS-RNG paths NEVER collapse onto the same
    // public matrix (would otherwise create cross-context replay risks).
    memcpy(l_rho_source, l_key_seed, 32);
    const uint32_t l_rho_nonce = 0x12345678u;
    memcpy(l_rho_source + 32, &l_rho_nonce, 4);
    dap_hash_sha3_256(l_rho_source, 36, &l_rho_hash);
    memcpy(l_rho_seed, &l_rho_hash, 32);
    secure_clean(l_rho_source, sizeof(l_rho_source));
    secure_clean(&l_rho_hash, sizeof(l_rho_hash));

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params->a[i].coeffs, l_rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate public matrix polynomial A[%d]", i);
            l_result = CHIPMUNK_ERROR_HASH_FAILED;
            goto cleanup;
        }
        chipmunk_poly_ntt(&l_hots_params->a[i]);
    }

    /* CR-D3: identical derivation as in chipmunk_keypair — the HOTS secret
     * for leaf-0 is computed via the domain-separated SHA3 helper so that
     * chipmunk_sign (which re-derives the same secret at signing time) can
     * produce signatures that verify against the public key stored here. */
    if (s_chipmunk_derive_hots_leaf_secret(l_key_seed, 0u, l_hots_sk) != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to derive HOTS leaf-0 secret (deterministic path)");
        l_result = CHIPMUNK_ERROR_INTERNAL;
        goto cleanup;
    }
    s_chipmunk_compute_hots_pk(l_hots_params, l_hots_sk, l_hots_pk);

    l_sk->leaf_index = 0u;
    memcpy(l_sk->key_seed, l_key_seed, 32);
    memcpy(l_pk->rho_seed, l_rho_seed, 32);
    memcpy(&l_pk->v0, &l_hots_pk->v0, sizeof(chipmunk_poly_t));
    memcpy(&l_pk->v1, &l_hots_pk->v1, sizeof(chipmunk_poly_t));
    memcpy(&l_sk->pk, l_pk, sizeof(*l_pk));

    l_result = chipmunk_public_key_to_bytes(l_pk_bytes, l_pk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key for tr commitment");
        goto cleanup;
    }

    l_result = dap_chipmunk_hash_sha3_384(l_sk->tr, l_pk_bytes, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to compute public key tr hash");
        goto cleanup;
    }

    l_result = chipmunk_private_key_to_bytes(a_private_key, l_sk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize private key");
        goto cleanup;
    }

    l_result = chipmunk_public_key_to_bytes(a_public_key, l_pk);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        goto cleanup;
    }

    debug_if(s_debug_more, L_DEBUG, "Successfully generated deterministic Chipmunk keypair");

cleanup:
    if (l_hots_sk) secure_clean(l_hots_sk, sizeof(*l_hots_sk));
    if (l_sk)      secure_clean(l_sk,      sizeof(*l_sk));
    secure_clean(l_key_seed, sizeof(l_key_seed));
    secure_clean(l_rho_seed, sizeof(l_rho_seed));

    DAP_DEL_Z(l_sk);
    DAP_DEL_Z(l_pk);
    DAP_DEL_Z(l_pk_bytes);
    DAP_DEL_Z(l_hots_params);
    DAP_DEL_Z(l_hots_pk);
    DAP_DEL_Z(l_hots_sk);
    return l_result;
}




