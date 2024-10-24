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

#define LOG_TAG "chain_key"
//static dap_pkey_t m_dap_pkey_null={0}; // For sizeof nothing more

/**
 * @brief 
 * convert encryption key to public key
 * @param a_key dap_enc_key_t encryption key
 * @return dap_pkey_t* 
 */
dap_pkey_t *dap_pkey_from_enc_key(dap_enc_key_t *a_key)
{
    if (a_key->pub_key_data_size > 0) {
        dap_pkey_type_t l_type = dap_pkey_type_from_enc_key_type(a_key->type);
        if (l_type.type == DAP_PKEY_TYPE_NULL) {
            log_it(L_WARNING, "No serialization preset");
            return NULL;
        }
        size_t l_pub_key_size;
        uint8_t *l_pkey = dap_enc_key_serialize_pub_key(a_key, &l_pub_key_size);
        if (!l_pkey) {
            log_it(L_WARNING, "Serialization failed");
            return NULL;
        }
        dap_pkey_t *l_ret = DAP_NEW_SIZE(dap_pkey_t, sizeof(dap_pkey_t) + l_pub_key_size);
        l_ret->header.type = l_type;
        l_ret->header.size = (uint32_t)l_pub_key_size;
        memcpy(&l_ret->pkey, l_pkey, l_pub_key_size);
        DAP_DELETE(l_pkey);
        return l_ret;
    } else {
        log_it(L_WARNING, "No public key in the input enc_key object");
        return NULL;
    }
    return NULL;
}

bool dap_pkey_get_hash(dap_pkey_t *a_pkey, dap_chain_hash_fast_t *a_out_hash)
{
    if (!a_pkey || !a_out_hash)
        return false;
    return dap_hash_fast(a_pkey->pkey, a_pkey->header.size, a_out_hash);
}

dap_pkey_t *dap_pkey_get_from_sign(dap_sign_t *a_sign)
{
    dap_pkey_t *l_pkey = DAP_NEW_SIZE(dap_pkey_t, sizeof(dap_pkey_t) + a_sign->header.sign_pkey_size);
    l_pkey->header.size = a_sign->header.sign_pkey_size;
    l_pkey->header.type = dap_pkey_type_from_sign_type(a_sign->header.type);
    memcpy(l_pkey->pkey, a_sign->pkey_n_sign, l_pkey->header.size);
    return l_pkey;
}
