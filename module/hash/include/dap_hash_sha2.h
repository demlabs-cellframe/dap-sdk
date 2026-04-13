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
 * @file dap_hash_sha2.h
 * @brief SHA-2 hash functions (SHA-256, SHA-512)
 * @details Native DAP SDK implementation of SHA-2 family.
 *
 * SHA-256 produces a 256-bit (32-byte) hash.
 * Used for digital signatures, integrity verification, etc.
 *
 * FIPS 180-4 compliant implementation.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants
// =============================================================================

#define DAP_HASH_SHA2_256_SIZE      32
#define DAP_HASH_SHA2_256_BLOCK     64

// =============================================================================
// Types
// =============================================================================

/**
 * @brief SHA-256 context for incremental hashing
 */
typedef struct {
    uint32_t state[8];      // Hash state
    uint64_t count;         // Number of bytes processed
    uint8_t buffer[64];     // Input buffer
} dap_hash_sha2_256_ctx_t;

// =============================================================================
// One-shot API
// =============================================================================

/**
 * @brief Compute SHA2-256 hash in one call
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length in bytes
 */
void dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

// =============================================================================
// Incremental API
// =============================================================================

/**
 * @brief Initialize SHA-256 context
 * @param[out] a_ctx Context to initialize
 */
void dap_hash_sha2_256_init(dap_hash_sha2_256_ctx_t *a_ctx);

/**
 * @brief Update SHA-256 context with more data
 * @param[in,out] a_ctx Context
 * @param[in] a_data Data to hash
 * @param[in] a_len Data length
 */
void dap_hash_sha2_256_update(dap_hash_sha2_256_ctx_t *a_ctx, const uint8_t *a_data, size_t a_len);

/**
 * @brief Finalize SHA-256 and get hash
 * @param[in,out] a_ctx Context (will be cleared)
 * @param[out] a_output Output buffer (32 bytes)
 */
void dap_hash_sha2_256_final(dap_hash_sha2_256_ctx_t *a_ctx, uint8_t a_output[32]);

// =============================================================================
// HMAC-SHA256
// =============================================================================

/**
 * @brief Compute HMAC-SHA256
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_key Key
 * @param[in] a_keylen Key length
 * @param[in] a_data Data to authenticate
 * @param[in] a_datalen Data length
 */
void dap_hash_hmac_sha2_256(
    uint8_t a_output[32],
    const uint8_t *a_key, size_t a_keylen,
    const uint8_t *a_data, size_t a_datalen
);

#ifdef __cplusplus
}
#endif
