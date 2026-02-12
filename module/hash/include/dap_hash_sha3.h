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
 * @file dap_hash_sha3.h
 * @brief SHA3 hash functions (FIPS 202)
 * @details Native DAP implementation based on Keccak-p[1600] permutation.
 *
 * Supported algorithms:
 *   - SHA3-224, SHA3-256, SHA3-384, SHA3-512 (fixed output)
 *
 * For SHAKE XOF functions see:
 *   - dap_hash_shake128.h
 *   - dap_hash_shake256.h
 *
 * @note Uses native DAP Keccak implementation with SIMD dispatch.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_keccak.h"
#include "dap_base58.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants
// =============================================================================

// SHA3 output sizes (bytes)
#define DAP_HASH_SHA3_224_SIZE      28
#define DAP_HASH_SHA3_256_SIZE      32
#define DAP_HASH_SHA3_384_SIZE      48
#define DAP_HASH_SHA3_512_SIZE      64

// Alternate names
#define SHA3_512_DIGEST_LENGTH      DAP_HASH_SHA3_512_SIZE

// SHA3-256 string format
#define DAP_HASH_SHA3_256_STR_LEN   (DAP_HASH_SHA3_256_SIZE * 2 + 2)  // 0x + hex
#define DAP_HASH_SHA3_256_STR_SIZE  (DAP_HASH_SHA3_256_STR_LEN + 1)   // + null

// =============================================================================
// Types
// =============================================================================

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

// =============================================================================
// SHA3 One-shot Functions
// =============================================================================

/**
 * @brief Compute SHA3-224 hash
 * @param a_output Output buffer (28 bytes)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_sha3_224(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHA3_224_RATE, DAP_KECCAK_SHA3_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_squeeze(&l_ctx, a_output, DAP_HASH_SHA3_224_SIZE);
}

/**
 * @brief Compute SHA3-256 hash (typed version)
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output hash struct
 * @return true on success
 */
bool dap_hash_sha3_256(const void *a_data_in, size_t a_data_in_size, dap_hash_sha3_256_t *a_hash_out);

/**
 * @brief Compute SHA3-256 hash (raw version)
 * @param a_output Output buffer (32 bytes)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_sha3_256_raw(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHA3_256_RATE, DAP_KECCAK_SHA3_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_squeeze(&l_ctx, a_output, DAP_HASH_SHA3_256_SIZE);
}

/**
 * @brief Compute SHA3-384 hash
 * @param a_output Output buffer (48 bytes)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_sha3_384(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHA3_384_RATE, DAP_KECCAK_SHA3_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_squeeze(&l_ctx, a_output, DAP_HASH_SHA3_384_SIZE);
}

/**
 * @brief Compute SHA3-512 hash
 * @param a_output Output buffer (64 bytes)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_sha3_512(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHA3_512_RATE, DAP_KECCAK_SHA3_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_squeeze(&l_ctx, a_output, DAP_HASH_SHA3_512_SIZE);
}

// =============================================================================
// SHA3-256 String Conversion Functions
// =============================================================================

/**
 * @brief Parse SHA3 hash from string (auto-detect format: hex or base58)
 */
int dap_hash_sha3_256_from_str(const char *a_hash_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Parse SHA3 hash from hex string
 */
int dap_hash_sha3_256_from_hex_str(const char *a_hex_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Parse SHA3 hash from base58 string
 */
int dap_hash_sha3_256_from_base58_str(const char *a_base58_str, dap_hash_sha3_256_t *a_hash);

/**
 * @brief Compare two SHA3 hashes
 */
DAP_STATIC_INLINE bool dap_hash_sha3_256_compare(const dap_hash_sha3_256_t *a_hash1, const dap_hash_sha3_256_t *a_hash2)
{
    if (!a_hash1 || !a_hash2)
        return false;
    return !memcmp(a_hash1, a_hash2, sizeof(dap_hash_sha3_256_t));
}

/**
 * @brief Check if SHA3 hash is blank (all zeros)
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
 */
DAP_STATIC_INLINE int dap_hash_sha3_256_to_str(const dap_hash_sha3_256_t *a_hash, char *a_str, size_t a_str_max)
{
    if (!a_hash) return -1;
    if (!a_str) return -2;
    if (a_str_max < DAP_HASH_SHA3_256_STR_SIZE) return -3;
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

// =============================================================================
// SHA3-256 Base58 Conversion Functions
// =============================================================================

/**
 * @brief SHA3-256 hash as base58 string
 */
typedef union dap_hash_sha3_256_b58_str {
    char s[DAP_BASE58_ENCODE_SIZE(sizeof(dap_hash_sha3_256_t))];
} dap_hash_sha3_256_b58_str_t;

/**
 * @brief Convert SHA3 hash to base58 string
 */
DAP_STATIC_INLINE char *dap_hash_sha3_256_to_base58_str(const dap_hash_sha3_256_t *a_hash)
{
    return dap_base58_encode_to_str(a_hash->raw, sizeof(dap_hash_sha3_256_t));
}

/**
 * @brief Convert SHA3 hash to base58 string struct
 */
dap_hash_sha3_256_b58_str_t dap_hash_sha3_256_to_base58_str_static_(const dap_hash_sha3_256_t *a_hash);

#define dap_hash_sha3_256_to_base58_str_static(hash) dap_hash_sha3_256_to_base58_str_static_(hash).s

// Legacy names for compatibility
#define dap_enc_b58_hash_str_t                      dap_hash_sha3_256_b58_str_t
#define dap_enc_base58_encode_hash_to_str           dap_hash_sha3_256_to_base58_str
#define dap_enc_base58_encode_hash_to_str_static_   dap_hash_sha3_256_to_base58_str_static_
#define dap_enc_base58_encode_hash_to_str_static    dap_hash_sha3_256_to_base58_str_static

/**
 * @brief Convert SHA3 hash to newly allocated hex string
 */
DAP_STATIC_INLINE char *dap_hash_sha3_256_to_str_new(const dap_hash_sha3_256_t *a_hash)
{
    if (!a_hash) return NULL;
    char *l_ret = DAP_NEW_Z_SIZE(char, DAP_HASH_SHA3_256_STR_SIZE);
    dap_hash_sha3_256_to_str_do(a_hash, l_ret);
    return l_ret;
}

/**
 * @brief Compute SHA3-256 hash and return as newly allocated string
 */
DAP_STATIC_INLINE char *dap_hash_sha3_256_str_new(const void *a_data, size_t a_data_size)
{
    if (!a_data || !a_data_size) return NULL;
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
