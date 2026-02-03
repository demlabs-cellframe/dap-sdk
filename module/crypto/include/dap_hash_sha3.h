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

// =============================================================================
// SHA3-256 Hash (Cryptographic)
// =============================================================================

#define DAP_HASH_SHA3_256_SIZE      32
#define DAP_HASH_SHA3_256_STR_LEN   (DAP_HASH_SHA3_256_SIZE * 2 + 2 /* heading 0x */)
#define DAP_HASH_SHA3_256_STR_SIZE  (DAP_HASH_SHA3_256_STR_LEN + 1 /* trailing zero */)

/**
 * @brief SHA3-256 hash type (32 bytes)
 */
typedef union dap_hash_sha3_256 {
    uint8_t raw[DAP_HASH_SHA3_256_SIZE];
} DAP_ALIGN_PACKED dap_hash_sha3_256_t;

/**
 * @brief SHA3 hash as hex string
 */
typedef struct dap_hash_sha3_256_str {
    char s[DAP_HASH_SHA3_256_STR_SIZE];
} dap_hash_sha3_256_str_t;

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// SHA3 Core Functions
// =============================================================================

/**
 * @brief Parse SHA3 hash from string (auto-detect format: hex or base58)
 * @param a_hash_str Input string (hex with 0x prefix, or base58)
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_str(const char *a_hash_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Parse SHA3 hash from hex string
 * @param a_hex_str Input hex string (with 0x prefix)
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_hex_str(const char *a_hex_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Parse SHA3 hash from base58 string
 * @param a_base58_str Input base58 string
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_base58_str(const char *a_base58_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Compute SHA3-256 hash of data
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output hash (32 bytes)
 * @return true on success, false on error
 */
bool dap_hash_sha3_256(const void *a_data_in, size_t a_data_in_size, dap_hash_sha3_256_t *a_hash_out);

/**
 * @brief Compare two SHA3 hashes
 * @param a_hash1 First hash
 * @param a_hash2 Second hash
 * @return true if equal, false otherwise
 */
DAP_STATIC_INLINE bool dap_hash_sha3_256_compare(const dap_hash_sha3_256_t *a_hash1, const dap_hash_sha3_256_t *a_hash2)
{
    if (!a_hash1 || !a_hash2)
        return false;
    return !memcmp(a_hash1, a_hash2, sizeof(dap_hash_sha3_256_t));
}

/**
 * @brief Check if SHA3 hash is blank (all zeros)
 * @param a_hash Hash to check
 * @return true if blank, false otherwise
 */
DAP_STATIC_INLINE bool dap_hash_sha3_256_is_blank(const dap_hash_sha3_256_t *a_hash)
{
    static dap_hash_sha3_256_t l_blank_hash = {};
    return dap_hash_sha3_256_compare(a_hash, &l_blank_hash);
}

/**
 * @brief Convert SHA3 hash to hex string (internal)
 */
DAP_STATIC_INLINE void dap_hash_sha3_256_to_str_do(const dap_hash_sha3_256_t *a_hash, char *a_str)
{
    a_str[0] = '0';
    a_str[1] = 'x';
    dap_htoa64((a_str + 2), a_hash->raw, DAP_HASH_SHA3_256_SIZE);
    a_str[DAP_HASH_SHA3_256_STR_SIZE - 1] = '\0';
}

/**
 * @brief Convert SHA3 hash to hex string
 * @param a_hash Input hash
 * @param a_str Output string buffer
 * @param a_str_max Size of output buffer
 * @return String length on success, negative on error
 */
DAP_STATIC_INLINE int dap_hash_sha3_256_to_str(const dap_hash_sha3_256_t *a_hash, char *a_str, size_t a_str_max)
{
    if (!a_hash)
        return -1;
    if (!a_str)
        return -2;
    if (a_str_max < DAP_HASH_SHA3_256_STR_SIZE)
        return -3;
    dap_hash_sha3_256_to_str_do(a_hash, a_str);
    return DAP_HASH_SHA3_256_STR_SIZE;
}

/**
 * @brief Convert SHA3 hash to string struct
 */
DAP_STATIC_INLINE dap_hash_sha3_256_str_t dap_hash_sha3_256_to_str_struct(const dap_hash_sha3_256_t *a_hash)
{
    dap_hash_sha3_256_str_t l_ret = { };
    dap_hash_sha3_256_to_str(a_hash, l_ret.s, DAP_HASH_SHA3_256_STR_SIZE);
    return l_ret;
}

#define dap_hash_sha3_256_to_str_static(hash) dap_hash_sha3_256_to_str_struct(hash).s

/**
 * @brief Convert SHA3 hash to newly allocated hex string
 * @param a_hash Input hash
 * @return Newly allocated string (caller must free), or NULL on error
 */
DAP_STATIC_INLINE char *dap_hash_sha3_256_to_str_new(const dap_hash_sha3_256_t *a_hash)
{
    if (!a_hash)
        return NULL;
    char *l_ret = DAP_NEW_Z_SIZE(char, DAP_HASH_SHA3_256_STR_SIZE);
    dap_hash_sha3_256_to_str_do(a_hash, l_ret);
    return l_ret;
}

/**
 * @brief Compute SHA3-256 hash and return as newly allocated string
 * @param a_data Input data
 * @param a_data_size Size of input data
 * @return Newly allocated hex string, or NULL on error
 */
DAP_STATIC_INLINE char *dap_hash_sha3_256_str_new(const void *a_data, size_t a_data_size)
{
    if (!a_data || !a_data_size)
        return NULL;

    dap_hash_sha3_256_t l_hash = { };
    dap_hash_sha3_256(a_data, a_data_size, &l_hash);
    char *a_str = DAP_NEW_Z_SIZE(char, DAP_HASH_SHA3_256_STR_SIZE);
    if (dap_hash_sha3_256_to_str(&l_hash, a_str, DAP_HASH_SHA3_256_STR_SIZE) > 0)
        return a_str;
    DAP_DELETE(a_str);
    return NULL;
}

/**
 * @brief Compute SHA3-256 hash and return as string struct
 */
DAP_STATIC_INLINE dap_hash_sha3_256_str_t dap_hash_sha3_256_data_to_str(const void *a_data, size_t a_data_size)
{
    dap_hash_sha3_256_str_t l_ret = { };
    dap_hash_sha3_256_t l_hash;
    dap_hash_sha3_256(a_data, a_data_size, &l_hash);
    dap_hash_sha3_256_to_str(&l_hash, l_ret.s, DAP_HASH_SHA3_256_STR_SIZE);
    return l_ret;
}

#ifdef __cplusplus
}
#endif
