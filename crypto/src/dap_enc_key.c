/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

    DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdlib.h>
#include <string.h>
#include "dap_common.h"

#include "dap_enc_iaes.h"
#include "dap_enc_oaes.h"
#include "dap_enc_bf.h"
#include "dap_enc_GOST.h"
#include "dap_enc_salsa2012.h"
#include "dap_enc_SEED.h"

#include "dap_enc_msrln.h"
#include "dap_enc_picnic.h"
#include "dap_enc_bliss.h"
#include "dap_enc_tesla.h"
#include "dap_enc_dilithium.h"
#include "dap_enc_falcon.h"
#include "dap_enc_newhope.h"
#include "dap_enc_kyber.h"

#include "dap_enc_ringct20.h"
#ifdef DAP_PQRL
#include "dap_pqrl.h"
#endif

#include "dap_enc_key.h"

#undef LOG_TAG
#define LOG_TAG "dap_enc_key"
dap_enc_key_callbacks_t s_callbacks[]={
    //-Symmetric ciphers----------------------
    // AES
    [DAP_ENC_KEY_TYPE_IAES]={
        .name = "IAES",
        .enc = dap_enc_iaes256_cbc_encrypt,
        .enc_na = dap_enc_iaes256_cbc_encrypt_fast ,
        .dec = dap_enc_iaes256_cbc_decrypt,
        .dec_na = dap_enc_iaes256_cbc_decrypt_fast ,
        .new_callback = dap_enc_aes_key_new,
        .delete_callback = dap_enc_aes_key_delete,
        .new_generate_callback = dap_enc_aes_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_iaes256_calc_encode_size,
        .dec_out_size = dap_enc_iaes256_calc_decode_max_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    // OAES
    [DAP_ENC_KEY_TYPE_OAES]={
        .name = "OAES",
        .enc = dap_enc_oaes_encrypt,
        .enc_na = dap_enc_oaes_encrypt_fast ,
        .dec = dap_enc_oaes_decrypt,
        .dec_na = dap_enc_oaes_decrypt_fast ,
        .new_callback = dap_enc_oaes_key_new,
        .delete_callback = dap_enc_oaes_key_delete,
        .new_generate_callback = dap_enc_oaes_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_oaes_calc_encode_size,
        .dec_out_size = dap_enc_oaes_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_BF_CBC]={
        .name = "BF_CBC",
        .enc = dap_enc_bf_cbc_encrypt,
        .enc_na = dap_enc_bf_cbc_encrypt_fast ,
        .dec = dap_enc_bf_cbc_decrypt,
        .dec_na = dap_enc_bf_cbc_decrypt_fast ,
        .new_callback = dap_enc_bf_cbc_key_new,
        .delete_callback = dap_enc_bf_key_delete,
        .new_generate_callback = dap_enc_bf_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_bf_cbc_calc_encode_size,
        .dec_out_size = dap_enc_bf_cbc_calc_decode_max_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_BF_OFB]={
        .name = "BF_OFB",
        .enc = dap_enc_bf_ofb_encrypt,
        .enc_na = dap_enc_bf_ofb_encrypt_fast ,
        .dec = dap_enc_bf_ofb_decrypt,
        .dec_na = dap_enc_bf_ofb_decrypt_fast ,
        .new_callback = dap_enc_bf_ofb_key_new,
        .delete_callback = dap_enc_bf_key_delete,
        .new_generate_callback = dap_enc_bf_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_bf_ofb_calc_encode_size,
        .dec_out_size = dap_enc_bf_ofb_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_GOST_OFB]={
        .name = "GOST_OFB",
        .enc = dap_enc_gost_ofb_encrypt,
        .enc_na = dap_enc_gost_ofb_encrypt_fast ,
        .dec = dap_enc_gost_ofb_decrypt,
        .dec_na = dap_enc_gost_ofb_decrypt_fast ,
        .new_callback = dap_enc_gost_ofb_key_new,
        .delete_callback = dap_enc_gost_key_delete,
        .new_generate_callback = dap_enc_gost_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_gost_ofb_calc_encode_size,
        .dec_out_size = dap_enc_gost_ofb_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_KUZN_OFB]={
        .name = "KUZN_OFB",
        .enc = dap_enc_kuzn_ofb_encrypt,
        .enc_na = dap_enc_kuzn_ofb_encrypt_fast ,
        .dec = dap_enc_kuzn_ofb_decrypt,
        .dec_na = dap_enc_kuzn_ofb_decrypt_fast ,
        .new_callback = dap_enc_kuzn_ofb_key_new,
        .delete_callback = dap_enc_gost_key_delete,
        .new_generate_callback = dap_enc_gost_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_kuzn_ofb_calc_encode_size,
        .dec_out_size = dap_enc_kuzn_ofb_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SALSA2012]={
        .name = "SALSA2012",
        .enc = dap_enc_salsa2012_encrypt,
        .enc_na = dap_enc_salsa2012_encrypt_fast ,
        .dec = dap_enc_salsa2012_decrypt,
        .dec_na = dap_enc_salsa2012_decrypt_fast ,
        .new_callback = dap_enc_salsa2012_key_new,
        .delete_callback = dap_enc_salsa2012_key_delete,
        .new_generate_callback = dap_enc_salsa2012_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_salsa2012_calc_encode_size,
        .dec_out_size = dap_enc_salsa2012_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SEED_OFB]={
        .name = "SEED_OFB",
        .enc = dap_enc_seed_ofb_encrypt,
        .enc_na = dap_enc_seed_ofb_encrypt_fast ,
        .dec = dap_enc_seed_ofb_decrypt,
        .dec_na = dap_enc_seed_ofb_decrypt_fast ,
        .new_callback = dap_enc_seed_ofb_key_new,
        .delete_callback = dap_enc_seed_key_delete,
        .new_generate_callback = dap_enc_seed_key_generate,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = dap_enc_seed_ofb_calc_encode_size,
        .dec_out_size = dap_enc_seed_ofb_calc_decode_size,
        .sign_get = NULL,
        .sign_verify = NULL
    },

    //-KEMs(Key Exchange Mechanism)----------------------
    [DAP_ENC_KEY_TYPE_MSRLN] = {
        .name = "MSRLN",
        .enc = NULL,
        .dec = NULL,
        .new_callback =                      dap_enc_msrln_key_new,
        .delete_callback =                   dap_enc_msrln_key_delete,
        .new_generate_callback =             dap_enc_msrln_key_generate,
        .gen_bob_shared_key =                dap_enc_msrln_gen_bob_shared_key,
        .gen_alice_shared_key =              dap_enc_msrln_gen_alice_shared_key,
        .new_from_data_public_callback =     dap_enc_msrln_key_new_from_data_public,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_KEM_KYBER512] = {
        .name = "KYBER",
        .enc = NULL,
        .dec = NULL,
        .new_callback =                    dap_enc_kyber512_key_new,
        .delete_callback =                 dap_enc_kyber512_key_delete,
        .new_generate_callback =           dap_enc_kyber512_key_generate,
        .gen_bob_shared_key =              dap_enc_kyber512_gen_bob_shared_key,
        .gen_alice_shared_key =            dap_enc_kyber512_gen_alice_shared_key,
        .new_from_data_public_callback =   dap_enc_kyber512_key_new_from_data_public,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM]={
        .name = "NEWHOPE_CPA_KEM",
        .enc = NULL,
        .dec = NULL,
        .enc_na = NULL,
        .dec_na = NULL,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .gen_bob_shared_key = dap_enc_newhope_pbk_enc,
        .gen_alice_shared_key = dap_enc_newhope_prk_dec,
        .new_callback = dap_enc_newhope_kem_key_new,
        .delete_callback = dap_enc_newhope_kem_key_delete,
        .new_generate_callback = dap_enc_newhope_kem_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    //------Signatures---------------------------
    [DAP_ENC_KEY_TYPE_SIG_PICNIC]={
        .name = "PICNIC",
        .enc = NULL,
        .dec = NULL,
        .enc_na = dap_enc_sig_picnic_get_sign, // dap_enc_picnic_enc_na
        .dec_na = dap_enc_sig_picnic_verify_sign,// dap_enc_picnic_dec_na
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_picnic_key_new,
        .gen_key_public = NULL,
        .gen_key_public_size = dap_enc_picnic_calc_signature_size,
        .delete_callback = dap_enc_sig_picnic_key_delete,
        .new_generate_callback = dap_enc_sig_picnic_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_BLISS]={
        .name = "SIG_BLISS",
        .enc = NULL,
        .dec = NULL,
        .enc_na = NULL,
        .dec_na = NULL,
        .sign_get = dap_enc_sig_bliss_get_sign,
        .sign_verify = dap_enc_sig_bliss_verify_sign,
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_bliss_key_new,
        .delete_callback = dap_enc_sig_bliss_key_delete,
        .new_generate_callback = dap_enc_sig_bliss_key_new_generate,
        .gen_key_public = dap_enc_sig_bliss_key_pub_output,
        .gen_key_public_size = dap_enc_sig_bliss_key_pub_output_size,

        .enc_out_size = NULL,
        .dec_out_size = NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_TESLA]={
        .name = "SIG_TESLA",
        .enc = NULL,
        .dec = NULL,
        .enc_na = dap_enc_sig_tesla_get_sign,
        .dec_na = dap_enc_sig_tesla_verify_sign,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_tesla_key_new,
        .delete_callback = dap_enc_sig_tesla_key_delete,
        .new_generate_callback = dap_enc_sig_tesla_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_DILITHIUM]={
        .name = "SIG_DILITHIUM",
        .enc = NULL,
        .dec = NULL,
        .enc_na = dap_enc_sig_dilithium_get_sign,
        .dec_na = dap_enc_sig_dilithium_verify_sign,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_dilithium_key_new,
        .delete_callback = dap_enc_sig_dilithium_key_delete,
        .new_generate_callback = dap_enc_sig_dilithium_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_RINGCT20]={
        .name = "SIG_RINGCT20",
        .enc = NULL,
        .dec = NULL,
        .enc_na = dap_enc_sig_ringct20_get_sign_with_pb_list,//dap_enc_sig_ringct20_get_sign,
        .dec_na = dap_enc_sig_ringct20_verify_sign,
        .dec_na_ext = dap_enc_sig_ringct20_verify_sign_with_pbk_list,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_ringct20_key_new,
        .delete_callback = dap_enc_sig_ringct20_key_delete,
        .new_generate_callback = dap_enc_sig_ringct20_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_FALCON]={
        .name = "SIG_FALCON",
        .enc = NULL,
        .dec = NULL,
        .enc_na = dap_enc_sig_falcon_get_sign,
        .dec_na = dap_enc_sig_falcon_verify_sign,
        .gen_key_public = NULL,
        .gen_key_public_size = NULL,
        .gen_bob_shared_key = NULL,
        .gen_alice_shared_key = NULL,
        .new_callback = dap_enc_sig_falcon_key_new,
        .delete_callback = dap_enc_sig_falcon_key_delete,
        .new_generate_callback = dap_enc_sig_falcon_key_new_generate,
        .enc_out_size = NULL,
        .dec_out_size = NULL,
        .sign_get = NULL,
        .sign_verify = NULL
    },

#ifdef DAP_PQLR
    [DAP_ENC_KEY_TYPE_PQLR_SIG_DILITHIUM] = {0},
    [DAP_ENC_KEY_TYPE_PQLR_SIG_FALCON] = {0},
    [DAP_ENC_KEY_TYPE_PQLR_SIG_SPHINCS] = {0},
    [DAP_ENC_KEY_TYPE_PQLR_KEM_SABER] = {0},
    [DAP_ENC_KEY_TYPE_PQLR_KEM_MCELIECE] = {0},
    [DAP_ENC_KEY_TYPE_PQLR_KEM_NEWHOPE] = {0},
#endif

};

const size_t c_callbacks_size = sizeof(s_callbacks) / sizeof(s_callbacks[0]);
/**
 * @brief dap_enc_key_init empty stub
 * @return
 */
int dap_enc_key_init()
{
#ifdef DAP_PQRL
    if( dap_pqrl_init(s_callbacks) != 0 )
        return -1;
#endif
    return 0;
}

/**
 * @brief dap_enc_key_deinit
 */
void dap_enc_key_deinit()
{
#ifdef DAP_PQRL
    dap_pqrl_deinit();
#endif

}

/**
 * @brief dap_enc_key_serialize_sign
 *
 * @param a_key_type
 * @param a_sign
 * @param a_sign_len [in/out]
 * @return allocates memory with private key
 */
uint8_t* dap_enc_key_serialize_sign(dap_enc_key_type_t a_key_type, uint8_t *a_sign, size_t *a_sign_len)
{
    uint8_t *l_data = NULL;
    switch (a_key_type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        l_data = dap_enc_sig_bliss_write_signature((bliss_signature_t*)a_sign, a_sign_len);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        l_data = dap_enc_tesla_write_signature((tesla_signature_t*)a_sign, a_sign_len);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        l_data = dap_enc_dilithium_write_signature((dilithium_signature_t*)a_sign, a_sign_len);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        l_data = dap_enc_falcon_write_signature((falcon_signature_t *) a_sign, a_sign_len);
        break;
    default:
        l_data = DAP_NEW_Z_SIZE(uint8_t, *a_sign_len);
        // if(!l_data) {
        //     log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        //     return NULL;
        // }
        memcpy(l_data, a_sign, *a_sign_len);
    }
    return l_data;
}

/**
 * @brief dap_enc_key_serialize_sign
 *
 * @param a_key_type
 * @param a_sign
 * @param a_sign_len [in/out]
 * @return allocates memory with private key
 */
uint8_t* dap_enc_key_deserialize_sign(dap_enc_key_type_t a_key_type, uint8_t *a_sign, size_t *a_sign_len)
{

    //todo: why are we changing a_sign_len after we have already used it in a function call?
    uint8_t *l_data = NULL;
    switch (a_key_type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        l_data = (uint8_t*)dap_enc_sig_bliss_read_signature(a_sign, *a_sign_len);
        *a_sign_len = sizeof(bliss_signature_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        l_data = (uint8_t*)dap_enc_tesla_read_signature(a_sign, *a_sign_len);
        *a_sign_len = sizeof(tesla_signature_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        l_data = (uint8_t*)dap_enc_dilithium_read_signature(a_sign, *a_sign_len);
        *a_sign_len = sizeof(dilithium_signature_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        l_data = (uint8_t*)dap_enc_falcon_read_signature(a_sign, *a_sign_len);
        *a_sign_len = sizeof(falcon_signature_t);
        break;
    default:
        l_data = DAP_NEW_Z_SIZE(uint8_t, *a_sign_len);
        // if(!l_data) {
        //     log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        //     return NULL;
        // }
        memcpy(l_data, a_sign, *a_sign_len);
    }
    return l_data;
}


/**
 * @brief dap_enc_key_serialize_priv_key
 *
 * @param a_key
 * @param a_buflen_out
 * @return allocates memory with private key
 */
uint8_t* dap_enc_key_serialize_priv_key(dap_enc_key_t *a_key, size_t *a_buflen_out)
{
    uint8_t *l_data = NULL;
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        l_data = dap_enc_sig_bliss_write_private_key(a_key->priv_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        l_data = dap_enc_tesla_write_private_key(a_key->priv_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        l_data = dap_enc_dilithium_write_private_key(a_key->priv_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        l_data = dap_enc_falcon_write_private_key(a_key->priv_key_data, a_buflen_out);
        break;
    default:
        l_data = DAP_NEW_Z_SIZE(uint8_t, a_key->priv_key_data_size);
        if(!l_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        memcpy(l_data, a_key->priv_key_data, a_key->priv_key_data_size);
        if(a_buflen_out)
            *a_buflen_out = a_key->priv_key_data_size;
    }
    return l_data;
}

/**
 * @brief dap_enc_key_serialize_pub_key
 *
 * @param a_key
 * @param a_buflen_out
 * @return allocates memory with private key
 */
uint8_t* dap_enc_key_serialize_pub_key(dap_enc_key_t *a_key, size_t *a_buflen_out)
{
    uint8_t *l_data = NULL;
    if ( a_key->pub_key_data == NULL ){
        log_it(L_ERROR, "Public key is NULL");
        return NULL;
    }
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        l_data = dap_enc_sig_bliss_write_public_key(a_key->pub_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        l_data = dap_enc_tesla_write_public_key(a_key->pub_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        l_data = dap_enc_dilithium_write_public_key(a_key->pub_key_data, a_buflen_out);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        l_data = dap_enc_falcon_write_public_key(a_key->pub_key_data, a_buflen_out);
        break;
    default:
        l_data = DAP_NEW_Z_SIZE(uint8_t, a_key->pub_key_data_size);
        if(!l_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        memcpy(l_data, a_key->pub_key_data, a_key->pub_key_data_size);
        if(a_buflen_out)
            *a_buflen_out = a_key->pub_key_data_size;
    }
    return l_data;
}
/**
 * @brief dap_enc_key_deserialize_priv_key
 *
 * @param a_key
 * @param a_buf
 * @param a_buflen_out
 * @return 0 Ok, -1 error
 */
int dap_enc_key_deserialize_priv_key(dap_enc_key_t *a_key, const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_key || !a_buf)
        return -1;
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        if((a_key->priv_key_data)) {
            bliss_b_private_key_delete((bliss_private_key_t *) a_key->priv_key_data);
            DAP_DELETE(a_key->pub_key_data);
        }
        a_key->priv_key_data = (uint8_t*) dap_enc_sig_bliss_read_private_key(a_buf, a_buflen);
        if(!a_key->priv_key_data)
        {
            a_key->priv_key_data_size = 0;
            return -1;
        }
        a_key->priv_key_data_size = sizeof(bliss_private_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        tesla_private_key_delete((tesla_private_key_t *) a_key->priv_key_data);
        a_key->priv_key_data = (uint8_t*) dap_enc_tesla_read_private_key(a_buf, a_buflen);
        if(!a_key->priv_key_data)
        {
            a_key->priv_key_data_size = 0;
            return -1;
        }
        a_key->priv_key_data_size = sizeof(tesla_private_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        DAP_DELETE(a_key->priv_key_data);
        a_key->priv_key_data_size = a_buflen;
        a_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, a_key->priv_key_data_size);
        if(!a_key->priv_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return -1;
        }
        memcpy(a_key->priv_key_data, a_buf, a_key->priv_key_data_size);
        dap_enc_sig_picnic_update(a_key);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        dilithium_private_key_delete((dilithium_private_key_t *) a_key->priv_key_data);
        a_key->priv_key_data = (uint8_t*) dap_enc_dilithium_read_private_key(a_buf, a_buflen);
        if(!a_key->priv_key_data)
        {
            a_key->priv_key_data_size = 0;
            return -1;
        }
        a_key->priv_key_data_size = sizeof(dilithium_private_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        falcon_private_key_delete((falcon_private_key_t *) a_key->priv_key_data);
        a_key->priv_key_data = (uint8_t*) dap_enc_falcon_read_private_key(a_buf, a_buflen);
        if(!a_key->priv_key_data)
        {
            a_key->priv_key_data_size = 0;
            return -1;
        }
        a_key->priv_key_data_size = sizeof(falcon_private_key_t);
        break;
    default:
        DAP_DELETE(a_key->priv_key_data);
        a_key->priv_key_data_size = a_buflen;
        a_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, a_key->priv_key_data_size);
        if(!a_key->priv_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return -1;
        }
        memcpy(a_key->priv_key_data, a_buf, a_key->priv_key_data_size);
    }
    return 0;
}

/**
 * @brief dap_enc_key_deserialize_pub_key
 *
 * @param a_key
 * @param a_buf
 * @param a_buflen_out
 * @return 0 Ok, -1 error
 */
int dap_enc_key_deserialize_pub_key(dap_enc_key_t *a_key, const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_key || !a_buf)
        return -1;
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        if((a_key->pub_key_data)) {
            bliss_b_public_key_delete((bliss_public_key_t *) a_key->pub_key_data);
            DAP_DELETE(a_key->pub_key_data);
        }
        a_key->pub_key_data = (uint8_t*) dap_enc_sig_bliss_read_public_key(a_buf, a_buflen);
        if(!a_key->pub_key_data)
        {
            a_key->pub_key_data_size = 0;
            return -1;
        }
        a_key->pub_key_data_size = sizeof(bliss_public_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        tesla_public_key_delete((tesla_public_key_t *) a_key->pub_key_data);
        a_key->pub_key_data = (uint8_t*) dap_enc_tesla_read_public_key(a_buf, a_buflen);
        if(!a_key->pub_key_data)
        {
            a_key->pub_key_data_size = 0;
            return -1;
        }
        a_key->pub_key_data_size = sizeof(tesla_public_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        DAP_DELETE(a_key->pub_key_data);
        a_key->pub_key_data_size = a_buflen;
        a_key->pub_key_data = DAP_NEW_Z_SIZE(uint8_t, a_key->pub_key_data_size);
        if(!a_key->priv_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return -1;
        }
        memcpy(a_key->pub_key_data, a_buf, a_key->pub_key_data_size);
        dap_enc_sig_picnic_update(a_key);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        if ( a_key->pub_key_data )
            dilithium_public_key_delete((dilithium_public_key_t *) a_key->pub_key_data);

        a_key->pub_key_data = (uint8_t*) dap_enc_dilithium_read_public_key(a_buf, a_buflen);
        if(!a_key->pub_key_data)
        {
            a_key->pub_key_data_size = 0;
            return -1;
        }
        a_key->pub_key_data_size = sizeof(dilithium_public_key_t);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        if ( a_key->pub_key_data )
            falcon_public_key_delete((falcon_public_key_t *) a_key->pub_key_data);

        a_key->pub_key_data = (uint8_t*) dap_enc_falcon_read_public_key(a_buf, a_buflen);
        if(!a_key->pub_key_data)
        {
            a_key->pub_key_data_size = 0;
            return -1;
        }
        a_key->pub_key_data_size = sizeof(falcon_public_key_t);
        break;
    default:
        DAP_DELETE(a_key->pub_key_data);
        a_key->pub_key_data_size = a_buflen;
        a_key->pub_key_data = DAP_NEW_Z_SIZE(uint8_t, a_key->pub_key_data_size);
        if(!a_key->priv_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return -1;
        }
        memcpy(a_key->pub_key_data, a_buf, a_key->pub_key_data_size);
    }
    return 0;
}

/**
 * @brief dap_enc_key_serialize
 * @param key
 * @return allocates dap_enc_key_serialize_t* dont remember use free()
 */
dap_enc_key_serialize_t* dap_enc_key_serialize(dap_enc_key_t *a_key)
{
    dap_enc_key_serialize_t *l_ret = DAP_NEW_Z(dap_enc_key_serialize_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    l_ret->priv_key_data_size = a_key->priv_key_data_size;
    l_ret->pub_key_data_size = a_key->pub_key_data_size;
    l_ret->last_used_timestamp = a_key->last_used_timestamp;
    l_ret->inheritor_size = a_key->_inheritor_size;
    l_ret->type = a_key->type;
    memcpy(l_ret->priv_key_data, a_key->priv_key_data, a_key->priv_key_data_size);
    memcpy(l_ret->pub_key_data, a_key->pub_key_data, a_key->pub_key_data_size);
    memcpy(l_ret->inheritor, a_key->_inheritor, a_key->_inheritor_size);
    return l_ret;
}

/**
 * @brief dap_enc_key_dup
 * @param a_key
 * @return
 */
dap_enc_key_t* dap_enc_key_dup(dap_enc_key_t * a_key)
{
    if (!a_key || a_key->type == DAP_ENC_KEY_TYPE_INVALID) {
        return NULL;
    }
    dap_enc_key_t *l_ret = dap_enc_key_new(a_key->type);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    if (a_key->priv_key_data_size) {
        l_ret->priv_key_data = DAP_NEW_Z_SIZE(byte_t, a_key->priv_key_data_size);
        if (!l_ret->priv_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            DAP_DEL_Z(l_ret);
            return NULL;
        }
        l_ret->priv_key_data_size = a_key->priv_key_data_size;
        memcpy(l_ret->priv_key_data, a_key->priv_key_data, a_key->priv_key_data_size);
    }
    if (a_key->pub_key_data_size) {
        l_ret->pub_key_data = DAP_NEW_Z_SIZE(byte_t, a_key->pub_key_data_size);
        if (!l_ret->pub_key_data) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            DAP_DEL_Z(l_ret->priv_key_data);
            DAP_DEL_Z(l_ret);
            return NULL;
        }
        l_ret->pub_key_data_size =  a_key->pub_key_data_size;
        memcpy(l_ret->pub_key_data, a_key->pub_key_data, a_key->pub_key_data_size);
    }
    if(a_key->_inheritor_size) {
        l_ret->_inheritor = DAP_NEW_Z_SIZE(byte_t, a_key->_inheritor_size);
        if (!l_ret->_inheritor) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            DAP_DEL_Z(l_ret->priv_key_data);
            DAP_DEL_Z(l_ret->pub_key_data);
            DAP_DEL_Z(l_ret);
            return NULL;
        }
        l_ret->_inheritor_size = a_key->_inheritor_size;
        memcpy(l_ret->_inheritor, a_key->_inheritor, a_key->_inheritor_size);
    }
    return l_ret;
}

/**
 * @brief dap_enc_key_deserialize
 * @param buf
 * @param buf_size
 * @return allocates dap_enc_key_t*. Use dap_enc_key_delete for free memory
 */
dap_enc_key_t* dap_enc_key_deserialize(const void *buf, size_t a_buf_size)
{
    if(a_buf_size != sizeof (dap_enc_key_serialize_t)) {
        log_it(L_ERROR, "Key can't be deserialize. buf_size(%zu) != sizeof (dap_enc_key_serialize_t)(%zu)", a_buf_size, sizeof(dap_enc_key_serialize_t));
        return NULL;
    }
    const dap_enc_key_serialize_t *in_key = (const dap_enc_key_serialize_t *)buf;
    dap_enc_key_t *l_ret = dap_enc_key_new(in_key->type);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    l_ret->last_used_timestamp = in_key->last_used_timestamp;
    l_ret->priv_key_data_size = in_key->priv_key_data_size;
    l_ret->pub_key_data_size = in_key->pub_key_data_size;
    l_ret->_inheritor_size = in_key->inheritor_size;
    DAP_DEL_Z(l_ret->priv_key_data);
    DAP_DEL_Z(l_ret->pub_key_data);
    l_ret->priv_key_data = DAP_NEW_Z_SIZE(byte_t, l_ret->priv_key_data_size);
    if (!l_ret->priv_key_data) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        DAP_DEL_Z(l_ret);
        return NULL;
    }
    memcpy(l_ret->priv_key_data, in_key->priv_key_data, l_ret->priv_key_data_size);
    l_ret->pub_key_data = DAP_NEW_Z_SIZE(byte_t, l_ret->pub_key_data_size);
    if (!l_ret->pub_key_data) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        DAP_DEL_Z(l_ret->priv_key_data);
        DAP_DEL_Z(l_ret);
        return NULL;
    }
    memcpy(l_ret->pub_key_data, in_key->pub_key_data, l_ret->pub_key_data_size);
    if(in_key->inheritor_size) {
        DAP_DEL_Z(l_ret->_inheritor);
        l_ret->_inheritor = DAP_NEW_Z_SIZE(byte_t, in_key->inheritor_size );
        if (!l_ret->_inheritor) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            DAP_DEL_Z(l_ret->priv_key_data);
            DAP_DEL_Z(l_ret->pub_key_data);
            DAP_DEL_Z(l_ret);
            return NULL;
        }
        memcpy(l_ret->_inheritor, in_key->inheritor, in_key->inheritor_size);
    } else {
        l_ret->_inheritor = NULL;
    }
    return l_ret;
}

/**
 * @brief dap_enc_key_new
 * @param a_key_type
 * @return
 */
dap_enc_key_t *dap_enc_key_new(dap_enc_key_type_t a_key_type)
{
    dap_enc_key_t * l_ret = NULL;
    if ((size_t)a_key_type < c_callbacks_size) {
        l_ret = DAP_NEW_Z(dap_enc_key_t);
        if (!l_ret) {
            log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        if(s_callbacks[a_key_type].new_callback){
            s_callbacks[a_key_type].new_callback(l_ret);
        }
    }
    if(l_ret)
        l_ret->type = a_key_type;
    return l_ret;
}

/**
 * @brief dap_enc_key_new_generate
 * @param a_key_type
 * @param kex_buf
 * @param kex_size
 * @param seed
 * @param seed_size
 * @param key_size - can be NULL ( generate size by default )
 * @return
 */
dap_enc_key_t *dap_enc_key_new_generate(dap_enc_key_type_t a_key_type, const void *a_kex_buf,
                                        size_t a_kex_size, const void* a_seed,
                                        size_t a_seed_size, size_t a_key_size)
{
    dap_enc_key_t * l_ret = NULL;
    if ((size_t)a_key_type < c_callbacks_size) {
        l_ret = dap_enc_key_new(a_key_type);
        if(s_callbacks[a_key_type].new_generate_callback) {
            s_callbacks[a_key_type].new_generate_callback( l_ret, a_kex_buf, a_kex_size, a_seed, a_seed_size, a_key_size);
        }
    }
    return l_ret;
}

/**
 * @brief dap_enc_key_update
 * @param a_key_type
 * @return
 */
void dap_enc_key_update(dap_enc_key_t *a_key)
{
    if(a_key)
        switch (a_key->type) {
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
            break;
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
            dap_enc_sig_picnic_update(a_key);
            break;
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
            break;
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
            break;
        default:
            break;
        }
}

size_t dap_enc_gen_key_public_size (dap_enc_key_t *a_key)
{
    if(s_callbacks[a_key->type].gen_key_public_size) {
        return s_callbacks[a_key->type].gen_key_public_size(a_key);
    } else {
        log_it(L_ERROR, "No callback for key public size calculate");
        return 0;
    }
}

int dap_enc_gen_key_public (dap_enc_key_t *a_key, void * a_output)
{
    if(s_callbacks[a_key->type].gen_key_public) {
        return s_callbacks[a_key->type].gen_key_public(a_key,a_output);
    } else {
        log_it(L_ERROR, "No callback for key public generate action");
    }
    return -1;
}

/**
 * @brief dap_enc_key_delete
 * @param a_key
 */
void dap_enc_key_signature_delete(dap_enc_key_type_t a_key_type, uint8_t *a_sig_buf)
{
    switch (a_key_type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
        bliss_signature_delete((bliss_signature_t*)a_sig_buf);
        break;
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
        tesla_signature_delete((tesla_signature_t*)a_sig_buf);
        break;
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        dilithium_signature_delete((dilithium_signature_t*)a_sig_buf);
        break;
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
        DAP_DELETE(((falcon_signature_t *)a_sig_buf)->sig_data);
        break;
    default:
        break;
    }
    DAP_DELETE(a_sig_buf);
}

/**
 * @brief dap_enc_key_delete
 * @param a_key
 */
void dap_enc_key_delete(dap_enc_key_t * a_key)
{
    if(s_callbacks[a_key->type].delete_callback) {
        s_callbacks[a_key->type].delete_callback(a_key);
    } else {
        log_it(L_ERROR, "delete callback is null. Can be leak memory!");
    }
    /* a_key->_inheritor must be cleaned in delete_callback func */
    if ( a_key->pub_key_data)
        DAP_DELETE(a_key->pub_key_data);
    if (a_key->priv_key_data )
        DAP_DELETE(a_key->priv_key_data);
    DAP_DELETE(a_key);
}

size_t dap_enc_key_get_enc_size(dap_enc_key_t * a_key, const size_t a_buf_in_size)
{
    if(s_callbacks[a_key->type].enc_out_size) {
        return s_callbacks[a_key->type].enc_out_size(a_buf_in_size);
    }
    log_it(L_ERROR, "enc_out_size not realize for current key type");
    return 0;
}

size_t dap_enc_key_get_dec_size(dap_enc_key_t * a_key, const size_t a_buf_in_size)
{
    if(s_callbacks[a_key->type].dec_out_size) {
        return s_callbacks[a_key->type].dec_out_size(a_buf_in_size);
    }
    log_it(L_ERROR, "dec_out_size not realize for current key type");
    return 0;
}

const char *dap_enc_get_type_name(dap_enc_key_type_t a_key_type)
{
    if(a_key_type >= DAP_ENC_KEY_TYPE_NULL && a_key_type <= DAP_ENC_KEY_TYPE_LAST) {
        if(s_callbacks[a_key_type].name) {
            return s_callbacks[a_key_type].name;
        }
    }
    log_it(L_WARNING, "name was not set for key type %d", a_key_type);
    return 0;
}

dap_enc_key_type_t dap_enc_key_type_find_by_name(const char * a_name){
    for(dap_enc_key_type_t i = 0; i <= DAP_ENC_KEY_TYPE_LAST; i++){
        const char * l_current_key_name = dap_enc_get_type_name(i);
        if(l_current_key_name && !strcmp(a_name, l_current_key_name))
            return i;
    }
    log_it(L_WARNING, "no key type with name %s", a_name);
    return DAP_ENC_KEY_TYPE_INVALID;
}

