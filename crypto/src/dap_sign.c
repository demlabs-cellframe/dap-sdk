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
#include "dap_enc_key.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_enc_base58.h"
#include "dap_json_rpc_errors.h"
#include "dap_config.h"

#define LOG_TAG "dap_sign"

static uint8_t s_sign_hash_type_default = DAP_SIGN_HASH_TYPE_SHA3;
static bool s_dap_sign_debug_more = false;

/**
 * @brief dap_sign_init
 * @param a_sign_hash_type_default Wich hash type will be used for new created signatures
 * @return
 */
int dap_sign_init(uint8_t a_sign_hash_type_default)
{
    s_sign_hash_type_default = a_sign_hash_type_default;
    s_dap_sign_debug_more = dap_config_get_item_bool_default(g_config, "sign", "debug_more", false);
    return 0;
}


/**
 * @brief get signature size (different for specific crypto algorithm)
 * 
 * @param a_key dap_enc_key_t * encryption key object
 * @param a_output_wish_size size_t output size
 * @return size_t 
 */
size_t dap_sign_create_output_unserialized_calc_size(dap_enc_key_t *a_key, UNUSED_ARG size_t a_output_wish_size )
{ 
    return dap_enc_calc_signature_unserialized_size(a_key);
}


/**
 * @brief get sign type (dap_sign_type_t) type from key type (dap_enc_key_type_t)
 * @param a_key_type dap_enc_key_type_t key type
 * @return
 */
dap_sign_type_t dap_sign_type_from_key_type( dap_enc_key_type_t a_key_type)
{
    dap_sign_type_t l_sign_type;
    memset(&l_sign_type, 0, sizeof(l_sign_type));
    switch (a_key_type){
        case DAP_ENC_KEY_TYPE_SIG_BLISS: l_sign_type.type = SIG_TYPE_BLISS; break;
        case DAP_ENC_KEY_TYPE_SIG_PICNIC: l_sign_type.type = SIG_TYPE_PICNIC; break;
        case DAP_ENC_KEY_TYPE_SIG_TESLA: l_sign_type.type = SIG_TYPE_TESLA; break;
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM: l_sign_type.type = SIG_TYPE_DILITHIUM; break;
        case DAP_ENC_KEY_TYPE_SIG_FALCON: l_sign_type.type = SIG_TYPE_FALCON; break;
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS: l_sign_type.type = SIG_TYPE_SPHINCSPLUS; break;
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA: l_sign_type.type = SIG_TYPE_ECDSA; break;
#endif
#ifdef DAP_SHIPOVNIK
        case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK: l_sign_type.type = SIG_TYPE_SHIPOVNIK; break;
#endif
        case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED: l_sign_type.type = SIG_TYPE_MULTI_CHAINED; break;
        default: l_sign_type.raw = 0;
    }
    return l_sign_type;
}

/**
 * @brief convert chain sign type (dap_sign_type_t) to encryption key type (dap_enc_key_type_t)
 * @param a_chain_sign_type dap_enc_key_type_t signature type
 * @return dap_enc_key_type_t
 */
dap_enc_key_type_t  dap_sign_type_to_key_type(dap_sign_type_t  a_chain_sign_type)
{
    switch (a_chain_sign_type.type) {
        case SIG_TYPE_BLISS: return DAP_ENC_KEY_TYPE_SIG_BLISS;
        case SIG_TYPE_TESLA: return DAP_ENC_KEY_TYPE_SIG_TESLA;
        case SIG_TYPE_PICNIC: return DAP_ENC_KEY_TYPE_SIG_PICNIC;
        case SIG_TYPE_DILITHIUM: return DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
        case SIG_TYPE_FALCON: return DAP_ENC_KEY_TYPE_SIG_FALCON;
        case SIG_TYPE_SPHINCSPLUS: return DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS;
#ifdef DAP_ECDSA
        case SIG_TYPE_ECDSA: return DAP_ENC_KEY_TYPE_SIG_ECDSA;
#endif
#ifdef DAP_SHIPOVNIK
        case SIG_TYPE_SHIPOVNIK: return DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK;
#endif
        case SIG_TYPE_MULTI_CHAINED: return DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED;
        default: return DAP_ENC_KEY_TYPE_INVALID;
    }
}



/**
 * @brief convert sign type (dap_sign_type_t) to string format
 * [sig_bliss,sig_tesla,sig_picnic,sig_dil,sig_multi2,sig_multi]
 * @param a_chain_sign_type sign type dap_sign_type_t
 * @return const char* 
 */
const char * dap_sign_type_to_str(dap_sign_type_t a_chain_sign_type)
{
    switch (a_chain_sign_type.type) {
        case SIG_TYPE_BLISS: return "sig_bliss";
        case SIG_TYPE_TESLA: return "sig_tesla";
        case SIG_TYPE_PICNIC: return "sig_picnic";
        case SIG_TYPE_DILITHIUM: return "sig_dil";
        case SIG_TYPE_FALCON: return "sig_falcon";
        case SIG_TYPE_SPHINCSPLUS: return "sig_sphincs";
#ifdef DAP_ECDSA
        case SIG_TYPE_ECDSA: return "sig_ecdsa";
#endif
#ifdef DAP_SHIPOVNIK
        case SIG_TYPE_SHIPOVNIK: return "sig_shipovnik";
#endif
        case SIG_TYPE_MULTI_COMBINED: return "sig_multi_combined";
        case SIG_TYPE_MULTI_CHAINED: return "sig_multi_chained";
        default: return "UNDEFINED";//DAP_ENC_KEY_TYPE_NULL;
    }

}

/**
 * @brief convert string to dap_sign_type_t type
 * 
 * @param a_type_str const char * algorithm type [sig_bliss,sig_tesla,sig_picnic,sig_dil,sig_multi2,sig_multi]
 * @return dap_sign_type_t 
 */
dap_sign_type_t dap_sign_type_from_str(const char * a_type_str)
{
    dap_sign_type_t l_sign_type = {0};
    if ( !dap_strcmp (a_type_str,"sig_bliss") ){
        l_sign_type.type = SIG_TYPE_BLISS;
    } else if ( !dap_strcmp (a_type_str,"sig_tesla") ){
        l_sign_type.type = SIG_TYPE_TESLA;
    } else if ( !dap_strcmp (a_type_str,"sig_picnic") ){
        l_sign_type.type = SIG_TYPE_PICNIC;
    } else if ( !dap_strcmp (a_type_str,"sig_dil") ){
        l_sign_type.type = SIG_TYPE_DILITHIUM;
    } else if ( !dap_strcmp (a_type_str, "sig_falcon") ) {
        l_sign_type.type = SIG_TYPE_FALCON;
    } else if ( !dap_strcmp (a_type_str, "sig_sphincs") ) {
         l_sign_type.type = SIG_TYPE_SPHINCSPLUS;
#ifdef DAP_ECDSA
    } else if ( !dap_strcmp (a_type_str, "sig_ecdsa") ) {
         l_sign_type.type = SIG_TYPE_ECDSA;
#endif
#ifdef DAP_SHIPOVNIK
    } else if ( !dap_strcmp (a_type_str, "sig_shipovnik") ) {
         l_sign_type.type = SIG_TYPE_SHIPOVNIK;
#endif
    } else if ( !dap_strcmp (a_type_str,"sig_multi_chained") ){
        l_sign_type.type = SIG_TYPE_MULTI_CHAINED;
    // } else if ( !dap_strcmp (a_type_str,"sig_multi_combined") ){
    //     l_sign_type.type = SIG_TYPE_MULTI_COMBINED;
    } else {
        log_it(L_WARNING, "Wrong sign type string \"%s\"", a_type_str ? a_type_str : "(null)");
    }
    return l_sign_type;
}

/**
 * @brief The function checks the signature type to see if it is outdated.
 * @param a_sign_type
 * @return bool
 */
bool dap_sign_type_is_depricated(dap_sign_type_t a_sign_type){
    if (a_sign_type.type == SIG_TYPE_PICNIC || a_sign_type.type == SIG_TYPE_BLISS || a_sign_type.type == SIG_TYPE_TESLA)
        return true;
    return false;
}

/**
 * @brief encrypt data
 * call a_key->sign_get
 * @param a_key dap_enc_key_t key object
 * @param a_data const void * data
 * @param a_data_size const size_t size of data
 * @param a_output void * output buffer
 * @param a_output_size size_t size of output buffer
 * @return int 
 */
int dap_sign_create_output(dap_enc_key_t *a_key, const void * a_data, const size_t a_data_size,
                           void * a_output, size_t *a_output_size)
{
    if(!a_key){
        log_it (L_ERROR, "Can't find the private key to create signature");
        return -1;
    }
    switch (a_key->type) {
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA:
#endif
#ifdef DAP_SHIPOVNIK
        case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK:
#endif
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED:
            return a_key->sign_get(a_key, a_data, a_data_size, a_output, *a_output_size);
        default:
            return -1;
    }
}

/**
 * @brief sign data with specified key
 * 
 * @param a_key dap_enc_key_t key object
 * @param a_data const void * buffer with data
 * @param a_data_size const size_t buffer size
 * @param a_output_wish_size size_t output buffer size
 * @return dap_sign_t* 
 */
dap_sign_t * dap_sign_create(dap_enc_key_t *a_key, const void * a_data,
        const size_t a_data_size, size_t a_output_wish_size)
{
    dap_return_val_if_fail(a_key && a_key->priv_key_data && a_key->priv_key_data_size, NULL);
    const void * l_sign_data;
    size_t l_sign_data_size;

    dap_chain_hash_fast_t l_sign_data_hash;

    if(s_sign_hash_type_default == DAP_SIGN_HASH_TYPE_NONE || a_key->type == DAP_ENC_KEY_TYPE_SIG_ECDSA) {
        l_sign_data = a_data;
        l_sign_data_size = a_data_size;
    }else{
        l_sign_data = &l_sign_data_hash;
        l_sign_data_size = sizeof(l_sign_data_hash);
        switch(s_sign_hash_type_default){
            case DAP_SIGN_HASH_TYPE_SHA3: dap_hash_fast(a_data,a_data_size,&l_sign_data_hash); break;
            default: log_it(L_CRITICAL, "We can't hash with hash type 0x%02x",s_sign_hash_type_default);
        }
    }

    // calculate max signature size
    size_t l_sign_unserialized_size = dap_sign_create_output_unserialized_calc_size(a_key, a_output_wish_size);
    if(l_sign_unserialized_size > 0) {
        size_t l_pub_key_size = 0;
        uint8_t *l_sign_unserialized = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sign_unserialized_size, NULL),
                *l_pub_key = dap_enc_key_serialize_pub_key(a_key, &l_pub_key_size);
        // calc signature [sign_size may decrease slightly]
        if( dap_sign_create_output(a_key, l_sign_data, l_sign_data_size,
                                         l_sign_unserialized, &l_sign_unserialized_size) != 0) {
            DAP_DEL_MULTY(l_sign_unserialized, l_pub_key);
            return NULL;
        } else {
            size_t l_sign_ser_size = l_sign_unserialized_size;
            uint8_t *l_sign_ser = dap_enc_key_serialize_sign(a_key->type, l_sign_unserialized, &l_sign_ser_size);
            if ( l_sign_ser ){
                dap_sign_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_sign_t, sizeof(dap_sign_hdr_t) + l_sign_ser_size + l_pub_key_size, NULL, l_sign_unserialized, l_pub_key, l_sign_ser);
                // write serialized public key to dap_sign_t
                memcpy(l_ret->pkey_n_sign, l_pub_key, l_pub_key_size);
                l_ret->header.type = dap_sign_type_from_key_type(a_key->type);
                // write serialized signature to dap_sign_t
                memcpy(l_ret->pkey_n_sign + l_pub_key_size, l_sign_ser, l_sign_ser_size);
                l_ret->header.sign_pkey_size =(uint32_t) l_pub_key_size;
                l_ret->header.sign_size = (uint32_t) l_sign_ser_size;
                l_ret->header.hash_type = s_sign_hash_type_default;

                dap_enc_key_signature_delete(a_key->type, l_sign_unserialized);
                DAP_DEL_MULTY(l_sign_ser, l_pub_key);
                return l_ret;
            } else {
                log_it(L_WARNING,"Can't serialize signature: NULL returned");
                return NULL;
            }
        }
    }
    return NULL;
}

/**
 * @brief 
 * get a_sign->pkey_n_sign + a_sign->header.sign_pkey_size
 * @param a_sign dap_sign_t object (header + raw signature data)
 * @param a_sign_out  a_sign->header.sign_size
 * @return uint8_t* 
 */
uint8_t* dap_sign_get_sign(dap_sign_t *a_sign, size_t *a_sign_size)
{
    dap_return_val_if_pass(!a_sign, NULL);

    if (a_sign_size)
        *a_sign_size = a_sign->header.sign_size;
    return a_sign->pkey_n_sign + a_sign->header.sign_pkey_size;
}

/**
 * @brief get a_sign->pkey_n_sign and a_sign->header.sign_pkey_size (optionally)
 * 
 * @param a_sign dap_sign_t sign object
 * @param a_pub_key_out [option] output pointer to a_sign->header.sign_pkey_size
 * @return uint8_t* 
 */
uint8_t* dap_sign_get_pkey(dap_sign_t *a_sign, size_t *a_pub_key_out)
{
    dap_return_val_if_pass(!a_sign, NULL);

    if(a_pub_key_out)
        *a_pub_key_out = a_sign->header.sign_pkey_size;
    return a_sign->pkey_n_sign;
}

/**
 * @brief get SHA3 hash of buffer (a_sign), storing in output buffer a_sign_hash
 * 
 * @param a_sign input buffer
 * @param a_sign_hash output buffer
 * @return true 
 * @return false 
 */
bool dap_sign_get_pkey_hash(dap_sign_t *a_sign, dap_chain_hash_fast_t *a_sign_hash)
{
    dap_return_val_if_fail(a_sign && a_sign->header.sign_pkey_size, false);
    return dap_hash_fast(a_sign->pkey_n_sign, a_sign->header.sign_pkey_size, a_sign_hash);
}

/**
 * @brief Compare two sign
 *
 * @param l_sign1
 * @param l_sign2
 * @return true or false
 */
bool dap_sign_compare_pkeys(dap_sign_t *l_sign1, dap_sign_t *l_sign2)
{
    size_t l_pkey_ser_size1 = 0, l_pkey_ser_size2 = 0;
    // Get public key from sign
    const uint8_t   *l_pkey_ser1 = dap_sign_get_pkey(l_sign1, &l_pkey_ser_size1),
                    *l_pkey_ser2 = dap_sign_get_pkey(l_sign2, &l_pkey_ser_size2);
    return (l_pkey_ser_size1 == l_pkey_ser_size2) && !memcmp(l_pkey_ser1, l_pkey_ser2, l_pkey_ser_size1);
}

/**
 * @brief verify, if a_sign->header.sign_pkey_size and a_sign->header.sign_size bigger, then a_max_key_size
 * 
 * @param a_sign signed data object 
 * @param a_max_sign_size max size of signature
 * @return true 
 * @return false 
 */
bool dap_sign_verify_size(dap_sign_t *a_sign, size_t a_max_sign_size)
{
    return (a_max_sign_size > sizeof(dap_sign_t)) && (a_sign->header.sign_size) &&
           (a_sign->header.sign_pkey_size) && (a_sign->header.type.type != SIG_TYPE_NULL) &&
           ((uint64_t)a_sign->header.sign_size + a_sign->header.sign_pkey_size + sizeof(dap_sign_t) <= (uint64_t)a_max_sign_size);
}

/**
 * @brief get deserialized pub key from dap_sign_t
 * 
 * @param a_chain_sign dap_sign_t object
 * @return dap_enc_key_t* 
 */
dap_enc_key_t *dap_sign_to_enc_key(dap_sign_t * a_chain_sign)
{
    dap_enc_key_type_t l_type = dap_sign_type_to_key_type(a_chain_sign->header.type);
    dap_return_val_if_pass(l_type == DAP_ENC_KEY_TYPE_INVALID, NULL);

    size_t l_pkey_size = 0;
    uint8_t *l_pkey = dap_sign_get_pkey(a_chain_sign, &l_pkey_size);
    dap_enc_key_t * l_ret =  dap_enc_key_new(l_type);
    // deserialize public key
    if (dap_enc_key_deserialize_pub_key(l_ret, l_pkey, l_pkey_size)) {
        log_it(L_ERROR, "Error in enc pub key deserialize");
        DAP_DEL_Z(l_ret);
    }
    return l_ret;
}

/**
 * @brief dap_sign_verify data signature
 * @param a_chain_sign dap_sign_t a_chain_sign object
 * @param a_data const void * buffer with data
 * @param a_data_size const size_t  buffer size
 * @return 0 valid signature, else invalid signature with error code
 */
int dap_sign_verify(dap_sign_t *a_chain_sign, const void *a_data, const size_t a_data_size)
{
    dap_return_val_if_pass(!a_chain_sign || !a_data, -2);

    dap_enc_key_t *l_key = dap_sign_to_enc_key(a_chain_sign);
    if ( !l_key ){
        log_it(L_WARNING,"Incorrect signature, can't extract key");
        return -3;
    }
    size_t l_sign_data_ser_size;
    uint8_t *l_sign_data_ser = dap_sign_get_sign(a_chain_sign, &l_sign_data_ser_size);

    if ( !l_sign_data_ser ){
        dap_enc_key_delete(l_key);
        log_it(L_WARNING,"Incorrect signature, can't extract serialized signature's data ");
        return -4;
    }

    size_t l_sign_data_size = a_chain_sign->header.sign_size;
    // deserialize signature
    uint8_t *l_sign_data = dap_enc_key_deserialize_sign(l_key->type, l_sign_data_ser, &l_sign_data_size);

    if ( !l_sign_data ){
        log_it(L_WARNING,"Incorrect signature, can't deserialize signature's data");
        dap_enc_key_delete(l_key);
        return -5;
    }

    int l_ret = 0;
    //uint8_t * l_sign = a_chain_sign->pkey_n_sign + a_chain_sign->header.sign_pkey_size;
    const void *l_verify_data;
    size_t l_verify_data_size;
    dap_chain_hash_fast_t l_verify_data_hash;

    if(a_chain_sign->header.hash_type == DAP_SIGN_HASH_TYPE_NONE || l_key->type == DAP_ENC_KEY_TYPE_SIG_ECDSA){
        l_verify_data = a_data;
        l_verify_data_size = a_data_size;
    }else{
        l_verify_data = &l_verify_data_hash;
        l_verify_data_size = sizeof(l_verify_data_hash);
        switch(s_sign_hash_type_default){
            case DAP_SIGN_HASH_TYPE_SHA3: dap_hash_fast(a_data,a_data_size,&l_verify_data_hash); break;
            default: log_it(L_CRITICAL, "Incorrect signature: we can't check hash with hash type 0x%02x",s_sign_hash_type_default);
            dap_enc_key_signature_delete(l_key->type, l_sign_data);
            dap_enc_key_delete(l_key);
            return -5;
        }
    }
    switch (l_key->type) {
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA:
#endif
#ifdef DAP_SHIPOVNIK
        case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK:
#endif
        case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED:
            l_ret = l_key->sign_verify(l_key, l_verify_data, l_verify_data_size, l_sign_data, l_sign_data_size);
            break;
        default:
            l_ret = -6;
    }
    dap_enc_key_signature_delete(l_key->type, l_sign_data);
    dap_enc_key_delete(l_key);
    return l_ret;
}


/**
 * @brief Get size of struct dap_sign_t
 * 
 * @param a_chain_sign dap_sign_t object
 * @return size_t 
 */
uint64_t dap_sign_get_size(dap_sign_t * a_chain_sign)
{
    if (!a_chain_sign || a_chain_sign->header.type.type == SIG_TYPE_NULL) {
        debug_if(s_dap_sign_debug_more, L_WARNING, "Sanity check error in dap_sign_get_size");
        return 0;
    }
    return (uint64_t)sizeof(dap_sign_t) + a_chain_sign->header.sign_size + a_chain_sign->header.sign_pkey_size;
}

dap_sign_t **dap_sign_get_unique_signs(void *a_data, size_t a_data_size, size_t *a_signs_count)
{
    const uint16_t l_realloc_count = 10;
    dap_return_val_if_fail(a_signs_count, NULL);
    dap_return_val_if_fail(a_data && a_data_size, (*a_signs_count = 0, NULL));
    size_t l_signs_count = *a_signs_count ? *a_signs_count : l_realloc_count;
    dap_sign_t **ret = NULL;
    uint64_t i = 0, l_sign_size = 0;
    for (uint64_t l_offset = 0; l_offset + sizeof(dap_sign_t) < a_data_size; l_offset += l_sign_size) {
        dap_sign_t *l_sign = (dap_sign_t *)((byte_t *)a_data + l_offset);
        l_sign_size = dap_sign_get_size(l_sign);
        if (l_offset + l_sign_size <= l_offset || l_offset + l_sign_size > a_data_size)
            break;
        bool l_dup = false;
        if (ret) {
            // Check duplicate signs
            for (size_t j = 0; j < i; j++) {
                if (dap_sign_compare_pkeys(l_sign, ret[j])) {
                    l_dup = true;
                    break;
                }
            }
            if (l_dup)
                continue;
        } else
            ret = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_sign_t*, l_signs_count, NULL);
        ret[i++] = l_sign;
        if (*a_signs_count && i == *a_signs_count)
            break;
        if (i == l_signs_count) {
            l_signs_count += l_realloc_count;
            dap_sign_t **l_ret_new = DAP_REALLOC_COUNT_RET_IF_FAIL(ret, l_signs_count, NULL, ret);
            ret = l_ret_new;
        }
    }
    *a_signs_count = i;
    return ret;
}

/**
 * @brief dap_sign_get_information Added in string information about signature
 * @param a_sign Signature can be NULL
 * @param a_str_out The output string pointer
 */
void dap_sign_get_information(dap_sign_t* a_sign, dap_string_t *a_str_out, const char *a_hash_out_type)
{
    dap_string_append_printf(a_str_out, "Signature: \n");
    if (!a_sign) {
        dap_string_append_printf(a_str_out, "! Corrupted signature data\n");
        return;
    }
    dap_chain_hash_fast_t l_hash_pkey;
    dap_string_append_printf(a_str_out, "\tType: %s\n",
                             dap_sign_type_to_str(a_sign->header.type));
    if(dap_sign_get_pkey_hash(a_sign, &l_hash_pkey)) {
        const char *l_hash_str = dap_strcmp(a_hash_out_type, "hex")
             ? dap_enc_base58_encode_hash_to_str_static(&l_hash_pkey)
             : dap_chain_hash_fast_to_str_static(&l_hash_pkey);
             dap_string_append_printf(a_str_out, "\tPublic key hash: %s\n", l_hash_str);
    }
    dap_string_append_printf(a_str_out, "\tPublic key size: %u\n"
                                        "\tSignature size: %u\n",
                             a_sign->header.sign_pkey_size,
                             a_sign->header.sign_size);
}

/**
 * @brief dap_sign_get_information Added in string information about signature
 * @param a_sign Signature can be NULL
 * @param a_json_out The output string pointer
 */
void dap_sign_get_information_json(json_object* a_json_arr_reply, dap_sign_t* a_sign, json_object *a_json_out, const char *a_hash_out_type)
{
    json_object_object_add(a_json_out,"Signature", json_object_new_string(""));
    if (!a_sign) {
        dap_json_rpc_error_add(a_json_arr_reply, -1, "Corrupted signature data");
        return;
    }
    dap_chain_hash_fast_t l_hash_pkey;
    json_object_object_add(a_json_out,"Type",json_object_new_string(dap_sign_type_to_str(a_sign->header.type)));
    if(dap_sign_get_pkey_hash(a_sign, &l_hash_pkey)) {
        const char *l_hash_str = dap_strcmp(a_hash_out_type, "hex")
             ? dap_enc_base58_encode_hash_to_str_static(&l_hash_pkey)
             : dap_chain_hash_fast_to_str_static(&l_hash_pkey);
             json_object_object_add(a_json_out,"Public key hash",json_object_new_string(l_hash_str));             
    }
    json_object_object_add(a_json_out,"Public key size",json_object_new_uint64(a_sign->header.sign_pkey_size));
    json_object_object_add(a_json_out,"Signature size",json_object_new_uint64(a_sign->header.sign_size));

}

/**
 * @brief return string with recommended types
 * @return string with recommended types
 */
DAP_INLINE const char *dap_sign_get_str_recommended_types(){
    return "sig_dil\nsig_falcon\n"
#ifdef DAP_ECDSA
    "sig_ecdsa\n"
#endif
#ifdef DAP_SHIPOVNIK
    "sig_shipovnik\n"
#endif
    "sig_sphincs\nsig_multi_chained\n";
}