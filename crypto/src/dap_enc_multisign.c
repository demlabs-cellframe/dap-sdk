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
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_enc_multisign.h"
#include "dap_enc_base58.h"

#include "dap_list.h"

#define LOG_TAG "dap_enc_multisign"


void dap_enc_sig_multisign_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED;
    a_key->sign_get = dap_enc_sig_multisign_get_sign;
    a_key->sign_verify = dap_enc_sig_multisign_verify_sign;
}

void dap_enc_sig_multisign_key_new_generate(dap_enc_key_t *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
// sanity check
    dap_return_if_pass(a_key->type < DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM || a_key->type > DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED || !a_kex_buf || !a_kex_size);
// memory alloc
    const dap_enc_key_type_t *l_key_types = a_kex_buf;
    dap_enc_key_t *l_keys[a_kex_size];
    for (size_t i = 0; i < a_kex_size; i++) {
        l_keys[i] = dap_enc_key_new_generate(l_key_types[i], NULL, 0, a_seed, a_seed_size, 0);
    }
    dap_multi_sign_params_t *l_params = dap_multi_sign_params_make(SIG_TYPE_MULTI_CHAINED, l_keys, a_kex_size, NULL, a_kex_size);
    dap_enc_sig_multisign_forming_keys(a_key, l_params);
    a_key->_pvt = l_params;
}

void dap_enc_sig_multisign_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_MULTY(a_key->priv_key_data, a_key->pub_key_data);
    a_key->priv_key_data_size = a_key->pub_key_data_size = 0;
    dap_multi_sign_params_delete((dap_multi_sign_params_t*)a_key->_pvt);
}

/**
 * @brief Auxiliary function to calculate multi-signature strucrutre size
 * @param a_sign The multi-signature
 * @param a_pkeys_size Size of each part
 * @param a_signes_size ...
 * @param a_pkeys_hashes_size ...
 * @return Multi-signature size, if error 0
 */
static uint64_t s_multi_sign_calc_size(const dap_multi_sign_t *a_sign, uint64_t *a_signes_size, uint64_t *a_pkeys_hashes_size)
{
    dap_return_val_if_pass(!a_sign, 0);

    uint64_t l_meta_data_size = sizeof(dap_sign_type_t) + 2 * sizeof(uint8_t) +
            a_sign->sign_count * (sizeof(uint8_t) + sizeof(dap_multi_sign_meta_t));
    uint64_t l_pkeys_hashes_size = a_sign->key_count * sizeof(dap_chain_hash_fast_t);
    uint64_t l_signes_size = 0;
    for (int i = 0; i < a_sign->sign_count; i++) {
        l_signes_size += a_sign->meta[i].sign_header.sign_size;
    }
    a_signes_size ? *a_signes_size = l_signes_size : 0;
    a_pkeys_hashes_size ? *a_pkeys_hashes_size = l_pkeys_hashes_size : 0;
    return l_meta_data_size + l_pkeys_hashes_size + l_signes_size;
}

/**
 * @brief Forming pub_key_data and priv_key_data from params
 * @param a_key updating key
 * @param a_params multisign params
 * @return 0 if pass, other if error
 */
int dap_enc_sig_multisign_forming_keys(dap_enc_key_t *a_key, const dap_multi_sign_params_t *a_params)
{
// sanity check
    dap_return_val_if_pass(!a_key || !a_params, -2);
// memory alloc
    dap_multisign_private_key_t *l_skey = NULL;
    dap_multisign_public_key_t *l_pkey = NULL;
    uint64_t l_skey_len = sizeof(uint64_t);
    uint64_t l_pkey_len = sizeof(uint64_t);
    for(size_t i = 0; i < a_params->key_count; ++i) {
        l_skey_len += dap_enc_ser_priv_key_size(a_params->keys[i]);
        l_pkey_len += dap_enc_ser_pub_key_size(a_params->keys[i]);
    }
    l_skey = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_multisign_private_key_t, l_skey_len, -1);
    l_pkey = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_multisign_public_key_t, l_pkey_len, -1, l_skey);
// func work
    uint64_t l_mem_bias_skey = 0;
    uint64_t l_mem_bias_pkey = 0;
    for(uint8_t i = 0; i < a_params->key_count; ++i) {
        uint64_t l_ser_skey_len = 0;
        uint64_t l_ser_pkey_len = 0;
        uint8_t *l_ser_skey = dap_enc_key_serialize_priv_key(a_params->keys[i], (size_t *)&l_ser_skey_len);
        uint8_t *l_ser_pkey = dap_enc_key_serialize_pub_key(a_params->keys[i], (size_t *)&l_ser_pkey_len);
        memcpy(l_skey->data + l_mem_bias_skey, l_ser_skey, l_ser_skey_len);
        memcpy(l_pkey->data + l_mem_bias_pkey, l_ser_pkey, l_ser_pkey_len);
        l_mem_bias_skey += l_ser_skey_len;
        l_mem_bias_pkey += l_ser_pkey_len;
        DAP_DEL_MULTY(l_ser_skey, l_ser_pkey);
    }
// out work
    DAP_DEL_MULTY(a_key->priv_key_data, a_key->pub_key_data);
    l_skey->len = l_skey_len;
    l_pkey->len = l_pkey_len;
    a_key->priv_key_data = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->priv_key_data_size = l_skey_len;
    a_key->pub_key_data_size = l_pkey_len;
    return 0;
}

/**
 * @brief Makes a serialization for multi-signature structure
 * @param a_sign Pointer to multi-signature
 * @param a_out_len OUT Output data lenght
 * @return Pointer to serialized data
 */
uint8_t *dap_enc_sig_multisign_write_signature(const void *a_sign, size_t *a_out_len)
{
// sanity check
    const dap_multi_sign_t *l_sign = a_sign;
    dap_return_val_if_pass(!l_sign || l_sign->type.type != SIG_TYPE_MULTI_CHAINED, NULL);
// func work
    uint64_t  l_signes_size, l_pkeys_hashes_size;
    uint64_t l_out_len = s_multi_sign_calc_size(l_sign, &l_signes_size, &l_pkeys_hashes_size) + sizeof(uint64_t) * 3;
    *a_out_len = l_out_len;
    uint8_t *l_ret = DAP_VA_SERIALIZE_NEW(l_out_len,
        &l_out_len, (uint64_t)sizeof(uint64_t),
        &l_pkeys_hashes_size, (uint64_t)sizeof(uint64_t),
        &l_signes_size, (uint64_t)sizeof(uint64_t),
        &l_sign->type, (uint64_t)sizeof(dap_sign_type_t),
        &l_sign->key_count, (uint64_t)sizeof(uint8_t),
        &l_sign->sign_count, (uint64_t)sizeof(uint8_t),
        l_sign->key_seq, (uint64_t)(sizeof(uint8_t) * l_sign->sign_count),
        l_sign->meta, (uint64_t)(sizeof(dap_multi_sign_meta_t) * l_sign->sign_count),
        l_sign->key_hashes, (uint64_t)l_pkeys_hashes_size,
        l_sign->sign_data, (uint64_t)l_signes_size
    );
    return l_ret;
}

/**
 * @brief dap_multi_sign_deserialize Makes a deserialization for multi-signature structure
 * @param a_sign Pointer to serialized data
 * @param a_sign_len Input data lenght
 * @return Pointer to multi-signature
 */
void *dap_enc_sig_multisign_read_signature(const uint8_t *a_sign, size_t a_sign_len)
{
    uint64_t l_mem_shift = sizeof(uint64_t) * 3 + sizeof(dap_sign_type_t) + sizeof(uint8_t) * 2;
    dap_return_val_if_pass(!a_sign || a_sign_len < l_mem_shift, NULL);
    dap_multi_sign_t *l_sign = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_multi_sign_t, NULL);
    uint64_t l_sign_len = 0, l_pkeys_size = 0, l_signes_size = 0, l_pkeys_hashes_size = 0;
    int l_res_des = DAP_VA_DESERIALIZE(a_sign, l_mem_shift, 
        &l_sign_len, (uint64_t)sizeof(uint64_t),
        &l_pkeys_hashes_size, (uint64_t)sizeof(uint64_t),
        &l_signes_size, (uint64_t)sizeof(uint64_t),
        &l_sign->type, (uint64_t)sizeof(dap_sign_type_t),
        &l_sign->key_count, (uint64_t)sizeof(uint8_t),
        &l_sign->sign_count, (uint64_t)sizeof(uint8_t)
    );
    if (l_res_des) {
        log_it(L_ERROR, "Multisign size deserialisation error");
        DAP_DELETE(l_sign);
        return NULL;
    }
// addtional allocation memory
    l_sign->key_seq = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sign->sign_count, NULL, l_sign);
    l_sign->meta = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_multi_sign_meta_t, l_sign->sign_count, NULL, l_sign->key_seq, l_sign);
    l_sign->key_hashes = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_hash_fast_t, l_pkeys_hashes_size, NULL, l_sign->meta, l_sign->key_seq, l_sign);
    l_sign->sign_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_signes_size, NULL, l_sign->key_hashes, l_sign->meta, l_sign->key_seq, l_sign);
// get data
    l_res_des = DAP_VA_DESERIALIZE(a_sign + l_mem_shift, l_sign_len - l_mem_shift,
        l_sign->key_seq, (uint64_t)(sizeof(uint8_t) * l_sign->sign_count),
        l_sign->meta, (uint64_t)(sizeof(dap_multi_sign_meta_t) * l_sign->sign_count),
        l_sign->key_hashes, (uint64_t)l_pkeys_hashes_size,
        l_sign->sign_data, (uint64_t)l_signes_size
    );
    if (l_res_des) {
        log_it(L_ERROR, "Multisign deserialisation error");
        DAP_DEL_MULTY(l_sign->sign_data, l_sign->key_hashes, l_sign->meta, l_sign->key_seq, l_sign);
        return NULL;
    }
    return l_sign;
}

/**
 * @brief Auxiliary function which helps fill multi-signature params structure
 * @param a_type Type of multi-signature
 * @param a_keys pointer to keys
 * @param a_key_count keys count
 * @param a_key_seq Signing keys sequence
 * @param a_sign_count Number of keys participating in multi-signing algorithm
 * @return Pointer to multi-signature params structure, if error - NULL
 */
dap_multi_sign_params_t *dap_multi_sign_params_make(dap_sign_type_enum_t a_type, dap_enc_key_t **a_keys, uint8_t a_key_count, const int *a_key_seq, uint8_t a_sign_count)
{
// sanity check
    dap_return_val_if_pass(a_type != SIG_TYPE_MULTI_CHAINED && a_type != SIG_TYPE_MULTI_COMBINED, NULL);
// memory alloc
    dap_multi_sign_params_t *l_params = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_multi_sign_params_t, NULL);
    l_params->keys = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_enc_key_t*, a_key_count, NULL, l_params);
    l_params->key_seq = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, a_sign_count, NULL, l_params->keys, l_params);
// func work
    l_params->type.type = a_type;
    l_params->key_count = a_key_count;
    l_params->sign_count = a_sign_count;
    for (int i = 0; i < a_key_count; i++) {
        l_params->keys[i] = a_keys[i];
    }
    for (uint8_t i = 0; i < a_sign_count; i++) {
        if (a_key_seq)
            l_params->key_seq[i] = a_key_seq[i];
        else
            l_params->key_seq[i] = i;
    }
    return l_params;
}

/**
 * @brief dap_multi_sign_delete Destroy multi-signature params structure
 * @param a_sign Pointer to multi-signature params structure to destroy
 * @return None
 */
void dap_multi_sign_params_delete(dap_multi_sign_params_t *a_params)
{
    if (!a_params)
        return;
    for (size_t i = 0; i < a_params->key_count; ++i) {
        dap_enc_key_delete(a_params->keys[i]);
    }
    DAP_DEL_MULTY(a_params->key_seq, a_params->keys, a_params);
}

/**
 * @brief dap_multi_sign_hash_data Make multi-signature hash for specified message
 * @param a_sign Pointer to multi-signature structure
 * @param a_data Pointer to message to be signed with this multi-signature
 * @param a_data_size Message size
 * @param a_hash OUT Pointer to calculated hash
 * @return True if success, overwise return false
 */
bool dap_multi_sign_hash_data(dap_multi_sign_t *a_sign, const void *a_data, const size_t a_data_size, dap_chain_hash_fast_t *a_hash)
{
    dap_return_val_if_pass(!a_sign, false);
    uint32_t l_meta_data_size = sizeof(dap_sign_type_t) + 2 * sizeof(uint8_t) + a_sign->sign_count * sizeof(uint8_t);
    uint8_t l_meta_data[l_meta_data_size], l_concatenated_hash[ 3 * sizeof(dap_chain_hash_fast_t) ] = { };
    
    bool l_ret = !!DAP_VA_SERIALIZE(l_meta_data, l_meta_data_size,
                                       &a_sign->type, (uint64_t)sizeof(dap_sign_type_t),
                                       &a_sign->key_count, (uint64_t)sizeof(uint8_t),
                                       &a_sign->sign_count, (uint64_t)sizeof(uint8_t),
                                       a_sign->key_seq, (uint64_t)(a_sign->sign_count * sizeof(uint8_t)));
    l_ret ? l_ret &= dap_hash_fast(a_data, a_data_size, (dap_chain_hash_fast_t *)l_concatenated_hash) : 0;  // get data hash
    l_ret ? l_ret &= dap_hash_fast(l_meta_data, l_meta_data_size, (dap_chain_hash_fast_t*)(l_concatenated_hash + sizeof(dap_chain_hash_fast_t))) : 0;  // get metadata hash
    l_ret ? l_ret &= dap_hash_fast(a_sign->key_hashes, a_sign->key_count * sizeof(dap_chain_hash_fast_t), (dap_chain_hash_fast_t *)(l_concatenated_hash + 2 * sizeof(dap_chain_hash_fast_t))) : 0;   // get key_hashes hash
    l_ret ? l_ret &= dap_hash_fast(l_concatenated_hash, 3 * sizeof(dap_chain_hash_fast_t), a_hash) : 0;  // get out hash of calculated hashes
    return l_ret;
}

/**
 * @brief dap_multi_sign_create Make multi-signature for specified message
 * @param a_params Pointer to multi-signature params structure
 * @param a_data Pointer to message to be signed with this multi-signature
 * @param a_data_size Message size
 * @return Pointer to multi-signature structure for specified message
 */
int dap_enc_sig_multisign_get_sign(dap_enc_key_t *a_key, const void *a_msg_in, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max)
{
// sanity check
    dap_multi_sign_params_t *l_params = a_key->_pvt;
    dap_return_val_if_pass(!l_params || !l_params->key_count || l_params->type.type != SIG_TYPE_MULTI_CHAINED, -1);
// memory alloc
    dap_multi_sign_t *l_sign = a_sign_out;
    l_sign->key_hashes = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_chain_hash_fast_t, l_params->key_count, -6);
    l_sign->key_seq = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_params->sign_count, -4, l_sign->key_hashes);
    l_sign->meta = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_multi_sign_meta_t, l_params->sign_count, -5, l_sign->key_seq, l_sign->key_hashes);
// data prepare
    l_sign->type = l_params->type;
    l_sign->key_count = l_params->key_count;
    l_sign->sign_count = l_params->sign_count;

    for (int i = 0; i < l_params->key_count; i++) {
        if (!dap_hash_fast(l_params->keys[i]->pub_key_data, l_params->keys[i]->pub_key_data_size, &l_sign->key_hashes[i])) {
            log_it (L_ERROR, "Can't create multi-signature hash");
            DAP_DEL_MULTY(l_sign->key_hashes, l_sign->key_seq, l_sign->meta);
            return -3;
        }
    }
    // need to forming metadata
    for (int i = 0; i < l_params->sign_count; i++) {
        l_sign->key_seq[i] = l_params->key_seq[i];
    }
    size_t l_signs_mem_shift = 0;
    size_t l_sign_size = 0;
    for (uint8_t i = 0; i < l_params->sign_count; ++i) {
        bool l_hashed = false;
        dap_chain_hash_fast_t l_data_hash;
        if (i == 0) {
             l_hashed = dap_multi_sign_hash_data(l_sign, a_msg_in, a_msg_size, &l_data_hash);
        } else {
             l_hashed = dap_hash_fast(&l_sign->sign_data[l_signs_mem_shift], l_sign_size, &l_data_hash);
             l_signs_mem_shift += l_sign_size;
        }
        if (!l_hashed) {
            log_it (L_ERROR, "Can't create multi-signature hash");
            DAP_DEL_MULTY(l_sign->key_hashes, l_sign->key_seq, l_sign->meta, l_sign->sign_data);
            return -4;
        }
        int l_num = l_sign->key_seq[i];
        dap_sign_t *l_dap_sign_step = dap_sign_create(l_params->keys[l_num], &l_data_hash, sizeof(dap_chain_hash_fast_t));
        if (!l_dap_sign_step) {
            log_it (L_ERROR, "Can't create multi-signature step signature");
            DAP_DEL_MULTY(l_sign->key_hashes, l_sign->key_seq, l_sign->meta, l_sign->sign_data);
            return -5;
        }
        uint8_t *l_sign_step = dap_sign_get_sign(l_dap_sign_step, &l_sign_size);
        l_sign->meta[i].sign_header = l_dap_sign_step->header;
        if (l_signs_mem_shift == 0) {
            l_sign->sign_data = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
        } else {
            l_sign->sign_data = DAP_REALLOC(l_sign->sign_data, l_signs_mem_shift + l_sign_size);
        }
        memcpy(l_sign->sign_data + l_signs_mem_shift, l_sign_step, l_sign_size);
        DAP_DELETE(l_dap_sign_step);
    }
// out work
    return 0;
}

/**
 * @brief dap_multi_sign_verify Make verification test for multi-signed message
 * @param a_sign Pointer to multi-signature structure
 * @param a_data Pointer to message signed with this multi-signature
 * @param a_data_size Signed message size
 * @return 0 valid signature, other verification error
 */
int dap_enc_sig_multisign_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sign,
        const size_t a_sign_size)
{
    dap_return_val_if_pass(!a_sign || !a_msg, -1);
    dap_multi_sign_t *l_sign = a_sign;
    if (l_sign->type.type != SIG_TYPE_MULTI_CHAINED) {
        log_it (L_ERROR, "Unsupported multi-signature type");
        return -1;
    }
    if (!l_sign->sign_data || !l_sign->key_hashes || !l_sign->meta || !l_sign->key_seq) {
        log_it (L_ERROR, "Invalid multi-signature format");
        return -2;
    }

    uint32_t l_pkeys_mem_shift = 0, l_signs_mem_shift = 0;
    for (uint8_t i = 0; i < l_sign->sign_count; ++i) {
        dap_chain_hash_fast_t l_data_hash;
        dap_multisign_public_key_t *l_pkeys = a_key->pub_key_data;
        size_t l_pkey_size = l_sign->meta[i].sign_header.sign_pkey_size;
        size_t l_sign_size = l_sign->meta[i].sign_header.sign_size;
        dap_sign_t *l_step_sign = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_sign_t, sizeof(dap_sign_hdr_t) + l_pkey_size + l_sign_size, -1);
        int l_verified = 0;
        // get multisign hash data
        if (!i && !dap_multi_sign_hash_data(l_sign, a_msg, a_msg_size, &l_data_hash)) {
            log_it (L_ERROR, "Can't create multi-signature hash");
            return DAP_DELETE(l_step_sign), -3;
        }
        l_step_sign->header = l_sign->meta[i].sign_header;
        memcpy(l_step_sign->pkey_n_sign, l_pkeys->data + l_pkeys_mem_shift, l_pkey_size);
        memcpy(l_step_sign->pkey_n_sign + l_pkey_size, l_sign->sign_data + l_signs_mem_shift, l_sign_size);
        l_verified = dap_sign_verify(l_step_sign, &l_data_hash, sizeof(dap_chain_hash_fast_t));
        DAP_DELETE(l_step_sign);
        // verify check
        if (l_verified) {
            return l_verified;
        }
        // get past hash
        if (!dap_hash_fast(l_sign->sign_data + l_signs_mem_shift, l_sign_size, &l_data_hash)) {
            log_it (L_ERROR, "Can't create multi-signature hash");
            return -4;
        }
        l_signs_mem_shift += l_sign_size;
        l_pkeys_mem_shift += l_sign->meta[i].sign_header.sign_pkey_size;
    }
    return 0;
}

/**
 * @brief dap_multi_sign_delete Destroy multi-signature structure
 * @param a_sign Pointer to multi-signature structure to destroy
 * @return None
 */
void dap_multi_sign_delete(void *a_sign)
{
    dap_return_if_pass(!a_sign);
    dap_multi_sign_t *l_sign = (dap_multi_sign_t*)a_sign;
    DAP_DEL_MULTY(l_sign->sign_data, l_sign->key_hashes, l_sign->meta, l_sign->key_seq);

}
