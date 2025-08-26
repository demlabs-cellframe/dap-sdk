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
#include "dap_strfuncs.h"
#include "dap_common.h"
#include "dap_hash.h"
#include "dap_enc_base58.h"
// XKCP includes moved here from header for encapsulation
#include "KeccakHash.h"
#include "hash/sha2-256/dap_sha2_256.h"

#define LOG_TAG "dap_hash"

/**
 * @brief dap_chain_str_to_hash_fast_to_str
 * @param a_hash_str
 * @param a_hash
 * @return
 */
int dap_chain_hash_fast_from_hex_str( const char *a_hex_str, dap_chain_hash_fast_t *a_hash)
{
    if (!a_hex_str || !a_hash)
        return -1;
    size_t l_hash_str_len = strlen(a_hex_str);
    if ( l_hash_str_len == DAP_CHAIN_HASH_FAST_STR_LEN && !dap_strncmp(a_hex_str, "0x", 2) && !dap_is_hex_string(a_hex_str + 2, l_hash_str_len - 2)) {
        dap_hex2bin(a_hash->raw, a_hex_str + 2, l_hash_str_len - 2);
        return  0;
    } else  { // Wrong string
        return -1;
    }
}


/**
 * @brief dap_chain_hash_fast_from_base58_str
 * @param a_base58_str
 * @param a_datum_hash
 * @return
 */
int dap_chain_hash_fast_from_base58_str(const char *a_base58_str,  dap_chain_hash_fast_t *a_hash)
{
    if (!a_hash)
        return -1;
    if (!a_base58_str)
        return -2;
    size_t l_hash_len = dap_strlen(a_base58_str);
    if (l_hash_len > DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_fast_t)))
        return -3;
    // from base58 to binary
    byte_t l_out[DAP_ENC_BASE58_DECODE_SIZE(DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_fast_t)))];
    size_t l_out_size = dap_enc_base58_decode(a_base58_str, l_out);
    if (l_out_size != sizeof(dap_hash_fast_t))
        return -4;
    memcpy(a_hash, l_out, sizeof(dap_hash_fast_t));
    return 0;
}

int dap_chain_hash_fast_from_str( const char *a_hash_str, dap_chain_hash_fast_t *a_hash)
{
    return dap_chain_hash_fast_from_hex_str(a_hash_str, a_hash) && dap_chain_hash_fast_from_base58_str(a_hash_str, a_hash);
}

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
int dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    return dap_sha2_256(a_output, a_input, a_inlen);
}
