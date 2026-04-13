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
 * @file dap_sig_ecdsa.h
 * @brief ECDSA signature operations on secp256k1 curve
 * @details Public API for ECDSA digital signatures.
 *
 * Elliptic curve secp256k1: y² = x³ + 7 (mod p)
 * Used by Bitcoin, Ethereum and many other cryptocurrencies.
 *
 * Supported operations:
 *   - Key generation (privkey -> pubkey)
 *   - ECDSA signing with RFC6979 deterministic nonce
 *   - ECDSA verification
 *   - Key/signature serialization
 *
 * @note Internal implementation details are hidden in sig_ecdsa/ directory.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants
// =============================================================================

// Key sizes
#define DAP_SIG_ECDSA_PRIVKEY_SIZE          32
#define DAP_SIG_ECDSA_PUBKEY_SIZE           64   // Internal representation
#define DAP_SIG_ECDSA_PUBKEY_COMPRESSED     33   // Serialized compressed
#define DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED   65   // Serialized uncompressed
#define DAP_SIG_ECDSA_SIGNATURE_SIZE        64   // Compact signature (r,s)
#define DAP_SIG_ECDSA_SIGNATURE_DER_MAX     72   // Maximum DER signature size

// Context flags
#define DAP_SIG_ECDSA_CONTEXT_NONE          0

// Serialization flags
#define DAP_SIG_ECDSA_EC_COMPRESSED         (1 << 0)
#define DAP_SIG_ECDSA_EC_UNCOMPRESSED       (1 << 1)

// =============================================================================
// Opaque Types (implementation hidden in sig_ecdsa/)
// =============================================================================

/**
 * @brief Opaque context structure for ECDSA operations
 * @note Contains randomization data for side-channel protection
 */
typedef struct dap_sig_ecdsa_context dap_sig_ecdsa_context_t;

/**
 * @brief Public key structure (opaque, 64 bytes internal)
 * @note Use serialize/parse functions for storage/transmission
 */
typedef struct {
    uint8_t data[DAP_SIG_ECDSA_PUBKEY_SIZE];
} dap_sig_ecdsa_pubkey_t;

/**
 * @brief ECDSA signature structure (opaque, 64 bytes internal)
 * @note Use serialize/parse functions for storage/transmission
 */
typedef struct {
    uint8_t data[DAP_SIG_ECDSA_SIGNATURE_SIZE];
} dap_sig_ecdsa_signature_t;

/**
 * @brief Nonce generation function type (RFC6979 by default)
 */
typedef int (*dap_sig_ecdsa_nonce_func_t)(
    uint8_t *nonce32,
    const uint8_t *msg32,
    const uint8_t *key32,
    const uint8_t *algo16,
    void *data,
    unsigned int attempt
);

// =============================================================================
// Context Functions
// =============================================================================

/**
 * @brief Create a new ECDSA context
 * @param a_flags Context flags (use DAP_SIG_ECDSA_CONTEXT_NONE)
 * @return New context or NULL on error
 * @note Call dap_sig_ecdsa_context_randomize() for side-channel protection
 */
dap_sig_ecdsa_context_t *dap_sig_ecdsa_context_create(unsigned int a_flags);

/**
 * @brief Destroy an ECDSA context
 * @param a_ctx Context to destroy
 */
void dap_sig_ecdsa_context_destroy(dap_sig_ecdsa_context_t *a_ctx);

/**
 * @brief Randomize context for side-channel protection
 * @param a_ctx Context to randomize
 * @param a_seed32 32-byte random seed (NULL to reset)
 * @return 1 on success, 0 on error
 */
int dap_sig_ecdsa_context_randomize(dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seed32);

// =============================================================================
// Key Functions
// =============================================================================

/**
 * @brief Verify a secret key is valid (0 < key < curve order)
 * @param a_ctx Context (can be NULL)
 * @param a_seckey 32-byte secret key
 * @return 1 if valid, 0 if invalid
 */
int dap_sig_ecdsa_seckey_verify(const dap_sig_ecdsa_context_t *a_ctx, const uint8_t *a_seckey);

/**
 * @brief Compute public key from secret key
 * @param a_ctx Context
 * @param a_pubkey Output public key
 * @param a_seckey 32-byte secret key
 * @return 1 on success, 0 on error
 */
int dap_sig_ecdsa_pubkey_create(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_seckey
);

/**
 * @brief Serialize a public key
 * @param a_ctx Context
 * @param a_output Output buffer
 * @param a_outputlen In: buffer size, Out: written size
 * @param a_pubkey Public key to serialize
 * @param a_flags COMPRESSED or UNCOMPRESSED
 * @return 1 on success
 */
int dap_sig_ecdsa_pubkey_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output,
    size_t *a_outputlen,
    const dap_sig_ecdsa_pubkey_t *a_pubkey,
    unsigned int a_flags
);

/**
 * @brief Parse a serialized public key
 * @param a_ctx Context
 * @param a_pubkey Output public key
 * @param a_input Serialized key (33 or 65 bytes)
 * @param a_inputlen Input length
 * @return 1 on success, 0 on error
 */
int dap_sig_ecdsa_pubkey_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_pubkey_t *a_pubkey,
    const uint8_t *a_input,
    size_t a_inputlen
);

// =============================================================================
// Signature Functions
// =============================================================================

/**
 * @brief Create an ECDSA signature
 * @param a_ctx Context
 * @param a_sig Output signature
 * @param a_msghash32 32-byte message hash
 * @param a_seckey 32-byte secret key
 * @param a_noncefp Nonce function (NULL for RFC6979)
 * @param a_ndata Nonce function data
 * @return 1 on success, 0 on error
 */
int dap_sig_ecdsa_sign(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const uint8_t *a_seckey,
    dap_sig_ecdsa_nonce_func_t a_noncefp,
    const void *a_ndata
);

/**
 * @brief Verify an ECDSA signature
 * @param a_ctx Context
 * @param a_sig Signature to verify
 * @param a_msghash32 32-byte message hash
 * @param a_pubkey Public key
 * @return 1 if valid, 0 if invalid
 */
int dap_sig_ecdsa_verify(
    const dap_sig_ecdsa_context_t *a_ctx,
    const dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const dap_sig_ecdsa_pubkey_t *a_pubkey
);

/**
 * @brief Serialize signature in compact format (64 bytes)
 * @param a_ctx Context
 * @param a_output64 Output buffer (64 bytes)
 * @param a_sig Signature
 * @return 1 on success
 */
int dap_sig_ecdsa_signature_serialize(
    const dap_sig_ecdsa_context_t *a_ctx,
    uint8_t *a_output64,
    const dap_sig_ecdsa_signature_t *a_sig
);

/**
 * @brief Parse signature from compact format
 * @param a_ctx Context
 * @param a_sig Output signature
 * @param a_input64 Input buffer (64 bytes)
 * @return 1 on success, 0 on error
 */
int dap_sig_ecdsa_signature_parse(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sig,
    const uint8_t *a_input64
);

/**
 * @brief Normalize signature to low-S form
 * @param a_ctx Context
 * @param a_sigout Output signature (can be same as input)
 * @param a_sigin Input signature
 * @return 1 if was already normalized, 0 if was changed
 */
int dap_sig_ecdsa_signature_normalize(
    const dap_sig_ecdsa_context_t *a_ctx,
    dap_sig_ecdsa_signature_t *a_sigout,
    const dap_sig_ecdsa_signature_t *a_sigin
);

// =============================================================================
// Default nonce function (RFC6979)
// =============================================================================

/**
 * @brief RFC6979 deterministic nonce function
 */
extern const dap_sig_ecdsa_nonce_func_t dap_sig_ecdsa_nonce_rfc6979;

/**
 * @brief Default nonce function (currently RFC6979)
 */
extern const dap_sig_ecdsa_nonce_func_t dap_sig_ecdsa_nonce_default;

// =============================================================================
// Integration with dap_enc_key system
// =============================================================================

#include "dap_enc_key.h"

/**
 * @brief Initialize key structure for ECDSA
 */
void dap_sig_ecdsa_key_new(dap_enc_key_t *a_key);

/**
 * @brief Generate new ECDSA key pair
 */
void dap_sig_ecdsa_key_new_generate(
    dap_enc_key_t *a_key,
    const void *a_kex_buf,
    size_t a_kex_size,
    const void *a_seed,
    size_t a_seed_size,
    size_t a_key_size
);

/**
 * @brief Sign message using dap_enc_key
 */
int dap_sig_ecdsa_get_sign(
    struct dap_enc_key *a_key,
    const void *a_msg,
    const size_t a_msg_size,
    void *a_sig,
    const size_t a_sig_size
);

/**
 * @brief Verify signature using dap_enc_key
 */
int dap_sig_ecdsa_verify_sign(
    struct dap_enc_key *a_key,
    const void *a_msg,
    const size_t a_msg_size,
    void *a_sig,
    const size_t a_sig_size
);

/**
 * @brief Serialize public key
 */
uint8_t *dap_sig_ecdsa_write_public_key(const void *a_pubkey, size_t *a_buflen_out);

/**
 * @brief Parse public key
 */
void *dap_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen);

/**
 * @brief Serialize signature
 */
uint8_t *dap_sig_ecdsa_write_signature(const void *a_sig, size_t *a_sig_len);

/**
 * @brief Parse signature
 */
void *dap_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen);

/**
 * @brief Delete signature
 */
void dap_sig_ecdsa_signature_delete(void *a_sig);

/**
 * @brief Delete private key (secure wipe)
 */
void dap_sig_ecdsa_private_key_delete(void *a_privkey);

/**
 * @brief Delete public key
 */
void dap_sig_ecdsa_public_key_delete(void *a_pubkey);

/**
 * @brief Delete both keys from dap_enc_key
 */
void dap_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t *a_key);

/**
 * @brief Deinitialize ECDSA subsystem
 */
void dap_sig_ecdsa_deinit(void);

// =============================================================================
// Size helpers
// =============================================================================

DAP_STATIC_INLINE uint64_t dap_sig_ecdsa_ser_key_size(UNUSED_ARG const void *a_in) {
    return DAP_SIG_ECDSA_PRIVKEY_SIZE;
}

DAP_STATIC_INLINE uint64_t dap_sig_ecdsa_ser_pkey_size(UNUSED_ARG const void *a_in) {
    return DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED;
}

DAP_STATIC_INLINE uint64_t dap_sig_ecdsa_deser_key_size(UNUSED_ARG const void *a_in) {
    return DAP_SIG_ECDSA_PRIVKEY_SIZE;
}

DAP_STATIC_INLINE uint64_t dap_sig_ecdsa_deser_pkey_size(UNUSED_ARG const void *a_in) {
    return DAP_SIG_ECDSA_PUBKEY_SIZE;
}

DAP_STATIC_INLINE uint64_t dap_sig_ecdsa_signature_size(UNUSED_ARG const void *a_in) {
    return DAP_SIG_ECDSA_SIGNATURE_SIZE;
}

#ifdef __cplusplus
}
#endif
