/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
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
#include <time.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_enc_base58.h"

#include "dap_config.h"
#include "dap_pkey.h"
#include "dap_enc_chipmunk.h"  // For Chipmunk implementation
#include "dap_enc_chipmunk_ring.h"  // For Chipmunk ring signatures
#include "chipmunk/chipmunk_ring.h"  // For ring signature structures
#include "chipmunk/chipmunk_aggregation.h"  // For aggregation functions
#include "dap_enc_dilithium.h"
#include "dilithium_params.h"

#define LOG_TAG "dap_sign"

static uint8_t s_sign_hash_type_default = DAP_SIGN_HASH_TYPE_SHA3;
static bool s_debug_more = true;
static dap_sign_callback_t s_get_pkey_by_hash_callback = NULL;

// Static function declarations for internal implementations
static dap_sign_t *dap_sign_chipmunk_aggregate_signatures_internal(
    dap_sign_t **a_signatures,
    uint32_t a_signatures_count,
    const void *a_message,
    size_t a_message_size,
    const dap_sign_aggregation_params_t *a_params);

static int dap_sign_chipmunk_verify_aggregated_internal(
    dap_sign_t *a_aggregated_sign,
    const void **a_messages,
    const size_t *a_message_sizes,
    dap_pkey_t **a_public_keys,
    uint32_t a_signers_count);

static int dap_sign_chipmunk_batch_verify_execute_internal(dap_sign_batch_verify_ctx_t *a_ctx);
static int dap_sign_dilithium_batch_verify_execute_internal(dap_sign_batch_verify_ctx_t *a_ctx);

/**
 * @brief dap_sign_init
 * @param a_sign_hash_type_default Wich hash type will be used for new created signatures
 * @return
 */
int dap_sign_init(uint8_t a_sign_hash_type_default)
{
    s_sign_hash_type_default = a_sign_hash_type_default;
    s_debug_more = dap_config_get_item_bool_default(g_config, "sign", "debug_more", false);
    return 0;
}


/**
 * @brief get signature size (different for specific crypto algorithm)
 * 
 * @param a_key dap_enc_key_t * encryption key object
 * @return size_t 
 */
DAP_INLINE size_t dap_sign_create_output_unserialized_calc_size(dap_enc_key_t *a_key)
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
        case DAP_ENC_KEY_TYPE_SIG_ML_DSA: l_sign_type.type = SIG_TYPE_ML_DSA; break;
        case DAP_ENC_KEY_TYPE_SIG_FALCON: l_sign_type.type = SIG_TYPE_FALCON; break;
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS: l_sign_type.type = SIG_TYPE_SPHINCSPLUS; break;
        case DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME: l_sign_type.type = SIG_TYPE_NTRU_PRIME; break;
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK: l_sign_type.type = SIG_TYPE_CHIPMUNK; break;
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING: l_sign_type.type = SIG_TYPE_CHIPMUNK_RING; break;
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA: l_sign_type.type = SIG_TYPE_ECDSA; break;
        case DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM: l_sign_type.type = SIG_TYPE_MULTI_ECDSA_DILITHIUM; break;
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
        case SIG_TYPE_ML_DSA: return DAP_ENC_KEY_TYPE_SIG_ML_DSA;
        case SIG_TYPE_FALCON: return DAP_ENC_KEY_TYPE_SIG_FALCON;
        case SIG_TYPE_SPHINCSPLUS: return DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS;
        case SIG_TYPE_NTRU_PRIME: return DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME;
        case SIG_TYPE_CHIPMUNK: return DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
        case SIG_TYPE_CHIPMUNK_RING: return DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING;
#ifdef DAP_ECDSA
        case SIG_TYPE_ECDSA: return DAP_ENC_KEY_TYPE_SIG_ECDSA;
        case SIG_TYPE_MULTI_ECDSA_DILITHIUM: return DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM;
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
        case SIG_TYPE_ML_DSA: return "sig_ml_dsa";
        case SIG_TYPE_FALCON: return "sig_falcon";
        case SIG_TYPE_SPHINCSPLUS: return "sig_sphincs";
        case SIG_TYPE_NTRU_PRIME: return "sig_ntru_prime";
        case SIG_TYPE_CHIPMUNK: return "sig_chipmunk";
        case SIG_TYPE_CHIPMUNK_RING: return "sig_chipmunk_ring";
#ifdef DAP_ECDSA
        case SIG_TYPE_ECDSA: return "sig_ecdsa";
        case SIG_TYPE_MULTI_ECDSA_DILITHIUM: return "sig_multi_ecdsa_dil";
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
    } else if ( !dap_strcmp (a_type_str, "sig_ml_dsa") ){
        l_sign_type.type = SIG_TYPE_ML_DSA;
    } else if ( !dap_strcmp (a_type_str, "sig_falcon") ) {
        l_sign_type.type = SIG_TYPE_FALCON;
    } else if ( !dap_strcmp (a_type_str, "sig_sphincs") ) {
         l_sign_type.type = SIG_TYPE_SPHINCSPLUS;
    } else if ( !dap_strcmp (a_type_str, "sig_ntru_prime") ) {
         l_sign_type.type = SIG_TYPE_NTRU_PRIME;
    } else if ( !dap_strcmp (a_type_str, "sig_chipmunk") ) {
         l_sign_type.type = SIG_TYPE_CHIPMUNK;
    } else if ( !dap_strcmp (a_type_str, "sig_chipmunk_ring") ) {
         l_sign_type.type = SIG_TYPE_CHIPMUNK_RING;
#ifdef DAP_ECDSA
    } else if ( !dap_strcmp (a_type_str, "sig_ecdsa") ) {
         l_sign_type.type = SIG_TYPE_ECDSA;
    } else if ( !dap_strcmp (a_type_str,"sig_multi_ecdsa_dil") ){
        l_sign_type.type = SIG_TYPE_MULTI_ECDSA_DILITHIUM;
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
bool dap_sign_type_is_deprecated(dap_sign_type_t a_sign_type){
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
        case DAP_ENC_KEY_TYPE_SIG_ML_DSA:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME:
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK:
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING:
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM:
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
 * @brief sign data with specified key with choosed hash type
 * @param a_key dap_enc_key_t key object
 * @param a_data const void * buffer with data
 * @param a_data_size const size_t buffer size
 * @param a_hash_type data and pkey hash type
 * @return dap_sign_t* 
 */
dap_sign_t *dap_sign_create_with_hash_type(dap_enc_key_t *a_key, const void * a_data,
        const size_t a_data_size, uint32_t a_hash_type)
{
    dap_return_val_if_fail(a_key && a_key->priv_key_data && a_key->priv_key_data_size, NULL);
    const void *l_sign_data = NULL;
    size_t l_sign_data_size = 0;
    dap_hash_sha3_256_t l_sign_data_hash = {};
    uint32_t l_hash_type = DAP_SIGN_REMOVE_PKEY_HASHING_FLAG(a_hash_type);
    bool l_use_pkey_hash = DAP_SIGN_GET_PKEY_HASHING_FLAG(a_hash_type);
    if (dap_enc_key_is_insign_hashing(a_key->type)) {
        if (l_hash_type != DAP_SIGN_HASH_TYPE_SIGN && l_hash_type != DAP_SIGN_HASH_TYPE_DEFAULT)
            log_it(L_WARNING, "%s enc key use insign hashing, hash type change to DAP_SIGN_HASH_TYPE_SIGN (0x%02x)", dap_enc_get_type_name(a_key->type), DAP_SIGN_HASH_TYPE_SIGN);
        l_hash_type = DAP_SIGN_HASH_TYPE_SIGN;
    } else {
        if (l_hash_type == DAP_SIGN_HASH_TYPE_SIGN) {
            log_it(L_WARNING, "%s enc key not use insign hashing, hash type change to default (0x%02x)", dap_enc_get_type_name(a_key->type), s_sign_hash_type_default);
            l_hash_type = s_sign_hash_type_default;
        }
        if (l_hash_type == DAP_SIGN_HASH_TYPE_DEFAULT)
            l_hash_type = s_sign_hash_type_default;
    }
    dap_return_val_if_pass_err(l_use_pkey_hash && l_hash_type == DAP_SIGN_HASH_TYPE_NONE, NULL, "Sign with DAP_PKEY_HASHING_FLAG can't have DAP_SIGN_HASH_TYPE_NONE (0x00)");

    if(l_hash_type == DAP_SIGN_HASH_TYPE_NONE || l_hash_type == DAP_SIGN_HASH_TYPE_SIGN) {
        l_sign_data = a_data;
        l_sign_data_size = a_data_size;
    } else {
        l_sign_data = &l_sign_data_hash;
        l_sign_data_size = sizeof(l_sign_data_hash);
        switch(l_hash_type){
            case DAP_SIGN_HASH_TYPE_SHA3: dap_hash_sha3_256(a_data,a_data_size,&l_sign_data_hash); break;
            default: log_it(L_CRITICAL, "We can't hash with hash type 0x%02x", l_hash_type);
        }
    }

    // calculate max signature size
    size_t l_sign_unserialized_size = dap_sign_create_output_unserialized_calc_size(a_key);
    if(l_sign_unserialized_size > 0) {
        size_t l_pub_key_size = 0;
        uint8_t *l_sign_unserialized = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sign_unserialized_size, NULL),
                *l_pub_key = NULL;   
        if (l_use_pkey_hash) {
            l_pub_key_size = DAP_HASH_SHA3_256_SIZE;
            l_pub_key = DAP_NEW_Z_SIZE(uint8_t, l_pub_key_size);
            dap_enc_key_get_pkey_hash(a_key, DAP_HASH_TYPE_SHA3_256, l_pub_key, l_pub_key_size);
        } else {
            l_pub_key = dap_enc_key_serialize_pub_key(a_key, &l_pub_key_size);
        }
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
                l_ret->header.hash_type = l_use_pkey_hash ? DAP_SIGN_ADD_PKEY_HASHING_FLAG(l_hash_type) : l_hash_type;

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
 * @brief Create a ring signature using Chipmunk_Ring
 */
dap_sign_t *dap_sign_create_ring(
    dap_enc_key_t *a_signer_key,
    const void *a_data,
    size_t a_data_size,
    dap_enc_key_t **a_ring_keys,
    size_t a_ring_size,
    uint32_t a_required_signers
) {
    debug_if(s_debug_more, L_DEBUG, "dap_sign_create_ring ENTRY: ring_size=%zu, required_signers=%u", 
             a_ring_size, a_required_signers);
    dap_return_val_if_fail(a_signer_key, NULL);
    // Allow empty messages (a_data can be NULL if a_data_size is 0)
    dap_return_val_if_fail(a_data || a_data_size == 0, NULL);
    dap_return_val_if_fail(a_ring_keys, NULL);
    dap_return_val_if_fail(a_ring_size >= 2, NULL);
    dap_return_val_if_fail(a_required_signers >= 1, NULL);
    dap_return_val_if_fail(a_required_signers <= a_ring_size, NULL);
    // ANONYMITY: No signer index validation needed for anonymous signatures

    // Verify all ring keys are of correct type (only CHIPMUNK_RING for ring signatures)
    for (size_t i = 0; i < a_ring_size; i++) {
        dap_return_val_if_fail(a_ring_keys[i], NULL);
        dap_return_val_if_fail(a_ring_keys[i]->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL);
    }

    // Verify signer key type (only CHIPMUNK_RING for ring signatures)
    dap_return_val_if_fail(a_signer_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL);

    // Calculate signature size with all parameters
    bool l_use_embedded_keys = true;  // Always use embedded keys for dap_sign interface
    size_t l_signature_size = dap_enc_chipmunk_ring_get_signature_size(a_ring_size, a_required_signers, l_use_embedded_keys);
    debug_if(s_debug_more, L_DEBUG, "Ring signature size for ring_size=%zu, required_signers=%u: %zu", a_ring_size, a_required_signers, l_signature_size);
    dap_return_val_if_fail(l_signature_size > 0, NULL);

    // Allocate signature buffer
    debug_if(s_debug_more, L_INFO, "Allocating signature buffer of size %zu", l_signature_size);
    uint8_t *l_signature_data = DAP_NEW_Z_SIZE(uint8_t, l_signature_size);
    dap_return_val_if_fail(l_signature_data, NULL);
    debug_if(s_debug_more, L_INFO, "Signature buffer allocated successfully");

    // Extract public keys from ring keys
    uint8_t **l_ring_pub_keys = DAP_NEW_Z_COUNT(uint8_t*, a_ring_size);
    if (!l_ring_pub_keys) {
        debug_if(s_debug_more, L_ERROR, "Failed to allocate ring public keys array");
        DAP_DELETE(l_signature_data);
        return NULL;
    }
    debug_if(s_debug_more, L_INFO, "Ring public keys array allocated successfully, size: %zu", a_ring_size);

    for (size_t i = 0; i < a_ring_size; i++) {
        if (!a_ring_keys[i] || !a_ring_keys[i]->pub_key_data) {
            log_it(L_ERROR, "Ring key %zu is NULL or has no public key data", i);
            DAP_DELETE(l_ring_pub_keys);
            DAP_DELETE(l_signature_data);
            return NULL;
        }
        if (a_ring_keys[i]->pub_key_data_size != CHIPMUNK_RING_PUBLIC_KEY_SIZE) {
            log_it(L_ERROR, "Ring key %zu has wrong size: expected %d, got %zu",
                   i, (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE, a_ring_keys[i]->pub_key_data_size);
            DAP_DELETE(l_ring_pub_keys);
            DAP_DELETE(l_signature_data);
            return NULL;
        }
        l_ring_pub_keys[i] = a_ring_keys[i]->pub_key_data;
    }

    // Create ring signature with required_signers support
    int l_result = dap_enc_chipmunk_ring_sign(
        a_signer_key->priv_key_data,
        a_data,
        a_data_size,
        l_ring_pub_keys,
        a_ring_size,
        a_required_signers,
        l_signature_data,
        l_signature_size
    );

    DAP_DELETE(l_ring_pub_keys);

    if (l_result != 0) {
        log_it(L_ERROR, "Failed to create Chipmunk ring signature");
        DAP_DELETE(l_signature_data);
        return NULL;
    }

    // Create dap_sign_t structure
    size_t l_total_size = sizeof(dap_sign_hdr_t) + l_signature_size;
    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t, l_total_size);

    if (!l_sign) {
        log_it(L_CRITICAL, "Failed to allocate memory for ring signature");
        DAP_DELETE(l_signature_data);
        return NULL;
    }

    // Set signature header
    l_sign->header.type.type = SIG_TYPE_CHIPMUNK_RING;
    l_sign->header.hash_type = DAP_SIGN_HASH_TYPE_DEFAULT;
    l_sign->header.sign_pkey_size = 0; // Ring signatures don't include individual public key
    l_sign->header.sign_size = (uint32_t)l_signature_size;

    // Copy signature data
    memcpy(l_sign->pkey_n_sign, l_signature_data, l_signature_size);

    DAP_DELETE(l_signature_data);

    return l_sign;
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
 * @param a_sign dap_sign_t sign object
 * @param a_pub_key_out [option] output pointer to a_sign->header.sign_pkey_size
 * @return uint8_t* 
 */
uint8_t *dap_sign_get_pkey(dap_sign_t *a_sign, size_t *a_pub_key_out)
{
    dap_return_val_if_pass(!a_sign, NULL);
    if (a_pub_key_out) {
        *a_pub_key_out = a_sign->header.sign_pkey_size;
        printf("DEBUG: sign_pkey_size = %zu, pkey_n_sign = %p\n", *a_pub_key_out, a_sign->pkey_n_sign);
        fflush(stdout);
    }
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
bool dap_sign_get_pkey_hash(dap_sign_t *a_sign, dap_hash_sha3_256_t *a_sign_hash)
{
    dap_return_val_if_fail(a_sign && a_sign->header.sign_pkey_size, false);
    if (DAP_SIGN_GET_PKEY_HASHING_FLAG(a_sign->header.hash_type)) {
        if (a_sign->header.sign_pkey_size > DAP_HASH_SHA3_256_SIZE) {
            log_it(L_ERROR, "Error in pkey size check, expected <= %zu, in sign %u", sizeof(dap_hash_sha3_256_t), a_sign->header.sign_pkey_size);
            return false;
        }
        return memcpy(a_sign_hash, a_sign->pkey_n_sign, a_sign->header.sign_pkey_size) ? true : false;
    }
    return  dap_hash_sha3_256(a_sign->pkey_n_sign, a_sign->header.sign_pkey_size, a_sign_hash);
}

/**
 * @brief Compare two sign
 *
 * @param l_sign1
 * @param l_sign2
 * @return true or false
 */
bool dap_sign_compare_pkeys(dap_sign_t *a_sign1, dap_sign_t *a_sign2)
{
    dap_return_val_if_fail(a_sign1 && a_sign2, false);
    if (a_sign1->header.type.type != a_sign2->header.type.type)
        return false;
    size_t l_pkey_ser_size1 = 0, l_pkey_ser_size2 = 0;
    // Get public key from sign
    const uint8_t   *l_pkey_ser1 = dap_sign_get_pkey(a_sign1, &l_pkey_ser_size1),
                    *l_pkey_ser2 = dap_sign_get_pkey(a_sign2, &l_pkey_ser_size2);
    return (l_pkey_ser_size1 == l_pkey_ser_size2) && !memcmp(l_pkey_ser1, l_pkey_ser2, l_pkey_ser_size1);
}


/**
 * @brief get deserialized pub key from dap_sign_t
 * @param a_chain_sign dap_sign_t object
 * @param a_pkey
 * @return dap_enc_key_t* 
 */
dap_enc_key_t *dap_sign_to_enc_key_by_pkey(dap_sign_t *a_chain_sign, dap_pkey_t *a_pkey)
{
    dap_return_val_if_pass(!a_chain_sign, NULL);
    // Additional validation for signature structure
    dap_return_val_if_pass(a_chain_sign->header.sign_size == 0 && a_chain_sign->header.sign_pkey_size == 0, NULL);
    dap_enc_key_type_t l_type = dap_sign_type_to_key_type(a_chain_sign->header.type);
    dap_return_val_if_pass(l_type == DAP_ENC_KEY_TYPE_INVALID, NULL);

    // Special handling for ring signatures that don't include individual public keys
    if (a_chain_sign->header.type.type == SIG_TYPE_CHIPMUNK_RING) {
        if (a_pkey) {
            // Use provided public key for ring signature verification
            dap_enc_key_t *l_ret = dap_enc_key_new(l_type);
            if (dap_enc_key_deserialize_pub_key(l_ret, a_pkey->pkey, a_pkey->header.size)) {
                log_it(L_ERROR, "Error in enc pub key deserialize for ring signature");
                DAP_DEL_Z(l_ret);
            }
            return l_ret;
        } else {
            log_it(L_ERROR, "Ring signatures require external public key for verification");
            return NULL;
        }
    }

    size_t l_pkey_size = a_pkey ? a_pkey->header.size : 0;
    uint8_t *l_pkey = a_pkey ? a_pkey->pkey : dap_sign_get_pkey(a_chain_sign, &l_pkey_size);
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
 * @param a_pkey pkey to verofy sign
 * @return 0 valid signature, else invalid signature with error code
 */
int dap_sign_verify_by_pkey(dap_sign_t *a_chain_sign, const void *a_data, const size_t a_data_size, dap_pkey_t *a_pkey)
{
    dap_return_val_if_pass(!a_chain_sign || !a_data, -2);

    dap_enc_key_t *l_key = dap_sign_to_enc_key_by_pkey(a_chain_sign, a_pkey);
    if ( !l_key ){
        log_it(L_WARNING,"Incorrect signature, can't extract key");
        return -3;
    }
    size_t l_sign_data_ser_size = 0;
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
    const void *l_verify_data;
    size_t l_verify_data_size;
    dap_hash_sha3_256_t l_verify_data_hash;
    uint32_t l_hash_type = DAP_SIGN_REMOVE_PKEY_HASHING_FLAG(a_chain_sign->header.hash_type);
    if(l_hash_type == DAP_SIGN_HASH_TYPE_DEFAULT)
        log_it(L_WARNING, "Detected DAP_SIGN_HASH_TYPE_DEFAULT (0x%02x) hash type in sign ", DAP_SIGN_HASH_TYPE_DEFAULT);

    if(l_hash_type == DAP_SIGN_HASH_TYPE_NONE || l_hash_type == DAP_SIGN_HASH_TYPE_SIGN){
        l_verify_data = a_data;
        l_verify_data_size = a_data_size;
    } else {
        l_verify_data = &l_verify_data_hash;
        l_verify_data_size = DAP_HASH_SHA3_256_SIZE;
        switch(l_hash_type){
            case DAP_SIGN_HASH_TYPE_SHA3: dap_hash_sha3_256(a_data,a_data_size,&l_verify_data_hash); break;
            default: log_it(L_CRITICAL, "Incorrect signature: we can't check hash with hash type 0x%02x", s_sign_hash_type_default);
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
        case DAP_ENC_KEY_TYPE_SIG_ML_DSA:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        case DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME:
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK:
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM:
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
        debug_if(s_debug_more, L_WARNING, "Sanity check error in dap_sign_get_size");
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
            dap_sign_t **l_ret_new = DAP_REALLOC_COUNT_RET_VAL_IF_FAIL(ret, l_signs_count, NULL, ret);
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
    dap_hash_sha3_256_t l_hash_pkey;
    dap_string_append_printf(a_str_out, "\tType: %s\n",
                             dap_sign_type_to_str(a_sign->header.type));
    if(dap_sign_get_pkey_hash(a_sign, &l_hash_pkey)) {
        const char *l_hash_str = dap_strcmp(a_hash_out_type, "hex")
             ? dap_enc_base58_encode_hash_to_str_static(&l_hash_pkey)
             : dap_hash_sha3_256_to_str_static(&l_hash_pkey);
             dap_string_append_printf(a_str_out, "\tPublic key hash: %s\n", l_hash_str);
    }
    dap_string_append_printf(a_str_out, "\tPublic key size: %u\n"
                                        "\tSignature size: %u\n",
                             a_sign->header.sign_pkey_size,
                             a_sign->header.sign_size);
}


/**
 * @brief return string with recommended types
 * @return string with recommended types
 */
DAP_INLINE const char *dap_sign_get_str_recommended_types()
{
    return "sig_dil\nsig_falcon\nsig_ntru_prime\n"
#ifdef DAP_ECDSA
    "sig_ecdsa\n"
    "sig_multi_ecdsa_dil\n"
#endif
#ifdef DAP_SHIPOVNIK
    "sig_shipovnik\n"
#endif
    "sig_sphincs\nsig_multi_chained\n";
}

/**
 * @brief init callback to search pkey by hash
 * @return if pass 0, other - error
 */
int dap_sign_set_pkey_by_hash_callback(dap_sign_callback_t a_callback)
{
    dap_return_val_if_pass_err(s_get_pkey_by_hash_callback, -1, "s_get_pkey_by_hash_callback already inited");
    s_get_pkey_by_hash_callback = a_callback;
    return 0;
}

// === Chipmunk Aggregated Signatures Implementation ===

// === Universal Extended Signature Operations Implementation ===

// Helper function to check if signature type supports aggregation
bool dap_sign_type_supports_aggregation(dap_sign_type_t a_signature_type)
{
    switch (a_signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
        case SIG_TYPE_CHIPMUNK_RING:
            return true;
        // Add other aggregation-capable signature types here
        default:
            return false;
    }
}

// Helper function to check if signature type supports batch verification
bool dap_sign_type_supports_batch_verification(dap_sign_type_t a_signature_type)
{
    switch (a_signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
        case SIG_TYPE_CHIPMUNK_RING:
        case SIG_TYPE_DILITHIUM:
        case SIG_TYPE_ML_DSA:
            return true;
        default:
            return false;
    }
}

// Get supported aggregation types for a signature algorithm
uint32_t dap_sign_get_supported_aggregation_types(
    dap_sign_type_t a_signature_type,
    dap_sign_aggregation_type_t *a_aggregation_types,
    uint32_t a_max_types)
{
    if (!a_aggregation_types || a_max_types == 0) {
        return 0;
    }
    
    uint32_t count = 0;
    switch (a_signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
            if (count < a_max_types) a_aggregation_types[count++] = DAP_SIGN_AGGREGATION_TYPE_TREE_BASED;
            break;
        case SIG_TYPE_CHIPMUNK_RING:
            // Ring signatures support ring aggregation
            if (count < a_max_types) a_aggregation_types[count++] = DAP_SIGN_AGGREGATION_TYPE_RING;
            break;
        // Add other signature types here
        default:
            break;
    }
    
    return count;
}

// Check if a signature is aggregated
bool dap_sign_is_aggregated(dap_sign_t *a_sign)
{
    if (!a_sign) {
        return false;
    }
    
    // Check signature type specific aggregation markers
    switch (a_sign->header.type.type) {
        case SIG_TYPE_CHIPMUNK:
        {
            // For Chipmunk: check if signature contains aggregation metadata
            // Aggregated signatures have special format:
            // - No public key stored (sign_pkey_size == 0)
            // - Signature data starts with signer count (uint32_t)
            // - Must have minimum size for metadata
            
            if (a_sign->header.sign_pkey_size == 0 && 
                a_sign->header.sign_size >= sizeof(uint32_t)) {
                // This looks like an aggregated signature
                return true;
            }
            
            // Regular Chipmunk signature has public key data
            return false;
        }
        default:
            return false;
    }
}

// Get the number of signers in a signature
uint32_t dap_sign_get_signers_count(dap_sign_t *a_sign)
{
    if (!a_sign) {
        return 0;
    }
    
    // For regular signatures, always return 1
    if (!dap_sign_is_aggregated(a_sign)) {
        return 1;
    }
    
    // For aggregated signatures, extract signer count from signature data
    switch (a_sign->header.type.type) {
        case SIG_TYPE_CHIPMUNK:
        {
            // Extract signer count from Chipmunk aggregated signature
            if (a_sign->header.sign_size < sizeof(uint32_t)) {
                return 1; // Not enough data for aggregated signature
            }
            
            uint8_t *sig_data = a_sign->pkey_n_sign;
            uint32_t signers_count = *(uint32_t*)sig_data;
            
            // Sanity check - reasonable upper limit
            if (signers_count > 1000) {
                log_it(L_WARNING, "Unusually high signer count: %u", signers_count);
                return 1;
            }
            
            return signers_count;
        }
        default:
            return 1;
    }
}

// Internal Chipmunk aggregation implementation.
//
// CR-D6/D7 note (Round-4): the previous implementation had three layers of
// damage that together made the result worthless cryptographically — it
// memcpy'd raw signature bytes into `individual_sigs[i].hots_sig` without
// going through chipmunk_signature_from_bytes(), it left hots_pk / rho_seed /
// proof / tree zeroed, and finally memcpy'd the whole multi_sig struct
// (including heap-allocated pointer fields) into a byte buffer that the
// remote verifier then dereferences as pointers into its own address space.
//
// Rebuilding this function on top of the now-correct aggregate primitive
// requires:
//   (a) a dedicated schema-driven serialization for chipmunk_multi_signature_t
//       (see CR-D10 in the remediation plan — the flat `memcpy` above is
//       undefined behaviour across processes),
//   (b) exposing rho_seed + hots_pk on every dap_sign_t for a Chipmunk
//       signer (already done after CR-D20 framing),
//   (c) proper Merkle-tree plumbing (CR-D15).
//
// None of those are in scope for the CR-D6/D7 rewrite, so until they land
// we refuse the aggregation request up front rather than produce a blob
// that cannot be verified by anyone.
static dap_sign_t *dap_sign_chipmunk_aggregate_signatures_internal(
    dap_sign_t **a_signatures,
    uint32_t a_signatures_count,
    const void *a_message,
    size_t a_message_size,
    const dap_sign_aggregation_params_t *a_params)
{
    (void)a_signatures;
    (void)a_signatures_count;
    (void)a_message;
    (void)a_message_size;
    (void)a_params;

    log_it(L_ERROR,
           "dap_sign aggregation for Chipmunk is disabled pending CR-D10 "
           "(schema-driven multi-signature serialization) and CR-D15 "
           "(Merkle-tree-backed signer proofs).  Use the in-memory "
           "chipmunk_aggregate_signatures_with_tree() API directly if you "
           "need aggregation today.");
    return NULL;
}

// Universal signature aggregation function
dap_sign_t *dap_sign_aggregate_signatures(
    dap_sign_t **a_signatures,
    uint32_t a_signatures_count,
    const void *a_message,
    size_t a_message_size,
    const dap_sign_aggregation_params_t *a_params)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_signatures || a_signatures_count == 0 || !a_message || a_message_size == 0 || !a_params) {
        log_it(L_ERROR, "Invalid input parameters");
        return NULL;
    }
    
    // Validate all signatures are the same type
    dap_sign_type_t sig_type = a_signatures[0]->header.type;
    for (uint32_t i = 1; i < a_signatures_count; i++) {
        if (a_signatures[i]->header.type.raw != sig_type.raw) {
            log_it(L_ERROR, "All signatures must be the same type");
            return NULL;
        }
    }
    
    // Check if signature type supports aggregation
    if (!dap_sign_type_supports_aggregation(sig_type)) {
        log_it(L_ERROR, "Signature type %s does not support aggregation", dap_sign_type_to_str(sig_type));
        return NULL;
    }
    
    // Dispatch to algorithm-specific aggregation implementation
    switch (sig_type.type) {
        case SIG_TYPE_CHIPMUNK:
            return dap_sign_chipmunk_aggregate_signatures_internal(a_signatures, a_signatures_count, a_message, a_message_size, a_params);
        // Add other signature types here
        default:
            log_it(L_ERROR, "Aggregation not implemented for signature type %s", dap_sign_type_to_str(sig_type));
            return NULL;
    }
}

// Universal aggregated signature verification
int dap_sign_verify_aggregated(
    dap_sign_t *a_aggregated_sign,
    const void **a_messages,
    const size_t *a_message_sizes,
    dap_pkey_t **a_public_keys,
    uint32_t a_signers_count)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_aggregated_sign || !a_messages || !a_message_sizes || a_signers_count == 0) {
        log_it(L_ERROR, "Invalid parameters for aggregated signature verification");
        return -1;
    }

    // Check if signature type supports aggregation
    if (!dap_sign_type_supports_aggregation(a_aggregated_sign->header.type)) {
        log_it(L_ERROR, "Signature type %s does not support aggregation", 
               dap_sign_type_to_str(a_aggregated_sign->header.type));
        return -2;
    }

    // Dispatch to algorithm-specific verification implementation
    switch (a_aggregated_sign->header.type.type) {
        case SIG_TYPE_CHIPMUNK:
            return dap_sign_chipmunk_verify_aggregated_internal(a_aggregated_sign, a_messages, 
                                                               a_message_sizes, a_public_keys, a_signers_count);
        // Add other signature types here
        default:
            log_it(L_ERROR, "Aggregated verification not implemented for signature type %s", 
                   dap_sign_type_to_str(a_aggregated_sign->header.type));
            return -3;
    }
}

/*
 * Internal Chipmunk aggregated verification.
 *
 * Symmetric to dap_sign_chipmunk_aggregate_signatures_internal above: the
 * wire format for an aggregated Chipmunk signature over dap_sign_t is
 * not defined yet — chipmunk_multi_signature_t carries heap-owned
 * pointers (proofs[].nodes, …) that cannot be round-tripped by a flat
 * memcpy / struct cast.  Every earlier implementation of this function
 * performed exactly such a cast and then dereferenced the embedded
 * pointers as if they were valid in the current address space, which is
 * textbook undefined behaviour on any cross-process blob.  It also
 * "verified" by checking the first message only and accepting any
 * signature with non-zero polynomial coefficients, which let arbitrary
 * gibberish pass as valid.
 *
 * Since the aggregation producer is already refusing to emit such a
 * blob, the verifier MUST refuse to accept one too — otherwise a future
 * caller could forge acceptance by fabricating the internal layout and
 * bypassing the producer altogether.  The "fail-fast and document" rule
 * is the only sound behaviour until CR-D10 lands a schema-driven
 * (proofs, pks, leaf_indices, hots_pks, …) serialisation format.
 */
static int dap_sign_chipmunk_verify_aggregated_internal(
    dap_sign_t *a_aggregated_sign,
    const void **a_messages,
    const size_t *a_message_sizes,
    dap_pkey_t **a_public_keys,
    uint32_t a_signers_count)
{
    (void)a_aggregated_sign;
    (void)a_messages;
    (void)a_message_sizes;
    (void)a_public_keys;
    (void)a_signers_count;

    log_it(L_ERROR,
           "dap_sign aggregated verification for Chipmunk is disabled pending "
           "CR-D10 (schema-driven multi-signature serialization).  Producing "
           "side is symmetrically refused.  Use the in-memory "
           "chipmunk_verify_multi_signature() API with a properly deserialised "
           "chipmunk_multi_signature_t if you need aggregate verification today.");
    return -5;
}

// Universal batch verification context creation
dap_sign_batch_verify_ctx_t *dap_sign_batch_verify_ctx_new(
    dap_sign_type_t a_signature_type,
    uint32_t a_max_signatures)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (a_max_signatures == 0 || a_max_signatures > 10000) {
        log_it(L_ERROR, "Invalid max signatures count: %u", a_max_signatures);
        return NULL;
    }

    // Check if signature type supports batch verification
    if (!dap_sign_type_supports_batch_verification(a_signature_type)) {
        log_it(L_ERROR, "Signature type %s does not support batch verification", 
               dap_sign_type_to_str(a_signature_type));
        return NULL;
    }

    dap_sign_batch_verify_ctx_t *l_ctx = DAP_NEW_Z(dap_sign_batch_verify_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Memory allocation failed for batch verify context");
        return NULL;
    }

    l_ctx->signature_type = a_signature_type;
    l_ctx->max_signatures = a_max_signatures;
    l_ctx->signatures_count = 0;

    l_ctx->signatures = DAP_NEW_Z_SIZE(dap_sign_t *, sizeof(dap_sign_t *) * a_max_signatures);
    l_ctx->messages = DAP_NEW_Z_SIZE(void *, sizeof(void *) * a_max_signatures);
    l_ctx->message_sizes = DAP_NEW_Z_SIZE(size_t, sizeof(size_t) * a_max_signatures);
    l_ctx->public_keys = DAP_NEW_Z_SIZE(dap_pkey_t *, sizeof(dap_pkey_t *) * a_max_signatures);

    if (!l_ctx->signatures || !l_ctx->messages || !l_ctx->message_sizes || !l_ctx->public_keys) {
        log_it(L_ERROR, "Memory allocation failed for batch verify context arrays");
        dap_sign_batch_verify_ctx_free(l_ctx);
        return NULL;
    }

    debug_if(s_debug_more, L_DEBUG, "Created batch verification context for max %u signatures", a_max_signatures);
    return l_ctx;
}

// Universal batch verification context cleanup
void dap_sign_batch_verify_ctx_free(dap_sign_batch_verify_ctx_t *a_ctx)
{
    if (!a_ctx) return;

    DAP_DELETE(a_ctx->signatures);
    DAP_DELETE(a_ctx->messages);
    DAP_DELETE(a_ctx->message_sizes);
    DAP_DELETE(a_ctx->public_keys);
    DAP_DELETE(a_ctx);
}

// Universal batch verification - add signature
int dap_sign_batch_verify_add_signature(
    dap_sign_batch_verify_ctx_t *a_ctx,
    dap_sign_t *a_signature,
    const void *a_message,
    size_t a_message_size,
    dap_pkey_t *a_public_key)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_ctx || !a_signature || !a_message || a_message_size == 0) {
        log_it(L_ERROR, "Invalid parameters for adding signature to batch");
        return -1;
    }

    if (a_signature->header.type.raw != a_ctx->signature_type.raw) {
        log_it(L_ERROR, "Signature type mismatch in batch verification");
        return -2;
    }

    if (a_ctx->signatures_count >= a_ctx->max_signatures) {
        log_it(L_ERROR, "Batch verification context is full");
        return -3;
    }

    uint32_t index = a_ctx->signatures_count;
    a_ctx->signatures[index] = a_signature;
    a_ctx->messages[index] = (void *)a_message;
    a_ctx->message_sizes[index] = a_message_size;
    a_ctx->public_keys[index] = a_public_key;
    
    a_ctx->signatures_count++;
    
    debug_if(s_debug_more, L_DEBUG, "Added signature %u to batch verification context", index);
    return 0;
}

// Universal batch verification execution  
int dap_sign_batch_verify_execute(dap_sign_batch_verify_ctx_t *a_ctx)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_ctx || a_ctx->signatures_count == 0) {
        log_it(L_ERROR, "Invalid batch verification context");
        return -1;
    }

    debug_if(s_debug_more, L_INFO, "Starting batch verification of %u signatures", a_ctx->signatures_count);
    
    // Dispatch to algorithm-specific batch verification
    switch (a_ctx->signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
            return dap_sign_chipmunk_batch_verify_execute_internal(a_ctx);
        case SIG_TYPE_DILITHIUM:
        case SIG_TYPE_ML_DSA:
            return dap_sign_dilithium_batch_verify_execute_internal(a_ctx);
        default:
            log_it(L_ERROR, "Batch verification not implemented for signature type %s", 
                   dap_sign_type_to_str(a_ctx->signature_type));
            return -2;
    }
}

// Internal Chipmunk batch verification.
//
// CR-D6/D7 rewrite (Round-4): the previous implementation forced each single
// Chipmunk signature through the multi-signer aggregation framework with
// zeroed public_key_roots / hots_pks / rho_seeds.  Under the corrected
// aggregate identity such inputs are rejected outright (and rightly so — an
// aggregate with a blank public key cannot be verified).  We now short-
// circuit to the dedicated single-signer batch verifier
// `chipmunk_batch_verify_hots`, which is the appropriate primitive for a
// list of independent Chipmunk signatures — it parses the public-key bytes
// (including rho_seed), performs the full HOTS identity check in the NTT
// domain and leverages the shared forward-NTT pass for amortised cost.
static int dap_sign_chipmunk_batch_verify_execute_internal(dap_sign_batch_verify_ctx_t *a_ctx)
{
    if (!a_ctx || a_ctx->signatures_count == 0) {
        log_it(L_ERROR, "Invalid batch verification context");
        return -1;
    }

    debug_if(s_debug_more, L_INFO, "Starting Chipmunk batch verification of %u signatures",
             a_ctx->signatures_count);

    const uint32_t l_count = a_ctx->signatures_count;

    const uint8_t **l_pks   = DAP_NEW_Z_COUNT(const uint8_t *, l_count);
    const uint8_t **l_msgs  = DAP_NEW_Z_COUNT(const uint8_t *, l_count);
    size_t         *l_mlens = DAP_NEW_Z_COUNT(size_t,          l_count);
    const uint8_t **l_sigs  = DAP_NEW_Z_COUNT(const uint8_t *, l_count);
    int            *l_res   = DAP_NEW_Z_COUNT(int,             l_count);

    if (!l_pks || !l_msgs || !l_mlens || !l_sigs || !l_res) {
        log_it(L_ERROR, "Failed to allocate batch verification working buffers");
        DAP_DEL_MULTY(l_pks, l_msgs, l_mlens, l_sigs, l_res);
        return -2;
    }

    for (uint32_t i = 0; i < l_count; i++) {
        dap_sign_t *l_dap_sig = a_ctx->signatures[i];
        if (!l_dap_sig || !a_ctx->public_keys[i] ||
            !a_ctx->messages[i] || a_ctx->message_sizes[i] == 0) {
            log_it(L_ERROR, "Missing required inputs for signature %u", i);
            DAP_DEL_MULTY(l_pks, l_msgs, l_mlens, l_sigs, l_res);
            return -3;
        }

        // Public key bytes (serialized chipmunk_public_key_t: rho_seed + v0 + v1).
        l_pks[i] = a_ctx->public_keys[i]->pkey;

        // Signature bytes live after the inline pkey copy in dap_sign_t.
        l_sigs[i] = l_dap_sig->pkey_n_sign + l_dap_sig->header.sign_pkey_size;

        l_msgs[i]  = (const uint8_t *)a_ctx->messages[i];
        l_mlens[i] = a_ctx->message_sizes[i];
    }

    int l_rc = chipmunk_batch_verify_hots(l_pks, l_msgs, l_mlens, l_sigs, l_count, l_res);
    if (l_rc < 0) {
        log_it(L_ERROR, "chipmunk_batch_verify_hots failed with %d", l_rc);
        DAP_DEL_MULTY(l_pks, l_msgs, l_mlens, l_sigs, l_res);
        return -4;
    }

    int l_first_bad = -1;
    for (uint32_t i = 0; i < l_count; i++) {
        if (l_res[i] != 0) {
            l_first_bad = (int)i;
            break;
        }
    }

    DAP_DEL_MULTY(l_pks, l_msgs, l_mlens, l_sigs, l_res);

    if (l_first_bad >= 0) {
        log_it(L_ERROR, "Chipmunk batch verification failed: signature %d rejected",
               l_first_bad);
        return -5;
    }

    debug_if(s_debug_more, L_INFO,
             "Chipmunk batch verification: %u/%u signatures verified",
             l_count, l_count);
    return 0;
}

static int dap_sign_dilithium_batch_verify_execute_internal(dap_sign_batch_verify_ctx_t *a_ctx)
{
    if (!a_ctx || a_ctx->signatures_count == 0) {
        log_it(L_ERROR, "Invalid Dilithium batch verification context");
        return -1;
    }

    uint32_t l_count = a_ctx->signatures_count;
    log_it(L_INFO, "Starting Dilithium batch verification of %u signatures", l_count);

    unsigned char **l_msgs = NULL;
    unsigned long long *l_msg_lens = NULL;
    dilithium_signature_t **l_sigs = NULL;
    const dilithium_public_key_t **l_pkeys = NULL;
    int *l_results = NULL;
    void **l_deserialized_sigs = NULL;
    void **l_deserialized_pkeys = NULL;
    void **l_hash_bufs = NULL;
    int l_ret = -1;

    l_msgs = DAP_NEW_Z_COUNT(unsigned char *, l_count);
    l_msg_lens = DAP_NEW_Z_COUNT(unsigned long long, l_count);
    l_sigs = DAP_NEW_Z_COUNT(dilithium_signature_t *, l_count);
    l_pkeys = DAP_NEW_Z_COUNT(const dilithium_public_key_t *, l_count);
    l_results = DAP_NEW_Z_COUNT(int, l_count);
    l_deserialized_sigs = DAP_NEW_Z_COUNT(void *, l_count);
    l_deserialized_pkeys = DAP_NEW_Z_COUNT(void *, l_count);
    l_hash_bufs = DAP_NEW_Z_COUNT(void *, l_count);

    if (!l_msgs || !l_msg_lens || !l_sigs || !l_pkeys || !l_results ||
        !l_deserialized_sigs || !l_deserialized_pkeys || !l_hash_bufs) {
        log_it(L_ERROR, "Memory allocation failed for Dilithium batch verify");
        goto cleanup;
    }

    for (uint32_t i = 0; i < l_count; i++) {
        dap_sign_t *l_dap_sig = a_ctx->signatures[i];
        if (!l_dap_sig) continue;

        size_t l_sign_ser_size = l_dap_sig->header.sign_size;
        uint8_t *l_sign_ser = l_dap_sig->pkey_n_sign + l_dap_sig->header.sign_pkey_size;

        l_deserialized_sigs[i] = dap_enc_sig_dilithium_read_signature(l_sign_ser, l_sign_ser_size);
        if (!l_deserialized_sigs[i]) {
            log_it(L_WARNING, "Failed to deserialize Dilithium signature %u", i);
            continue;
        }
        l_sigs[i] = (dilithium_signature_t *)l_deserialized_sigs[i];

        size_t l_pkey_ser_size = l_dap_sig->header.sign_pkey_size;
        uint8_t *l_pkey_ser = l_dap_sig->pkey_n_sign;

        l_deserialized_pkeys[i] = dap_enc_sig_dilithium_read_public_key(l_pkey_ser, l_pkey_ser_size);
        if (!l_deserialized_pkeys[i]) {
            log_it(L_WARNING, "Failed to deserialize Dilithium public key %u", i);
            continue;
        }
        l_pkeys[i] = (const dilithium_public_key_t *)l_deserialized_pkeys[i];

        uint32_t l_hash_type = DAP_SIGN_REMOVE_PKEY_HASHING_FLAG(l_dap_sig->header.hash_type);
        if (l_hash_type == DAP_SIGN_HASH_TYPE_NONE || l_hash_type == DAP_SIGN_HASH_TYPE_SIGN) {
            l_msgs[i] = (unsigned char *)a_ctx->messages[i];
            l_msg_lens[i] = a_ctx->message_sizes[i];
        } else {
            dap_hash_sha3_256_t *l_hash = DAP_NEW(dap_hash_sha3_256_t);
            if (!l_hash) continue;
            dap_hash_sha3_256(a_ctx->messages[i], a_ctx->message_sizes[i], l_hash);
            l_hash_bufs[i] = l_hash;
            l_msgs[i] = (unsigned char *)l_hash;
            l_msg_lens[i] = sizeof(dap_hash_sha3_256_t);
        }
    }

    int l_passed = dilithium_crypto_sign_open_batch(
        l_msgs, l_msg_lens, l_sigs, l_pkeys, l_count, l_results);

    if (l_passed < 0) {
        log_it(L_ERROR, "Dilithium batch verify internal error: %d", l_passed);
        l_ret = -2;
        goto cleanup;
    }

    int l_all_ok = 1;
    for (uint32_t i = 0; i < l_count; i++) {
        if (l_results[i] != 0) {
            l_all_ok = 0;
            break;
        }
    }

    log_it(L_INFO, "Dilithium batch verification: %d/%u passed", l_passed, l_count);
    l_ret = l_all_ok ? 0 : -3;

cleanup:
    for (uint32_t i = 0; i < l_count; i++) {
        if (l_deserialized_sigs[i])
            dilithium_signature_delete(l_deserialized_sigs[i]);
        if (l_deserialized_pkeys[i])
            dilithium_public_key_delete(l_deserialized_pkeys[i]);
        DAP_DEL_Z(l_hash_bufs[i]);
    }
    DAP_DEL_Z(l_msgs);
    DAP_DEL_Z(l_msg_lens);
    DAP_DEL_Z(l_sigs);
    DAP_DEL_Z(l_pkeys);
    DAP_DEL_Z(l_results);
    DAP_DEL_Z(l_deserialized_sigs);
    DAP_DEL_Z(l_deserialized_pkeys);
    DAP_DEL_Z(l_hash_bufs);
    return l_ret;
}

// Universal benchmarking functions
int dap_sign_benchmark_aggregation(
    dap_sign_type_t a_signature_type,
    dap_sign_aggregation_type_t a_aggregation_type,
    uint32_t a_signatures_count,
    dap_sign_performance_stats_t *a_stats)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_stats || a_signatures_count == 0 || a_signatures_count > 1000) {
        log_it(L_ERROR, "Invalid benchmark parameters");
        return -1;
    }

    if (!dap_sign_type_supports_aggregation(a_signature_type)) {
        log_it(L_ERROR, "Signature type %s does not support aggregation", 
               dap_sign_type_to_str(a_signature_type));
        return -2;
    }

    memset(a_stats, 0, sizeof(dap_sign_performance_stats_t));
    a_stats->signatures_processed = a_signatures_count;

    debug_if(s_debug_more, L_INFO, "Starting aggregation benchmark with %u signatures", a_signatures_count);

    clock_t start = clock();
    
    // **PRODUCTION-READY**: Реальные операции агрегации вместо dummy циклов
    switch (a_signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
        {
            // Создаем реальные тестовые подписи для агрегации
            dap_sign_t **test_signatures = DAP_NEW_Z_SIZE(dap_sign_t*, a_signatures_count);
            dap_enc_key_t **test_keys = DAP_NEW_Z_SIZE(dap_enc_key_t*, a_signatures_count);
            
            if (!test_signatures || !test_keys) {
                log_it(L_ERROR, "Failed to allocate memory for benchmark");
                if (test_signatures) DAP_DELETE(test_signatures);
                if (test_keys) DAP_DELETE(test_keys);
                return -3;
            }
            
            // Генерируем тестовые ключи и подписи
            const char *test_message = "Benchmark test message for aggregation";
            size_t test_message_len = strlen(test_message);
            
            for (uint32_t i = 0; i < a_signatures_count; i++) {
                // Создаем ключ
                test_keys[i] = dap_enc_chipmunk_key_new();
                if (!test_keys[i]) {
                    log_it(L_ERROR, "Failed to generate test key %u", i);
                    // Cleanup
                    for (uint32_t j = 0; j < i; j++) {
                        if (test_signatures[j]) DAP_DELETE(test_signatures[j]);
                        if (test_keys[j]) dap_enc_key_delete(test_keys[j]);
                    }
                    DAP_DELETE(test_signatures);
                    DAP_DELETE(test_keys);
                    return -3;
                }
                
                // Создаем подпись
                size_t signature_size = 0;
                dap_sign_create_output(test_keys[i], test_message, test_message_len, NULL, &signature_size);
                
                test_signatures[i] = DAP_NEW_Z_SIZE(dap_sign_t, signature_size);
                if (!test_signatures[i]) {
                    log_it(L_ERROR, "Failed to allocate signature %u", i);
                    // Cleanup
                    for (uint32_t j = 0; j < i; j++) {
                        if (test_signatures[j]) DAP_DELETE(test_signatures[j]);
                        if (test_keys[j]) dap_enc_key_delete(test_keys[j]);
                    }
                    dap_enc_key_delete(test_keys[i]);
                    DAP_DELETE(test_signatures);
                    DAP_DELETE(test_keys);
                    return -3;
                }
                
                size_t actual_size = signature_size;
                int sign_result = dap_sign_create_output(test_keys[i], test_message, test_message_len, 
                                                        test_signatures[i], &actual_size);
                if (sign_result != 0) {
                    log_it(L_WARNING, "Failed to create test signature %u", i);
                }
            }
            
            // Выполняем реальную агрегацию
            dap_sign_aggregation_params_t params = {0};
            params.aggregation_type = a_aggregation_type;
            
            dap_sign_t *aggregated = dap_sign_chipmunk_aggregate_signatures_internal(
                test_signatures, a_signatures_count, test_message, test_message_len, &params);
            
            // Cleanup
            for (uint32_t i = 0; i < a_signatures_count; i++) {
                if (test_signatures[i]) DAP_DELETE(test_signatures[i]);
                if (test_keys[i]) dap_enc_key_delete(test_keys[i]);
            }
            if (aggregated) DAP_DELETE(aggregated);
            DAP_DELETE(test_signatures);
            DAP_DELETE(test_keys);
            
            break;
        }
        default:
            log_it(L_ERROR, "Aggregation benchmarking not implemented for signature type %s", 
                   dap_sign_type_to_str(a_signature_type));
            return -3;
    }
    
    clock_t end = clock();
    
    a_stats->aggregation_time_ms = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    a_stats->throughput_sigs_per_sec = a_signatures_count / (a_stats->aggregation_time_ms / 1000.0);

    debug_if(s_debug_more, L_INFO, "Aggregation benchmark completed: %.2f ms, %.2f sigs/sec", 
           a_stats->aggregation_time_ms, a_stats->throughput_sigs_per_sec);

    return 0;
}

int dap_sign_benchmark_batch_verification(
    dap_sign_type_t a_signature_type,
    uint32_t a_signatures_count,
    dap_sign_performance_stats_t *a_stats)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    if (!a_stats || a_signatures_count == 0 || a_signatures_count > 1000) {
        log_it(L_ERROR, "Invalid benchmark parameters");
        return -1;
    }

    if (!dap_sign_type_supports_batch_verification(a_signature_type)) {
        log_it(L_ERROR, "Signature type %s does not support batch verification", 
               dap_sign_type_to_str(a_signature_type));
        return -2;
    }

    memset(a_stats, 0, sizeof(dap_sign_performance_stats_t));
    a_stats->signatures_processed = a_signatures_count;

    log_it(L_INFO, "Starting batch verification benchmark with %u signatures", a_signatures_count);

    clock_t start = clock();
    
    // **PRODUCTION-READY**: Реальная batch verification вместо dummy циклов
    switch (a_signature_type.type) {
        case SIG_TYPE_CHIPMUNK:
        {
            // Создаем реальный batch verification context
            dap_sign_batch_verify_ctx_t *batch_ctx = dap_sign_batch_verify_ctx_new(a_signature_type, a_signatures_count);
            if (!batch_ctx) {
                log_it(L_ERROR, "Failed to create batch verification context");
                return -3;
            }
            
            // Генерируем тестовые подписи для верификации
            const char *test_message = "Benchmark test message for batch verification";
            size_t test_message_len = strlen(test_message);
            
            for (uint32_t i = 0; i < a_signatures_count; i++) {
                // Создаем ключ и подпись
                dap_enc_key_t *test_key = dap_enc_chipmunk_key_new();
                if (!test_key) {
                    log_it(L_WARNING, "Failed to generate test key %u", i);
                    continue;
                }
                
                size_t signature_size = 0;
                dap_sign_create_output(test_key, test_message, test_message_len, NULL, &signature_size);
                
                dap_sign_t *test_signature = DAP_NEW_Z_SIZE(dap_sign_t, signature_size);
                if (test_signature) {
                    size_t actual_size = signature_size;
                    int sign_result = dap_sign_create_output(test_key, test_message, test_message_len, 
                                                            test_signature, &actual_size);
                    
                    if (sign_result == 0) {
                        // Создаем public key для верификации
                        dap_pkey_t *pkey = dap_pkey_from_enc_key(test_key);
                        if (pkey) {
                            // Добавляем в batch
                            dap_sign_batch_verify_add_signature(batch_ctx, test_signature, 
                                                               test_message, test_message_len, pkey);
                            DAP_DELETE(pkey);
                        }
                    }
                    DAP_DELETE(test_signature);
                }
                dap_enc_key_delete(test_key);
            }
            
            // Выполняем реальную batch verification
            int batch_result = dap_sign_batch_verify_execute(batch_ctx);
            
            dap_sign_batch_verify_ctx_free(batch_ctx);
            
            if (batch_result != 0) {
                log_it(L_WARNING, "Batch verification failed during benchmark");
            }
            
            break;
        }
        default:
            log_it(L_ERROR, "Batch verification benchmarking not implemented for signature type %s", 
                   dap_sign_type_to_str(a_signature_type));
            return -3;
    }
    
    clock_t end = clock();
    
    a_stats->batch_verification_time_ms = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    a_stats->throughput_sigs_per_sec = a_signatures_count / (a_stats->batch_verification_time_ms / 1000.0);

    debug_if(s_debug_more, L_INFO, "Batch verification benchmark completed: %.2f ms, %.2f sigs/sec",
           a_stats->batch_verification_time_ms, a_stats->throughput_sigs_per_sec);

    return 0;
}

/**
 * @brief Verify Chipmunk_Ring signature
 * @param a_sign Ring signature to verify
 * @param a_data Data that was signed
 * @param a_data_size Size of data
 * @param a_ring_keys Array of public keys for the ring
 * @param a_ring_size Number of keys in the ring
 * @return 0 if signature is valid, negative value otherwise
 */
int dap_sign_verify_ring(dap_sign_t *a_sign, const void *a_data, size_t a_data_size,
                        dap_enc_key_t **a_ring_keys, size_t a_ring_size) {
    dap_return_val_if_fail(a_sign, -EINVAL);
    // Allow empty messages (a_data can be NULL if a_data_size is 0)
    dap_return_val_if_fail(a_data || a_data_size == 0, -EINVAL);
    dap_return_val_if_fail(a_ring_keys, -EINVAL);
    dap_return_val_if_fail(a_ring_size >= 2 && a_ring_size <= CHIPMUNK_RING_MAX_RING_SIZE, -EINVAL);

    // Check signature type
    if (a_sign->header.type.type != SIG_TYPE_CHIPMUNK_RING) {
        log_it(L_ERROR, "Invalid signature type for ring verification: expected %u, got %u",
               SIG_TYPE_CHIPMUNK_RING, a_sign->header.type.type);
        return -EINVAL;
    }

    // Extract signature data
    size_t l_signature_data_size = 0;
    uint8_t *l_signature_data = dap_sign_get_sign(a_sign, &l_signature_data_size);
    if (!l_signature_data || l_signature_data_size == 0) {
        log_it(L_ERROR, "Failed to extract signature data from dap_sign_t");
        return -EINVAL;
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: extracted signature data, size=%zu", l_signature_data_size);

    // Create ring container from public keys
    chipmunk_ring_public_key_t *l_public_keys = DAP_NEW_SIZE(chipmunk_ring_public_key_t,sizeof(chipmunk_ring_public_key_t) * a_ring_size);
    if (!l_public_keys) {
        log_it(L_CRITICAL, "Failed to allocate memory for ring public keys");
        return -ENOMEM;
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: allocated public keys array");

    // Copy public keys with enhanced safety checks
    for (size_t i = 0; i < a_ring_size; i++) {
        if (!a_ring_keys[i]) {
            log_it(L_ERROR, "Ring key at index %zu is NULL", i);
            DAP_FREE(l_public_keys);
            return -EINVAL;
        }
        if (a_ring_keys[i]->type != DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING) {
            log_it(L_ERROR, "Ring key at index %zu has invalid type %d for ring verification",
                   i, a_ring_keys[i]->type);
            DAP_FREE(l_public_keys);
            return -EINVAL;
        }
        if (!a_ring_keys[i]->pub_key_data) {
            log_it(L_ERROR, "Ring key %zu has NULL pub_key_data", i);
            DAP_FREE(l_public_keys);
            return -EINVAL;
        }
        if (a_ring_keys[i]->pub_key_data_size != CHIPMUNK_RING_PUBLIC_KEY_SIZE) {
            log_it(L_ERROR, "Ring key %zu has wrong size: expected %d, got %zu",
                   i, (int)CHIPMUNK_RING_PUBLIC_KEY_SIZE, a_ring_keys[i]->pub_key_data_size);
            DAP_FREE(l_public_keys);
            return -EINVAL;
        }
        
        memcpy(l_public_keys[i].data, a_ring_keys[i]->pub_key_data, sizeof(l_public_keys[i].data));
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: copied %zu public keys", a_ring_size);

    // Create ring container
    chipmunk_ring_container_t l_ring;
    memset(&l_ring, 0, sizeof(l_ring));

    int l_result = chipmunk_ring_container_create(l_public_keys, a_ring_size, &l_ring);
    DAP_FREE(l_public_keys);

    if (l_result != 0) {
        log_it(L_ERROR, "Failed to create ring container: %d", l_result);
        return l_result;
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: created ring container successfully");

    // Deserialize signature
    chipmunk_ring_signature_t *l_signature = DAP_NEW_Z(chipmunk_ring_signature_t);
    if (!l_signature) {
        log_it(L_CRITICAL, "Failed to allocate memory for signature deserialization");
        chipmunk_ring_container_free(&l_ring);
        return -ENOMEM;
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: allocated signature structure");

    l_result = chipmunk_ring_signature_from_bytes(l_signature, l_signature_data, l_signature_data_size);
    if (l_result != 0) {
        log_it(L_ERROR, "Failed to deserialize ring signature: %d", l_result);
        chipmunk_ring_container_free(&l_ring);
        DAP_FREE(l_signature);
        return l_result;
    }

    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: deserialized signature successfully");

    // Verify signature with adaptive key handling
    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: calling chipmunk_ring_verify (embedded_keys=%s)", 
           l_signature->use_embedded_keys ? "true" : "false");
    
    if (l_signature->use_embedded_keys) {
        // Use embedded keys - pass NULL as ring parameter
        l_result = chipmunk_ring_verify(a_data, a_data_size, l_signature, NULL);
    } else {
        // Use external keys - pass ring container
        l_result = chipmunk_ring_verify(a_data, a_data_size, l_signature, &l_ring);
    }
    
    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: chipmunk_ring_verify returned %d", l_result);

    // Cleanup
    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: starting cleanup");
    chipmunk_ring_signature_free(l_signature);
    chipmunk_ring_container_free(&l_ring);
    // Note: chipmunk_ring_signature_free only frees internal arrays, not the structure itself
    DAP_FREE(l_signature);
    debug_if(s_debug_more, L_INFO, "dap_sign_verify_ring: cleanup completed");

    return l_result;
}

/**
 * @brief dap_sign_get_information_json - Get signature information as JSON
 * @param a_sign Signature to extract information from
 * @param a_json_out JSON object to add information to
 * @param a_hash_out_type Hash output format ("hex" or "base58")
 * @param a_version JSON format version (1 or 2)
 * @return 0 on success, negative error code otherwise
 */
int dap_sign_get_information_json(dap_sign_t* a_sign, dap_json_t *a_json_out, const char *a_hash_out_type, int a_version)
{
    if (!a_sign || !a_json_out)
        return -1;
    
    // Get signature type string
    const char *l_sign_type_str = dap_sign_type_to_str(a_sign->header.type);
    if (a_version == 1) {
        dap_json_object_add_object(a_json_out, "sig_type", dap_json_object_new_string(l_sign_type_str));
        dap_json_object_add_object(a_json_out, "sig_size", dap_json_object_new_uint64(a_sign->header.sign_size));
    } else {
        dap_json_object_add_object(a_json_out, "type", dap_json_object_new_string(l_sign_type_str));
        dap_json_object_add_object(a_json_out, "size", dap_json_object_new_uint64(a_sign->header.sign_size));
    }
    
    // Get public key hash
    dap_hash_sha3_256_t l_pkey_hash = {0};
    if (dap_sign_get_pkey_hash(a_sign, &l_pkey_hash)) {
        char *l_hash_str = dap_strcmp(a_hash_out_type, "hex")
                               ? dap_enc_base58_encode_hash_to_str(&l_pkey_hash)
                               : dap_hash_sha3_256_to_str_new(&l_pkey_hash);
        dap_json_object_add_object(a_json_out, a_version == 1 ? "pkey_hash" : "sig_pkey_hash", 
                                   dap_json_object_new_string(l_hash_str));
        DAP_DELETE(l_hash_str);
    }
    
    return 0;
}

