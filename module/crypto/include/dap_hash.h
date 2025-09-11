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


#define DAP_HASH_FAST_SIZE          32
#define DAP_CHAIN_HASH_FAST_SIZE    DAP_HASH_FAST_SIZE
#define DAP_CHAIN_HASH_FAST_STR_LEN (DAP_HASH_FAST_SIZE * 2 + 2 /* heading 0x */)
#define DAP_CHAIN_HASH_FAST_STR_SIZE (DAP_CHAIN_HASH_FAST_STR_LEN + 1 /*trailing zero*/)
#define DAP_HASH_FAST_STR_SIZE DAP_CHAIN_HASH_FAST_STR_SIZE

typedef enum dap_hash_type {
    DAP_HASH_TYPE_KECCAK = 0,
    DAP_HASH_TYPE_SLOW_0 = 1,
    DAP_HASH_TYPE_SHA3_256 = 2,
    DAP_HASH_TYPE_SHA3_384 = 3,
    DAP_HASH_TYPE_SHA3_512 = 4,
    DAP_HASH_TYPE_SHAKE128 = 5,
    DAP_HASH_TYPE_SHAKE256 = 6
} dap_hash_type_t;

/**
 * @brief Hash function flags for extended functionality
 */
typedef enum dap_hash_flags {
    DAP_HASH_FLAG_NONE = 0,
    DAP_HASH_FLAG_DOMAIN_SEPARATION = 1,  ///< Add domain separation prefix
    DAP_HASH_FLAG_SALT = 2,               ///< Use provided salt/context
    DAP_HASH_FLAG_ITERATIVE = 4           ///< Multiple hash iterations
} dap_hash_flags_t;

/**
 * @brief Extended parameters for hash function
 */
typedef struct dap_hash_params {
    const uint8_t *salt;                  ///< Salt/context data (can be NULL)
    size_t salt_size;                     ///< Size of salt data
    const char *domain_separator;         ///< Domain separation string (can be NULL)
    uint32_t iterations;                  ///< Number of iterations for iterative hashing (0 = single)
    uint32_t security_level;              ///< Desired security level in bits
} dap_hash_params_t;

typedef union dap_chain_hash_fast{
    uint8_t raw[DAP_CHAIN_HASH_FAST_SIZE];
} DAP_ALIGN_PACKED dap_chain_hash_fast_t;
typedef dap_chain_hash_fast_t dap_hash_fast_t;
typedef dap_hash_fast_t dap_hash_t;
typedef struct dap_hash_str {
    char s[DAP_HASH_FAST_STR_SIZE];
} dap_hash_str_t;

#ifdef __cplusplus
extern "C" {
#endif

int dap_chain_hash_fast_from_str( const char * a_hash_str, dap_hash_fast_t *a_hash);
int dap_chain_hash_fast_from_hex_str( const char *a_hex_str, dap_chain_hash_fast_t *a_hash);
int dap_chain_hash_fast_from_base58_str(const char *a_base58_str,  dap_chain_hash_fast_t *a_hash);
/**
 * @brief
 * get SHA3_256 hash for specific data
 * @param a_data_in input data
 * @param a_data_in_size size of input data
 * @param a_hash_out returned hash
 * @return true
 * @return false
 */
bool dap_hash_fast( const void *a_data_in, size_t a_data_in_size, dap_hash_fast_t *a_hash_out );

/**
 * @brief Configurable hash function with arbitrary output size
 * @details Supports different hash algorithms and arbitrary output length
 * 
 * @param a_hash_type Hash algorithm type (SHA3-256, SHA3-512, SHAKE-128, etc.)
 * @param a_input Input data to hash
 * @param a_input_size Size of input data
 * @param a_output Output buffer for hash
 * @param a_output_size Desired output size (for SHAKE functions)
 * @param a_flags Additional flags (domain separation, salt, etc.)
 * @param a_params Extended parameters structure (can be NULL for defaults)
 * @return 0 on success, negative on error
 */
int dap_hash(dap_hash_type_t a_hash_type,
            const void *a_input, size_t a_input_size,
            uint8_t *a_output, size_t a_output_size,
            dap_hash_flags_t a_flags,
            const dap_hash_params_t *a_params);


/**
 * @brief dap_hash_fast_compare
 * compare to hashes (dap_hash_fast_t) through memcmp
 * @param a_hash1 - dap_hash_fast_t hash1
 * @param a_hash2 - dap_hash_fast_t hash2
 * @return
 */
DAP_STATIC_INLINE bool dap_hash_fast_compare(const dap_hash_fast_t *a_hash1, const dap_hash_fast_t *a_hash2)
{
    if(!a_hash1 || !a_hash2)
        return false;
    return !memcmp(a_hash1, a_hash2, sizeof(dap_hash_fast_t)); /* 0 - true, <> 0 - false */
}

/**
 * @brief
 * compare hash with blank hash
 * @param a_hash
 * @return true
 * @return false
 */

DAP_STATIC_INLINE bool dap_hash_fast_is_blank( const dap_hash_fast_t *a_hash )
{
    static dap_hash_fast_t l_blank_hash = {};
    return dap_hash_fast_compare(a_hash, &l_blank_hash);
}

DAP_STATIC_INLINE void dap_chain_hash_fast_to_str_do(const dap_hash_fast_t *a_hash, char *a_str)
{
    a_str[0] = '0';
    a_str[1] = 'x';
    dap_htoa64((a_str + 2), a_hash->raw, DAP_CHAIN_HASH_FAST_SIZE);
    a_str[ DAP_CHAIN_HASH_FAST_STR_SIZE - 1 ] = '\0';
}

DAP_STATIC_INLINE int dap_chain_hash_fast_to_str(const dap_hash_fast_t *a_hash, char *a_str, size_t a_str_max )
{
    if(! a_hash )
        return -1;
    if(! a_str )
        return -2;
    if( a_str_max < DAP_CHAIN_HASH_FAST_STR_SIZE )
        return -3;
    dap_chain_hash_fast_to_str_do(a_hash, a_str);
    return DAP_CHAIN_HASH_FAST_STR_SIZE;
}

DAP_STATIC_INLINE dap_hash_str_t dap_chain_hash_fast_to_hash_str(const dap_hash_fast_t *a_hash) {
    dap_hash_str_t l_ret = { };
    dap_chain_hash_fast_to_str(a_hash, l_ret.s, DAP_CHAIN_HASH_FAST_STR_SIZE);
    return l_ret;
}

#define dap_chain_hash_fast_to_str_static(hash) dap_chain_hash_fast_to_hash_str(hash).s
#define dap_hash_fast_to_str dap_chain_hash_fast_to_str
#define dap_hash_fast_to_str_static dap_chain_hash_fast_to_str_static

DAP_STATIC_INLINE char *dap_chain_hash_fast_to_str_new(const dap_hash_fast_t *a_hash)
{
    if (!a_hash)
        return NULL;
    char *l_ret = DAP_NEW_Z_SIZE(char, DAP_CHAIN_HASH_FAST_STR_SIZE);
    // Avoid compiler warning with NULL '%s' argument
    dap_chain_hash_fast_to_str_do(a_hash, l_ret);
    return l_ret;
}

#define dap_hash_fast_to_str_new dap_chain_hash_fast_to_str_new

/**
 * @brief dap_hash_fast_str_new
 * @param a_data
 * @param a_data_size
 * @return
 */
DAP_STATIC_INLINE char *dap_hash_fast_str_new( const void *a_data, size_t a_data_size )
{
    if(!a_data || !a_data_size)
        return NULL;

    dap_chain_hash_fast_t l_hash = { };
    dap_hash_fast(a_data, a_data_size, &l_hash);
    char *a_str = DAP_NEW_Z_SIZE(char, DAP_CHAIN_HASH_FAST_STR_SIZE);
    if (dap_chain_hash_fast_to_str(&l_hash, a_str, DAP_CHAIN_HASH_FAST_STR_SIZE) > 0)
        return a_str;
    DAP_DELETE(a_str);
    return NULL;
}

DAP_STATIC_INLINE dap_hash_str_t dap_get_data_hash_str(const void *a_data, size_t a_data_size)
{
    dap_hash_str_t l_ret = { };
    dap_hash_fast_t dummy_hash;
    dap_hash_fast(a_data, a_data_size, &dummy_hash);
    dap_chain_hash_fast_to_str(&dummy_hash, l_ret.s, DAP_CHAIN_HASH_FAST_STR_SIZE);
    return l_ret;
}

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
int dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

#ifdef __cplusplus
}
#endif
