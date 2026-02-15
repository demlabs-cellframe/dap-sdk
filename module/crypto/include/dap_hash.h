/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Sources         https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2025
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
// Legacy type definitions for backward compatibility with cellframe-sdk
// =============================================================================

#define DAP_HASH_FAST_SIZE          DAP_HASH_SHA3_256_SIZE
#define DAP_CHAIN_HASH_FAST_SIZE    DAP_HASH_FAST_SIZE
#define DAP_CHAIN_HASH_FAST_STR_LEN (DAP_HASH_FAST_SIZE * 2 + 2 /* heading 0x */)
#define DAP_CHAIN_HASH_FAST_STR_SIZE (DAP_CHAIN_HASH_FAST_STR_LEN + 1 /*trailing zero*/)
#define DAP_HASH_FAST_STR_SIZE DAP_CHAIN_HASH_FAST_STR_SIZE

// Legacy types as aliases to new SHA3-256 types
typedef dap_hash_sha3_256_t dap_chain_hash_fast_t;
typedef dap_chain_hash_fast_t dap_hash_fast_t;
typedef dap_hash_fast_t dap_hash_t;
typedef dap_hash_sha3_256_str_t dap_hash_str_t;

// Legacy function wrappers
#define dap_hash_fast(data, size, hash_out) dap_hash_sha3_256(data, size, hash_out)
#define dap_hash_fast_is_blank(hash) dap_hash_sha3_256_is_blank(hash)
#define dap_hash_fast_compare(h1, h2) dap_hash_sha3_256_compare(h1, h2)
#define dap_chain_hash_fast_is_blank(hash) dap_hash_sha3_256_is_blank(hash)
#define dap_chain_hash_fast_compare(h1, h2) dap_hash_sha3_256_compare(h1, h2)
#define dap_chain_hash_fast_to_str(hash, str, max) dap_hash_sha3_256_to_str(hash, str, max)
#define dap_chain_hash_fast_to_str_new(hash) dap_hash_sha3_256_to_str_new(hash)
#define dap_chain_hash_fast_to_str_static(hash) dap_hash_sha3_256_to_str_static(hash)
#define dap_hash_fast_to_str(hash, str, max) dap_hash_sha3_256_to_str(hash, str, max)
#define dap_hash_fast_to_str_new(hash) dap_hash_sha3_256_to_str_new(hash)
#define dap_hash_fast_to_str_static(hash) dap_hash_sha3_256_to_str_static(hash)
#define dap_get_data_hash_str(data, size) dap_hash_sha3_256_data_to_str(data, size)
#define dap_chain_hash_fast_from_str(str, hash) dap_hash_sha3_256_from_str(str, hash)
#define dap_hash_fast_from_str(str, hash) dap_hash_sha3_256_from_str(str, hash)
#define dap_chain_hash_fast_from_hex_str(str, hash) dap_hash_sha3_256_from_hex_str(str, hash)
#define dap_hash_fast_from_hex_str(str, hash) dap_hash_sha3_256_from_hex_str(str, hash)
#define dap_hash_fast_str_new(data, size) dap_hash_sha3_256_str_new(data, size)
#define dap_chain_hash_fast_to_str_do(hash, str) dap_hash_sha3_256_to_str_do(hash, str)
#define dap_hash_fast_to_str_do(hash, str) dap_hash_sha3_256_to_str_do(hash, str)
#define dap_chain_hash_fast_from_base58_str(str, hash) dap_hash_sha3_256_from_base58_str(str, hash)
#define dap_hash_fast_from_base58_str(str, hash) dap_hash_sha3_256_from_base58_str(str, hash)

// =============================================================================
// Hash Dispatcher - Universal API for multiple hash algorithms
// =============================================================================

/**
 * @brief Available hash algorithms
 */
typedef enum dap_hash_type {
    DAP_HASH_TYPE_SHA3_256 = 0,     // SHA3-256 (default)
    DAP_HASH_TYPE_KECCAK_256,       // Keccak-256 (Ethereum style)
    DAP_HASH_TYPE_SHA2_256,         // SHA2-256
    DAP_HASH_TYPE_SLOW_0 = 0x100    // Slow hash (for PoW etc)
} dap_hash_type_t;

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Universal Hash API (type-agnostic)
// =============================================================================

/**
 * @brief Get hash size for specified algorithm
 * @param a_type Hash algorithm
 * @return Size in bytes, or 0 for unknown type
 */
DAP_STATIC_INLINE size_t dap_hash_size(dap_hash_type_t a_type)
{
    switch (a_type) {
        case DAP_HASH_TYPE_SHA3_256:
        case DAP_HASH_TYPE_KECCAK_256:
        case DAP_HASH_TYPE_SHA2_256:
            return 32;
        case DAP_HASH_TYPE_SLOW_0:
            return 32; // TODO: verify slow hash size
        default:
            return 0;
    }
}

/**
 * @brief Get hex string size for specified algorithm (including 0x prefix and null terminator)
 * @param a_type Hash algorithm
 * @return String size, or 0 for unknown type
 */
DAP_STATIC_INLINE size_t dap_hash_str_size(dap_hash_type_t a_type)
{
    size_t l_hash_size = dap_hash_size(a_type);
    return l_hash_size ? (l_hash_size * 2 + 2 /* 0x */ + 1 /* \0 */) : 0;
}

/**
 * @brief Compute hash of data using specified algorithm
 * @param a_type Hash algorithm
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output buffer (must be at least dap_hash_size(a_type) bytes)
 * @param a_hash_out_size Size of output buffer
 * @return true on success, false on error
 */
bool dap_hash(dap_hash_type_t a_type, const void *a_data_in, size_t a_data_in_size,
              void *a_hash_out, size_t a_hash_out_size);

/**
 * @brief Compare two hashes of specified type
 * @param a_type Hash algorithm
 * @param a_hash1 First hash
 * @param a_hash2 Second hash
 * @return true if equal, false otherwise
 */
DAP_STATIC_INLINE bool dap_hash_compare_type(dap_hash_type_t a_type, const void *a_hash1, const void *a_hash2)
{
    if (!a_hash1 || !a_hash2)
        return false;
    size_t l_size = dap_hash_size(a_type);
    return l_size ? !memcmp(a_hash1, a_hash2, l_size) : false;
}

/**
 * @brief Check if hash is blank (all zeros)
 * @param a_type Hash algorithm
 * @param a_hash Hash to check
 * @return true if blank, false otherwise
 */
DAP_STATIC_INLINE bool dap_hash_is_blank_type(dap_hash_type_t a_type, const void *a_hash)
{
    if (!a_hash)
        return true;
    size_t l_size = dap_hash_size(a_type);
    const byte_t *l_bytes = (const byte_t *)a_hash;
    for (size_t i = 0; i < l_size; i++) {
        if (l_bytes[i] != 0)
            return false;
    }
    return true;
}

/**
 * @brief Convert hash to hex string (with 0x prefix)
 * @param a_type Hash algorithm
 * @param a_hash Input hash
 * @param a_str Output string buffer
 * @param a_str_max Size of output buffer
 * @return String length on success, negative on error
 */
DAP_STATIC_INLINE int dap_hash_to_str_type(dap_hash_type_t a_type, const void *a_hash, char *a_str, size_t a_str_max)
{
    if (!a_hash)
        return -1;
    if (!a_str)
        return -2;
    size_t l_hash_size = dap_hash_size(a_type);
    size_t l_str_size = dap_hash_str_size(a_type);
    if (!l_hash_size || a_str_max < l_str_size)
        return -3;
    a_str[0] = '0';
    a_str[1] = 'x';
    dap_htoa64((a_str + 2), (const byte_t *)a_hash, l_hash_size);
    a_str[l_str_size - 1] = '\0';
    return (int)l_str_size;
}

/**
 * @brief Convert hash to newly allocated hex string
 * @param a_type Hash algorithm
 * @param a_hash Input hash
 * @return Newly allocated string (caller must free), or NULL on error
 */
DAP_STATIC_INLINE char *dap_hash_to_str_new_type(dap_hash_type_t a_type, const void *a_hash)
{
    if (!a_hash)
        return NULL;
    size_t l_str_size = dap_hash_str_size(a_type);
    if (!l_str_size)
        return NULL;
    char *l_ret = DAP_NEW_Z_SIZE(char, l_str_size);
    if (dap_hash_to_str_type(a_type, a_hash, l_ret, l_str_size) < 0) {
        DAP_DELETE(l_ret);
        return NULL;
    }
    return l_ret;
}

// =============================================================================
// SHA2-256 specific
// =============================================================================

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return 0 on success, negative error code on failure
 */
int dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

#ifdef __cplusplus
}
#endif
