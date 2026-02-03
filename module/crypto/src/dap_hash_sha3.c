/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://github.com/demlabsinc
 * Copyright  (c) 2017-2018
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_strfuncs.h"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_enc_base58.h"
// XKCP includes (will be replaced with native implementation)
#include "SimpleFIPS202.h"

#define LOG_TAG "dap_hash_sha3"

/**
 * @brief Parse SHA3 hash from hex string
 * @param a_hex_str Input hex string (with 0x prefix)
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_hex_str(const char *a_hex_str, dap_hash_sha3_256_t *a_hash)
{
    if (!a_hex_str || !a_hash)
        return -1;
    size_t l_hash_str_len = strlen(a_hex_str);
    if (l_hash_str_len == DAP_HASH_SHA3_256_STR_LEN && !dap_strncmp(a_hex_str, "0x", 2) && 
        !dap_is_hex_string(a_hex_str + 2, l_hash_str_len - 2)) {
        dap_hex2bin(a_hash->raw, a_hex_str + 2, l_hash_str_len - 2);
        return 0;
    } else {
        return -1;
    }
}

/**
 * @brief Parse SHA3 hash from base58 string
 * @param a_base58_str Input base58 string
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_base58_str(const char *a_base58_str, dap_hash_sha3_256_t *a_hash)
{
    if (!a_hash)
        return -1;
    if (!a_base58_str)
        return -2;
    size_t l_hash_len = dap_strlen(a_base58_str);
    if (l_hash_len > DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_sha3_256_t)))
        return -3;
    // from base58 to binary
    byte_t l_out[DAP_ENC_BASE58_DECODE_SIZE(DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_sha3_256_t)))];
    size_t l_out_size = dap_enc_base58_decode(a_base58_str, l_out);
    if (l_out_size != sizeof(dap_hash_sha3_256_t))
        return -4;
    memcpy(a_hash, l_out, sizeof(dap_hash_sha3_256_t));
    return 0;
}

/**
 * @brief Parse SHA3 hash from string (auto-detect hex or base58)
 * @param a_hash_str Input string
 * @param a_hash Output hash
 * @return 0 on success, negative on error
 */
int dap_hash_sha3_256_from_str(const char *a_hash_str, dap_hash_sha3_256_t *a_hash)
{
    // Try hex first, then base58
    return dap_hash_sha3_256_from_hex_str(a_hash_str, a_hash) && 
           dap_hash_sha3_256_from_base58_str(a_hash_str, a_hash);
}

/**
 * @brief Compute SHA3-256 hash of data
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output hash (32 bytes)
 * @return true on success, false on failure
 */
bool dap_hash_sha3_256(const void *a_data_in, size_t a_data_in_size, dap_hash_sha3_256_t *a_hash_out)
{
    // Allow empty input (a_data_in_size == 0) - SHA3 can hash empty data
    if (a_hash_out == NULL)
        return false;
    
    // For non-empty input, a_data_in must not be NULL and vice versa
    if ((a_data_in == NULL) != (a_data_in_size == 0))
        return false;

    SHA3_256((unsigned char *)a_hash_out, (const unsigned char *)a_data_in, a_data_in_size);
    return true;
}
