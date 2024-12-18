/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
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
#include <string.h>
#include "dap_common.h"
#include "dap_pkey.h"
#include "dap_enc_base58.h"
#include "dap_strfuncs.h"

#define LOG_TAG "chain_key"

/**
 * @brief 
 * convert encryption key to public key
 * @param a_key dap_enc_key_t encryption key
 * @return dap_pkey_t* 
 */
dap_pkey_t *dap_pkey_from_enc_key(dap_enc_key_t *a_key)
{
    dap_return_val_if_fail(a_key, NULL);
    dap_return_val_if_fail_err(a_key->pub_key_data_size, NULL, "No public key in the input enc_key object");
    dap_pkey_type_t l_type = dap_pkey_type_from_enc_key_type(a_key->type);
    dap_return_val_if_pass_err(l_type.type == DAP_PKEY_TYPE_NULL, NULL, "Undefined pkey type");
    dap_pkey_t *l_ret = NULL;
    size_t l_pub_key_size = 0;
    uint8_t *l_pkey = dap_enc_key_serialize_pub_key(a_key, &l_pub_key_size);
    if ( l_pkey && l_pub_key_size ) {
        l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_pkey_t, sizeof(dap_pkey_t) + l_pub_key_size, NULL, l_pkey);
        *l_ret = (dap_pkey_t) { .header.type = l_type, .header.size = l_pub_key_size };
        memcpy(l_ret->pkey, l_pkey, l_pub_key_size);
    } else
        log_it(L_ERROR, "Pub key serialization failed");
    return DAP_DELETE(l_pkey), l_ret;
}

bool dap_pkey_get_hash(dap_pkey_t *a_pkey, dap_chain_hash_fast_t *a_out_hash)
{
    return a_pkey ? dap_hash_fast(a_pkey->pkey, a_pkey->header.size, a_out_hash) : false;
}

dap_pkey_t *dap_pkey_get_from_sign(dap_sign_t *a_sign)
{
    dap_return_val_if_fail(a_sign, NULL);
    dap_pkey_t *l_pkey = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_pkey_t, sizeof(dap_pkey_t) + a_sign->header.sign_pkey_size, NULL);
    *l_pkey = (dap_pkey_t) { .header.size = a_sign->header.sign_pkey_size, .header.type = dap_pkey_type_from_sign_type(a_sign->header.type) };
    memcpy(l_pkey->pkey, a_sign->pkey_n_sign, l_pkey->header.size);
    return l_pkey;
}

/**
 * @brief dap_pkey_get_hex_str
 * @param a_hash_str
 * @return pass - pointer to dap_pkey_t, error - NULL
 */
dap_pkey_t *dap_pkey_get_from_hex_str(const char *a_hex_str)
{
    dap_return_val_if_pass(!a_hex_str, NULL);
    int l_str_len = dap_strlen(a_hex_str) - 2;
    // from hex to binary 
    if (l_str_len < 1 || (!dap_strncmp(a_hex_str, "0x", 2) && !dap_is_hex_string(a_hex_str + 2, l_str_len))) {
        return NULL;
    }
    dap_pkey_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_pkey_t, l_str_len / 2 + 1, NULL);
    size_t l_out_size = dap_hex2bin((uint8_t *)l_ret, a_hex_str + 2, l_str_len);
    if (l_ret->header.type.type == DAP_PKEY_TYPE_NULL || l_out_size != dap_pkey_get_size(l_ret)) {
        log_it(L_ERROR, "Error in read pkey from hex string");
        DAP_DEL_Z(l_ret);
    }
    return l_ret;
}

/**
 * @brief dap_pkey_get_base58_str
 * @param a_base58_str
 * @return pass - pointer to dap_pkey_t, error - NULL
 */
dap_pkey_t *dap_pkey_get_from_base58_str(const char *a_base58_str)
{
    dap_return_val_if_pass(!a_base58_str, NULL);
    size_t l_str_len = dap_strlen(a_base58_str);
    // from base58 to binary 
    dap_pkey_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_pkey_t, DAP_ENC_BASE58_DECODE_SIZE(l_str_len), NULL);
    size_t l_out_size = dap_enc_base58_decode(a_base58_str, l_ret);
    if (l_ret->header.type.type == DAP_PKEY_TYPE_NULL || l_out_size != dap_pkey_get_size(l_ret)) {
        log_it(L_ERROR, "Error in read pkey from base58 string");
        DAP_DEL_Z(l_ret);
    }
    return l_ret;
}


DAP_INLINE dap_pkey_t *dap_pkey_get_from_str( const char *a_pkey_str)
{
    dap_pkey_t *l_ret = dap_pkey_get_from_hex_str(a_pkey_str);
    return  l_ret ? l_ret : dap_pkey_get_from_base58_str(a_pkey_str);
}

/**
 * @brief dap_pkey_get_hex_str
 * @param a_hash_str
 * @return pass - pointer to dap_pkey_t, error - NULL
 */
char *dap_pkey_to_hex_str(dap_pkey_t *a_pkey)
{
    size_t l_pkey_size = dap_pkey_get_size(a_pkey);
    dap_return_val_if_pass(!l_pkey_size, NULL);
    // from binary to hex 
    char *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(char, (l_pkey_size * 2) + 3, NULL);
    l_ret[0] = '0';
    l_ret[1] = 'x';
    dap_bin2hex(l_ret + 2, a_pkey, l_pkey_size);
    return l_ret;
}

/**
 * @brief dap_pkey_get_base58_str
 * @param a_base58_str
 * @return pass - pointer to dap_pkey_t, error - NULL
 */
char *dap_pkey_to_base58_str(dap_pkey_t *a_pkey)
{
    size_t l_pkey_size = dap_pkey_get_size(a_pkey);
    dap_return_val_if_pass(!l_pkey_size, NULL);
    // from binary to hex 
    char *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(char, DAP_ENC_BASE58_ENCODE_SIZE(l_pkey_size), NULL);
    dap_enc_base58_encode(a_pkey, l_pkey_size, l_ret);
    return l_ret;
}


DAP_INLINE char *dap_pkey_to_str(dap_pkey_t *a_pkey, const char *a_str_type)
{
    return  dap_strcmp(a_str_type, "hex") ? dap_pkey_to_base58_str(a_pkey) : dap_pkey_to_hex_str(a_pkey);
}