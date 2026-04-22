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
 * @file dap_hash_sha3.c
 * @brief SHA3 hash functions implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_base58.h"
#include "dap_hash_sha3.h"
#include "dap_hash_keccak.h"

#define LOG_TAG "dap_hash_sha3"

// =============================================================================
// SHA3-256 Core Implementation
// =============================================================================

bool dap_hash_sha3_256(const void *a_data_in, size_t a_data_in_size, dap_hash_sha3_256_t *a_hash_out)
{
    if (a_hash_out == NULL)
        return false;

    if (a_data_in == NULL && a_data_in_size > 0)
        return false;

    uint64_t l_st[25];
    dap_keccak_sponge_get_ops()->absorb_136(l_st,
        (const uint8_t *)a_data_in, a_data_in_size, DAP_KECCAK_SHA3_SUFFIX);
    memcpy(a_hash_out->raw, l_st, DAP_HASH_SHA3_256_SIZE);
    return true;
}

// =============================================================================
// String Parsing Functions
// =============================================================================

int dap_hash_sha3_256_from_hex_str(const char *a_hex_str, dap_hash_sha3_256_t *a_hash)
{
    if (!a_hex_str || !a_hash)
        return -1;
    size_t l_len = strlen(a_hex_str);
    if (l_len == DAP_HASH_SHA3_256_STR_LEN && 
        !dap_strncmp(a_hex_str, "0x", 2) && 
        !dap_is_hex_string(a_hex_str + 2, l_len - 2)) {
        dap_hex2bin(a_hash->raw, a_hex_str + 2, l_len - 2);
        return 0;
    }
    return -1;
}

int dap_hash_sha3_256_from_base58_str(const char *a_base58_str, dap_hash_sha3_256_t *a_hash)
{
    if (!a_hash)
        return -1;
    if (!a_base58_str)
        return -2;
    size_t l_len = dap_strlen(a_base58_str);
    if (l_len > DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_sha3_256_t)))
        return -3;
    byte_t l_out[DAP_ENC_BASE58_DECODE_SIZE(DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_sha3_256_t)))];
    size_t l_out_size = dap_enc_base58_decode(a_base58_str, l_out);
    if (l_out_size != sizeof(dap_hash_sha3_256_t))
        return -4;
    memcpy(a_hash, l_out, sizeof(dap_hash_sha3_256_t));
    return 0;
}

int dap_hash_sha3_256_from_str(const char *a_hash_str, dap_hash_sha3_256_t *a_hash)
{
    // Try hex first, then base58
    return dap_hash_sha3_256_from_hex_str(a_hash_str, a_hash) && 
           dap_hash_sha3_256_from_base58_str(a_hash_str, a_hash);
}

// =============================================================================
// Base58 Conversion Functions
// =============================================================================

dap_hash_sha3_256_b58_str_t dap_hash_sha3_256_to_base58_str_static_(const dap_hash_sha3_256_t *a_hash)
{
    dap_hash_sha3_256_b58_str_t l_ret = { };
    if (a_hash)
        dap_base58_encode(a_hash->raw, sizeof(dap_hash_sha3_256_t), l_ret.s);
    return l_ret;
}
