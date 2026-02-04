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
 * @file dap_secp256k1.h
 * @brief Native DAP implementation of secp256k1 ECDSA
 * @details Elliptic curve secp256k1: y² = x³ + 7 (mod p)
 *
 * Curve parameters:
 *   p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
 *   a = 0, b = 7
 *   G = generator point
 *   n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
 *
 * Supported operations:
 *   - Key generation (privkey -> pubkey)
 *   - ECDSA signing with RFC6979 deterministic nonce
 *   - ECDSA verification
 *   - Key/signature serialization
 *
 * @note Uses native DAP implementation with SIMD optimizations where available.
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
#define DAP_SECP256K1_PRIVKEY_SIZE          32
#define DAP_SECP256K1_PUBKEY_SIZE           64   // Internal representation
#define DAP_SECP256K1_PUBKEY_COMPRESSED     33   // Serialized compressed
#define DAP_SECP256K1_PUBKEY_UNCOMPRESSED   65   // Serialized uncompressed
#define DAP_SECP256K1_SIGNATURE_SIZE        64   // Compact signature (r,s)
#define DAP_SECP256K1_SIGNATURE_DER_MAX     72   // Maximum DER signature size

// Context flags
#define DAP_SECP256K1_CONTEXT_NONE          0

// Serialization flags
#define DAP_SECP256K1_EC_COMPRESSED         (1 << 0)
#define DAP_SECP256K1_EC_UNCOMPRESSED       (1 << 1)

// =============================================================================
// Types
// =============================================================================

/**
 * @brief Opaque context structure for secp256k1 operations
 * @note Contains randomization data for side-channel protection
 */
typedef struct dap_secp256k1_context dap_secp256k1_context_t;

/**
 * @brief Public key structure (internal representation)
 * @note Use serialize/parse functions for storage/transmission
 */
typedef struct {
    uint8_t data[DAP_SECP256K1_PUBKEY_SIZE];
} dap_secp256k1_pubkey_t;

/**
 * @brief ECDSA signature structure (internal representation)
 * @note Use serialize/parse functions for storage/transmission
 */
typedef struct {
    uint8_t data[DAP_SECP256K1_SIGNATURE_SIZE];
} dap_secp256k1_signature_t;

/**
 * @brief SHA256 hash context for internal use
 */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} dap_secp256k1_sha256_t;

/**
 * @brief Nonce generation function type (RFC6979 by default)
 */
typedef int (*dap_secp256k1_nonce_function_t)(
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
 * @brief Create a new secp256k1 context
 * @param a_flags Context flags (use DAP_SECP256K1_CONTEXT_NONE)
 * @return New context or NULL on error
 * @note Call dap_secp256k1_context_randomize() for side-channel protection
 */
dap_secp256k1_context_t *dap_secp256k1_context_create(unsigned int a_flags);

/**
 * @brief Destroy a secp256k1 context
 * @param a_ctx Context to destroy
 */
void dap_secp256k1_context_destroy(dap_secp256k1_context_t *a_ctx);

/**
 * @brief Randomize context for side-channel protection
 * @param a_ctx Context to randomize
 * @param a_seed32 32-byte random seed (NULL to reset)
 * @return 1 on success, 0 on error
 */
int dap_secp256k1_context_randomize(dap_secp256k1_context_t *a_ctx, const uint8_t *a_seed32);

// =============================================================================
// Key Functions
// =============================================================================

/**
 * @brief Verify a secret key is valid
 * @param a_ctx Context (can be NULL)
 * @param a_seckey 32-byte secret key
 * @return 1 if valid, 0 if invalid
 */
int dap_secp256k1_ec_seckey_verify(const dap_secp256k1_context_t *a_ctx, const uint8_t *a_seckey);

/**
 * @brief Compute public key from secret key
 * @param a_ctx Context
 * @param a_pubkey Output public key
 * @param a_seckey 32-byte secret key
 * @return 1 on success, 0 on error
 */
int dap_secp256k1_ec_pubkey_create(
    const dap_secp256k1_context_t *a_ctx,
    dap_secp256k1_pubkey_t *a_pubkey,
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
int dap_secp256k1_ec_pubkey_serialize(
    const dap_secp256k1_context_t *a_ctx,
    uint8_t *a_output,
    size_t *a_outputlen,
    const dap_secp256k1_pubkey_t *a_pubkey,
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
int dap_secp256k1_ec_pubkey_parse(
    const dap_secp256k1_context_t *a_ctx,
    dap_secp256k1_pubkey_t *a_pubkey,
    const uint8_t *a_input,
    size_t a_inputlen
);

// =============================================================================
// ECDSA Functions
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
int dap_secp256k1_ecdsa_sign(
    const dap_secp256k1_context_t *a_ctx,
    dap_secp256k1_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const uint8_t *a_seckey,
    dap_secp256k1_nonce_function_t a_noncefp,
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
int dap_secp256k1_ecdsa_verify(
    const dap_secp256k1_context_t *a_ctx,
    const dap_secp256k1_signature_t *a_sig,
    const uint8_t *a_msghash32,
    const dap_secp256k1_pubkey_t *a_pubkey
);

/**
 * @brief Serialize signature in compact format (64 bytes)
 * @param a_ctx Context
 * @param a_output64 Output buffer (64 bytes)
 * @param a_sig Signature
 * @return 1 on success
 */
int dap_secp256k1_ecdsa_signature_serialize_compact(
    const dap_secp256k1_context_t *a_ctx,
    uint8_t *a_output64,
    const dap_secp256k1_signature_t *a_sig
);

/**
 * @brief Parse signature from compact format
 * @param a_ctx Context
 * @param a_sig Output signature
 * @param a_input64 Input buffer (64 bytes)
 * @return 1 on success, 0 on error
 */
int dap_secp256k1_ecdsa_signature_parse_compact(
    const dap_secp256k1_context_t *a_ctx,
    dap_secp256k1_signature_t *a_sig,
    const uint8_t *a_input64
);

/**
 * @brief Normalize signature to low-S form
 * @param a_ctx Context
 * @param a_sigout Output signature (can be same as input)
 * @param a_sigin Input signature
 * @return 1 if was already normalized, 0 if was changed
 */
int dap_secp256k1_ecdsa_signature_normalize(
    const dap_secp256k1_context_t *a_ctx,
    dap_secp256k1_signature_t *a_sigout,
    const dap_secp256k1_signature_t *a_sigin
);

// =============================================================================
// SHA256 Functions (for internal hashing)
// =============================================================================

/**
 * @brief Initialize SHA256 context
 * @param a_hash Context to initialize
 */
void dap_secp256k1_sha256_initialize(dap_secp256k1_sha256_t *a_hash);

/**
 * @brief Update SHA256 with data
 * @param a_hash Context
 * @param a_data Data to hash
 * @param a_size Data size
 */
void dap_secp256k1_sha256_write(dap_secp256k1_sha256_t *a_hash, const uint8_t *a_data, size_t a_size);

/**
 * @brief Finalize SHA256 and get hash
 * @param a_hash Context
 * @param a_out32 Output buffer (32 bytes)
 */
void dap_secp256k1_sha256_finalize(dap_secp256k1_sha256_t *a_hash, uint8_t *a_out32);

// =============================================================================
// Default nonce function (RFC6979)
// =============================================================================

/**
 * @brief RFC6979 deterministic nonce function
 */
extern const dap_secp256k1_nonce_function_t dap_secp256k1_nonce_function_rfc6979;

/**
 * @brief Default nonce function (currently RFC6979)
 */
extern const dap_secp256k1_nonce_function_t dap_secp256k1_nonce_function_default;

#ifdef __cplusplus
}
#endif
