/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Sources         https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2019
 * All rights reserved.

 This file is part of CellFrame SDK the open source project

    CellFrame SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CellFrame SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any CellFrame SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "dap_common.h"
#include "dap_hash_sha3.h"

// =============================================================================
// Hash Dispatcher - High-level API
// =============================================================================

/**
 * @brief Available hash algorithms
 */
typedef enum dap_hash_type {
    DAP_HASH_TYPE_SHA3_256 = 0,     // Default, SHA3-256
    DAP_HASH_TYPE_KECCAK_256,       // Keccak-256 (Ethereum style)
    DAP_HASH_TYPE_SHA2_256,         // SHA2-256
    DAP_HASH_TYPE_SLOW_0 = 0x100    // Slow hash (for PoW etc)
} dap_hash_type_t;

// =============================================================================
// Type aliases - dap_hash_t is SHA3-256
// =============================================================================

typedef dap_hash_sha3_256_t         dap_hash_t;
typedef dap_hash_sha3_256_str_t     dap_hash_str_t;

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// High-level Hash API (dispatcher)
// =============================================================================

/**
 * @brief Compute hash of data using specified algorithm
 * @param a_type Hash algorithm
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output buffer (size depends on algorithm)
 * @param a_hash_out_size Size of output buffer
 * @return true on success, false on error
 */
bool dap_hash(dap_hash_type_t a_type, const void *a_data_in, size_t a_data_in_size,
              void *a_hash_out, size_t a_hash_out_size);

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return 0 on success, negative error code on failure
 */
int dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

// =============================================================================
// Hash utility functions (using SHA3-256 as dap_hash_t)
// =============================================================================

/**
 * @brief Compare two default hashes
 */
DAP_STATIC_INLINE bool dap_hash_compare(const dap_hash_t *a_hash1, const dap_hash_t *a_hash2)
{
    return dap_hash_sha3_256_compare(a_hash1, a_hash2);
}

/**
 * @brief Check if hash is blank
 */
DAP_STATIC_INLINE bool dap_hash_is_blank(const dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_is_blank(a_hash);
}

/**
 * @brief Convert hash to hex string
 */
DAP_STATIC_INLINE int dap_hash_to_str(const dap_hash_t *a_hash, char *a_str, size_t a_str_max)
{
    return dap_hash_sha3_256_to_str(a_hash, a_str, a_str_max);
}

/**
 * @brief Convert hash to string struct
 */
DAP_STATIC_INLINE dap_hash_str_t dap_hash_to_str_struct(const dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_to_str_struct(a_hash);
}

#define dap_hash_to_str_static(hash) dap_hash_sha3_256_to_str_static(hash)

/**
 * @brief Convert hash to newly allocated string
 */
DAP_STATIC_INLINE char *dap_hash_to_str_new(const dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_to_str_new(a_hash);
}

/**
 * @brief Parse hash from string
 */
DAP_STATIC_INLINE int dap_hash_from_str(const char *a_hash_str, dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_from_str(a_hash_str, a_hash);
}

/**
 * @brief Parse hash from hex string
 */
DAP_STATIC_INLINE int dap_hash_from_hex_str(const char *a_hex_str, dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_from_hex_str(a_hex_str, a_hash);
}

/**
 * @brief Parse hash from base58 string
 */
DAP_STATIC_INLINE int dap_hash_from_base58_str(const char *a_base58_str, dap_hash_t *a_hash)
{
    return dap_hash_sha3_256_from_base58_str(a_base58_str, a_hash);
}

/**
 * @brief Compute hash and return as newly allocated string
 */
DAP_STATIC_INLINE char *dap_hash_str_new(const void *a_data, size_t a_data_size)
{
    return dap_hash_sha3_256_str_new(a_data, a_data_size);
}

/**
 * @brief Compute hash and return as string struct
 */
DAP_STATIC_INLINE dap_hash_str_t dap_hash_data_to_str(const void *a_data, size_t a_data_size)
{
    return dap_hash_sha3_256_data_to_str(a_data, a_data_size);
}

#ifdef __cplusplus
}
#endif

