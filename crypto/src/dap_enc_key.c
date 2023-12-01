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
#include "dap_enc_sphincsplus.h"
#include "dap_enc_multisign.h"

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
        .name =                             "IAES",
        .enc =                              dap_enc_iaes256_cbc_encrypt,
        .enc_na =                           dap_enc_iaes256_cbc_encrypt_fast ,
        .dec =                              dap_enc_iaes256_cbc_decrypt,
        .dec_na =                           dap_enc_iaes256_cbc_decrypt_fast ,
        .new_callback =                     dap_enc_aes_key_new,
        .delete_callback =                  dap_enc_aes_key_delete,
        .new_generate_callback =            dap_enc_aes_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_iaes256_calc_encode_size,
        .dec_out_size =                     dap_enc_iaes256_calc_decode_max_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    // OAES
    [DAP_ENC_KEY_TYPE_OAES]={
        .name =                             "OAES",
        .enc =                              dap_enc_oaes_encrypt,
        .enc_na =                           dap_enc_oaes_encrypt_fast ,
        .dec =                              dap_enc_oaes_decrypt,
        .dec_na =                           dap_enc_oaes_decrypt_fast ,
        .new_callback =                     dap_enc_oaes_key_new,
        .delete_callback =                  dap_enc_oaes_key_delete,
        .new_generate_callback =            dap_enc_oaes_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_oaes_calc_encode_size,
        .dec_out_size =                     dap_enc_oaes_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_BF_CBC]={
        .name =                             "BF_CBC",
        .enc =                              dap_enc_bf_cbc_encrypt,
        .enc_na =                           dap_enc_bf_cbc_encrypt_fast ,
        .dec =                              dap_enc_bf_cbc_decrypt,
        .dec_na =                           dap_enc_bf_cbc_decrypt_fast ,
        .new_callback =                     dap_enc_bf_cbc_key_new,
        .delete_callback =                  dap_enc_bf_key_delete,
        .new_generate_callback =            dap_enc_bf_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_bf_cbc_calc_encode_size,
        .dec_out_size =                     dap_enc_bf_cbc_calc_decode_max_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_BF_OFB]={
        .name =                             "BF_OFB",
        .enc =                              dap_enc_bf_ofb_encrypt,
        .enc_na =                           dap_enc_bf_ofb_encrypt_fast ,
        .dec =                              dap_enc_bf_ofb_decrypt,
        .dec_na =                           dap_enc_bf_ofb_decrypt_fast ,
        .new_callback =                     dap_enc_bf_ofb_key_new,
        .delete_callback =                  dap_enc_bf_key_delete,
        .new_generate_callback =            dap_enc_bf_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_bf_ofb_calc_encode_size,
        .dec_out_size =                     dap_enc_bf_ofb_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_GOST_OFB]={
        .name =                             "GOST_OFB",
        .enc =                              dap_enc_gost_ofb_encrypt,
        .enc_na =                           dap_enc_gost_ofb_encrypt_fast ,
        .dec =                              dap_enc_gost_ofb_decrypt,
        .dec_na =                           dap_enc_gost_ofb_decrypt_fast ,
        .new_callback =                     dap_enc_gost_ofb_key_new,
        .delete_callback =                  dap_enc_gost_key_delete,
        .new_generate_callback =            dap_enc_gost_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_gost_ofb_calc_encode_size,
        .dec_out_size =                     dap_enc_gost_ofb_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_KUZN_OFB]={
        .name =                             "KUZN_OFB",
        .enc =                              dap_enc_kuzn_ofb_encrypt,
        .enc_na =                           dap_enc_kuzn_ofb_encrypt_fast ,
        .dec =                              dap_enc_kuzn_ofb_decrypt,
        .dec_na =                           dap_enc_kuzn_ofb_decrypt_fast ,
        .new_callback =                     dap_enc_kuzn_ofb_key_new,
        .delete_callback =                  dap_enc_gost_key_delete,
        .new_generate_callback =            dap_enc_gost_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_kuzn_ofb_calc_encode_size,
        .dec_out_size =                     dap_enc_kuzn_ofb_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_SALSA2012]={
        .name =                             "SALSA2012",
        .enc =                              dap_enc_salsa2012_encrypt,
        .enc_na =                           dap_enc_salsa2012_encrypt_fast ,
        .dec =                              dap_enc_salsa2012_decrypt,
        .dec_na =                           dap_enc_salsa2012_decrypt_fast ,
        .new_callback =                     dap_enc_salsa2012_key_new,
        .delete_callback =                  dap_enc_salsa2012_key_delete,
        .new_generate_callback =            dap_enc_salsa2012_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_salsa2012_calc_encode_size,
        .dec_out_size =                     dap_enc_salsa2012_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_SEED_OFB]={
        .name =                             "SEED_OFB",
        .enc =                              dap_enc_seed_ofb_encrypt,
        .enc_na =                           dap_enc_seed_ofb_encrypt_fast ,
        .dec =                              dap_enc_seed_ofb_decrypt,
        .dec_na =                           dap_enc_seed_ofb_decrypt_fast ,
        .new_callback =                     dap_enc_seed_ofb_key_new,
        .delete_callback =                  dap_enc_seed_key_delete,
        .new_generate_callback =            dap_enc_seed_key_generate,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     dap_enc_seed_ofb_calc_encode_size,
        .dec_out_size =                     dap_enc_seed_ofb_calc_decode_size,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },

    //-KEMs(Key Exchange Mechanism)----------------------
    [DAP_ENC_KEY_TYPE_MSRLN] = {
        .name =                             "MSRLN",
        .enc =                              NULL,
        .dec =                              NULL,
        .new_callback =                     dap_enc_msrln_key_new,
        .delete_callback =                  dap_enc_msrln_key_delete,
        .new_generate_callback =            dap_enc_msrln_key_generate,
        .gen_bob_shared_key =               dap_enc_msrln_gen_bob_shared_key,
        .gen_alice_shared_key =             dap_enc_msrln_gen_alice_shared_key,
        .new_from_data_public_callback =    dap_enc_msrln_key_new_from_data_public,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_KEM_KYBER512] = {
        .name =                             "KYBER",
        .enc =                              NULL,
        .dec =                              NULL,
        .new_callback =                     dap_enc_kyber512_key_new,
        .delete_callback =                  dap_enc_kyber512_key_delete,
        .new_generate_callback =            dap_enc_kyber512_key_generate,
        .gen_bob_shared_key =               dap_enc_kyber512_gen_bob_shared_key,
        .gen_alice_shared_key =             dap_enc_kyber512_gen_alice_shared_key,
        .new_from_data_public_callback =    dap_enc_kyber512_key_new_from_data_public,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM]={
        .name =                             "NEWHOPE_CPA_KEM",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .gen_bob_shared_key =               dap_enc_newhope_gen_bob_shared_key,
        .gen_alice_shared_key =             dap_enc_newhope_gen_alice_shared_key,
        .new_callback =                     dap_enc_newhope_kem_key_new,
        .delete_callback =                  dap_enc_newhope_kem_key_delete,
        .new_generate_callback =            dap_enc_newhope_kem_key_new_generate,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    //------Signatures---------------------------
    [DAP_ENC_KEY_TYPE_SIG_PICNIC]={
        .name =                             "PICNIC",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL, // dap_enc_picnic_enc_na
        .dec_na =                           NULL,// dap_enc_picnic_dec_na
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .new_callback =                     dap_enc_sig_picnic_key_new,
        .gen_key_public =                   NULL,
        .delete_callback =                  dap_enc_sig_picnic_key_delete,
        .new_generate_callback =            dap_enc_sig_picnic_key_new_generate,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
        .sign_get =                         dap_enc_sig_picnic_get_sign,
        .sign_verify =                      dap_enc_sig_picnic_verify_sign,
        .ser_priv_key_size =                NULL,
        .ser_pub_key_size =                 NULL,
        .deser_sign_size =                  dap_enc_sig_picnic_deser_sig_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_BLISS]={
        .name =                             "SIG_BLISS",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .sign_get =                         dap_enc_sig_bliss_get_sign,
        .sign_verify =                      dap_enc_sig_bliss_verify_sign,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
    
        .new_callback =                     dap_enc_sig_bliss_key_new,
        .new_generate_callback =            dap_enc_sig_bliss_key_new_generate,
        .gen_key_public =                   dap_enc_sig_bliss_key_pub_output,

        .delete_callback =                  dap_enc_sig_bliss_key_delete,
        .del_sign =                         bliss_signature_delete,
        .del_pub_key =                      bliss_b_public_key_delete,
        .del_priv_key =                     bliss_b_private_key_delete,

        .ser_sign =                         dap_enc_sig_bliss_write_signature,
        .ser_priv_key =                     dap_enc_sig_bliss_write_private_key,
        .ser_pub_key =                      dap_enc_sig_bliss_write_public_key,
        .ser_priv_key_size =                dap_enc_sig_bliss_ser_private_key_size,
        .ser_pub_key_size =                 dap_enc_sig_bliss_ser_public_key_size,

        .deser_sign =                       dap_enc_sig_bliss_read_signature,
        .deser_priv_key =                   dap_enc_sig_bliss_read_private_key,
        .deser_pub_key =                    dap_enc_sig_bliss_read_public_key,
        .deser_sign_size =                  dap_enc_sig_bliss_deser_sig_size,
        .deser_pub_key_size =               dap_enc_sig_bliss_deser_public_key_size,
        .deser_priv_key_size =              dap_enc_sig_bliss_deser_private_key_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_TESLA]={
        .name =                             "SIG_TESLA",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .gen_key_public =                   NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
    
        .new_callback =                     dap_enc_sig_tesla_key_new,
        .new_generate_callback =            dap_enc_sig_tesla_key_new_generate,

        .delete_callback =                  dap_enc_sig_tesla_key_delete,
        .del_sign =                         tesla_signature_delete,
        .del_pub_key =                      tesla_public_key_delete,
        .del_priv_key =                     tesla_private_key_delete,
        
        .sign_get =                         dap_enc_sig_tesla_get_sign,
        .sign_verify =                      dap_enc_sig_tesla_verify_sign,
        .ser_sign =                         dap_enc_sig_tesla_write_signature,
        .ser_priv_key =                     dap_enc_sig_tesla_write_private_key,
        .ser_pub_key =                      dap_enc_sig_tesla_write_public_key,
        .ser_priv_key_size =                dap_enc_sig_tesla_ser_private_key_size,
        .ser_pub_key_size =                 dap_enc_sig_tesla_ser_public_key_size,

        .deser_sign =                       dap_enc_sig_tesla_read_signature,
        .deser_priv_key =                   dap_enc_sig_tesla_read_private_key,
        .deser_pub_key =                    dap_enc_sig_tesla_read_public_key,
        .deser_sign_size =                  dap_enc_sig_tesla_deser_sig_size,
        .deser_pub_key_size =               dap_enc_sig_tesla_deser_public_key_size,
        .deser_priv_key_size =              dap_enc_sig_tesla_deser_private_key_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_DILITHIUM]={
        .name =                             "SIG_DILITHIUM",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .gen_key_public =                   NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,

        .new_callback =                     dap_enc_sig_dilithium_key_new,
        .new_generate_callback =            dap_enc_sig_dilithium_key_new_generate,
        
        .delete_callback =                  dap_enc_sig_dilithium_key_delete,
        .del_sign =                         dilithium_signature_delete,
        .del_pub_key =                      dilithium_public_key_delete,
        .del_priv_key =                     dilithium_private_key_delete,

        .sign_get =                         dap_enc_sig_dilithium_get_sign,
        .sign_verify =                      dap_enc_sig_dilithium_verify_sign,
    
        .ser_sign =                         dap_enc_sig_dilithium_write_signature,
        .ser_priv_key =                     dap_enc_sig_dilithium_write_private_key,
        .ser_pub_key =                      dap_enc_sig_dilithium_write_public_key,
        .ser_pub_key_size =                 dap_enc_sig_dilithium_ser_public_key_size,
        .ser_priv_key_size =                dap_enc_sig_dilithium_ser_private_key_size,

        .deser_sign =                       dap_enc_sig_dilithium_read_signature,
        .deser_priv_key =                   dap_enc_sig_dilithium_read_private_key,
        .deser_pub_key =                    dap_enc_sig_dilithium_read_public_key,
        .deser_sign_size =                  dap_enc_sig_dilithium_deser_sig_size,
        .deser_pub_key_size =               dap_enc_sig_dilithium_deser_public_key_size,
        .deser_priv_key_size =              dap_enc_sig_dilithium_deser_private_key_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_RINGCT20]={
        .name =                             "SIG_RINGCT20",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           dap_enc_sig_ringct20_get_sign_with_pb_list,//dap_enc_sig_ringct20_get_sign,
        .dec_na =                           dap_enc_sig_ringct20_verify_sign,
        .dec_na_ext =                       dap_enc_sig_ringct20_verify_sign_with_pbk_list,
        .gen_key_public =                   NULL,
        .ser_pub_key_size =                 NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .new_callback =                     dap_enc_sig_ringct20_key_new,
        .delete_callback =                  dap_enc_sig_ringct20_key_delete,
        .new_generate_callback =            dap_enc_sig_ringct20_key_new_generate,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
        .sign_get =                         NULL,
        .sign_verify =                      NULL
    },
    [DAP_ENC_KEY_TYPE_SIG_FALCON]={
        .name =                             "SIG_FALCON",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .gen_key_public =                   NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,

        .new_callback =                     dap_enc_sig_falcon_key_new,
        .new_generate_callback =            dap_enc_sig_falcon_key_new_generate,
    
        .delete_callback =                  dap_enc_sig_falcon_key_delete,
        .del_sign =                         falcon_signature_delete,
        .del_pub_key =                      falcon_public_key_delete,
        .del_priv_key =                     falcon_private_key_delete,
    
        .sign_get =                         dap_enc_sig_falcon_get_sign,
        .sign_verify =                      dap_enc_sig_falcon_verify_sign,

        .ser_sign =                         dap_enc_sig_falcon_write_signature,
        .ser_priv_key =                     dap_enc_sig_falcon_write_private_key,
        .ser_pub_key =                      dap_enc_sig_falcon_write_public_key,
        .ser_priv_key_size =                dap_enc_sig_falcon_ser_private_key_size,
        .ser_pub_key_size =                 dap_enc_sig_falcon_ser_public_key_size,

        .deser_sign =                       dap_enc_sig_falcon_read_signature,
        .deser_priv_key =                   dap_enc_sig_falcon_read_private_key,
        .deser_pub_key =                    dap_enc_sig_falcon_read_public_key,
        .deser_sign_size =                  dap_enc_sig_falcon_deser_sig_size,
        .deser_pub_key_size =               dap_enc_sig_falcon_deser_public_key_size,
        .deser_priv_key_size =              dap_enc_sig_falcon_deser_private_key_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS]={
        .name =                             "SIG_SPHINCSPLUS",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           dap_enc_sig_sphincsplus_get_sign_msg,
        .dec_na =                           dap_enc_sig_sphincsplus_open_sign_msg,
        .gen_key_public =                   NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
    
        .new_callback =                     dap_enc_sig_sphincsplus_key_new,
        .new_generate_callback =            dap_enc_sig_sphincsplus_key_new_generate,
        .delete_callback =                  dap_enc_sig_sphincsplus_key_delete,
    
        .del_sign =                         sphincsplus_signature_delete,
        .del_pub_key =                      sphincsplus_public_key_delete,
        .del_priv_key =                     sphincsplus_private_key_delete,

        .sign_get =                         dap_enc_sig_sphincsplus_get_sign,
        .sign_verify =                      dap_enc_sig_sphincsplus_verify_sign,

        .ser_sign =                         dap_enc_sig_sphincsplus_write_signature,
        .ser_priv_key =                     dap_enc_sig_sphincsplus_write_private_key,
        .ser_pub_key =                      dap_enc_sig_sphincsplus_write_public_key,
        .ser_priv_key_size =                dap_enc_sig_sphincsplus_ser_private_key_size,
        .ser_pub_key_size =                 dap_enc_sig_sphincsplus_ser_public_key_size,

        .deser_sign =                       dap_enc_sig_sphincsplus_read_signature,
        .deser_priv_key =                   dap_enc_sig_sphincsplus_read_private_key,
        .deser_pub_key =                    dap_enc_sig_sphincsplus_read_public_key,
        .deser_sign_size =                  dap_enc_sig_sphincsplus_deser_sig_size,
        .deser_pub_key_size =               dap_enc_sig_sphincsplus_deser_public_key_size,
        .deser_priv_key_size =              dap_enc_sig_sphincsplus_deser_private_key_size,
    },
    [DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED]={
        .name =                             "MULTI_CHAINED",
        .enc =                              NULL,
        .dec =                              NULL,
        .enc_na =                           NULL,
        .dec_na =                           NULL,
        .gen_key_public =                   NULL,
        .gen_bob_shared_key =               NULL,
        .gen_alice_shared_key =             NULL,
        .enc_out_size =                     NULL,
        .dec_out_size =                     NULL,
    
        .new_callback =                     dap_enc_sig_multisign_key_new,
        .new_generate_callback =            dap_enc_sig_multisign_key_new_generate,

        .delete_callback =                  dap_enc_sig_multisign_key_delete,
        .del_pub_key =                      NULL,
        .del_priv_key =                     NULL,

        .sign_get =                         dap_enc_sig_multisign_get_sign,
        .sign_verify =                      dap_enc_sig_multisign_verify_sign,

        .ser_sign =                         dap_enc_sig_multisign_write_signature,
        .ser_priv_key =                     NULL,
        .ser_pub_key =                      NULL,
        .ser_priv_key_size =                NULL,
        .ser_pub_key_size =                 NULL,

        .deser_sign =                       dap_enc_sig_multisign_read_signature,
        .deser_priv_key =                   NULL,
        .deser_pub_key =                    NULL,
        .deser_sign_size =                  dap_enc_sig_multisign_deser_sig_size,
        .deser_pub_key_size =               NULL,
        .deser_priv_key_size =              NULL,
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
uint8_t *dap_enc_key_serialize_sign(dap_enc_key_type_t a_key_type, uint8_t *a_sign, size_t *a_sign_len)
{
    uint8_t *l_data = NULL;
    switch (a_key_type) {
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED:
            l_data = s_callbacks[a_key_type].ser_sign(a_sign, a_sign_len);
            break;
        default:
            DAP_NEW_Z_SIZE_RET_VAL(l_data, uint8_t, *a_sign_len, NULL, NULL);
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
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
    case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED:
        l_data = s_callbacks[a_key_type].deser_sign(a_sign, *a_sign_len);
        *a_sign_len = s_callbacks[a_key_type].deser_sign_size(NULL);
        break;
    default:
        DAP_NEW_Z_SIZE_RET_VAL(l_data, uint8_t, *a_sign_len, NULL, NULL);
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
// sanity check
    dap_return_val_if_pass(!a_key || !a_key->priv_key_data_size, NULL);
// func work
    uint8_t *l_data = NULL;
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        l_data = s_callbacks[a_key->type].ser_priv_key(a_key->priv_key_data, a_buflen_out);
        break;
    default:
        DAP_NEW_Z_SIZE_RET_VAL(l_data, uint8_t, a_key->priv_key_data_size, NULL, NULL);
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
// sanity check
    dap_return_val_if_pass(!a_key || !a_key->pub_key_data, NULL);
// func work
    uint8_t *l_data = NULL;
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        l_data = s_callbacks[a_key->type].ser_pub_key(a_key->pub_key_data, a_buflen_out);
        break;
    default:
        DAP_NEW_Z_SIZE_RET_VAL(l_data, uint8_t, a_key->pub_key_data_size, NULL, NULL);
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
// sanity check
    dap_return_val_if_pass(!a_key || !a_buf, -1);
// func work
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        if (a_key->priv_key_data)
            s_callbacks[a_key->type].del_priv_key(a_key->priv_key_data);
        a_key->priv_key_data = s_callbacks[a_key->type].deser_priv_key(a_buf, a_buflen);
        if(!a_key->priv_key_data) {
            a_key->priv_key_data_size = 0;
            return -2;
        }
        a_key->priv_key_data_size = s_callbacks[a_key->type].deser_priv_key_size(NULL);
        break;
    default:
        DAP_DEL_Z(a_key->priv_key_data);
        a_key->priv_key_data_size = a_buflen;
        DAP_NEW_Z_SIZE_RET_VAL(a_key->priv_key_data, uint8_t, a_key->priv_key_data_size, -1, NULL);
        memcpy(a_key->priv_key_data, a_buf, a_key->priv_key_data_size);
        dap_enc_key_update(a_key);
    }
    return 0;
}

/**
 * @brief dap_enc_key_deserialize_pub_key
 * @param a_key
 * @param a_buf
 * @param a_buflen_out
 * @return 0 Ok, -1 error
 */
int dap_enc_key_deserialize_pub_key(dap_enc_key_t *a_key, const uint8_t *a_buf, size_t a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_key || !a_buf, -1);
// func work
    switch (a_key->type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        if (a_key->pub_key_data)
            s_callbacks[a_key->type].del_pub_key(a_key->pub_key_data);

        a_key->pub_key_data = s_callbacks[a_key->type].deser_pub_key(a_buf, a_buflen);
        if(!a_key->pub_key_data) {
            a_key->pub_key_data_size = 0;
            return -1;
        }
        a_key->pub_key_data_size = s_callbacks[a_key->type].deser_pub_key_size(NULL);
        break;
    default:
        DAP_DEL_Z(a_key->pub_key_data);
        a_key->pub_key_data_size = a_buflen;
        DAP_NEW_Z_SIZE_RET_VAL(a_key->pub_key_data, uint8_t, a_key->pub_key_data_size, -1, NULL);
        memcpy(a_key->pub_key_data, a_buf, a_key->pub_key_data_size);
        dap_enc_key_update(a_key);
    }
    return 0;
}

/**
 * @brief dap_enc_key_serialize
 * @param key
 * @return allocates dap_enc_key_serialize_t* dont remember use free()
 */
uint8_t *dap_enc_key_serialize(dap_enc_key_t *a_key, size_t *a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_key, NULL);
// func work
    uint64_t l_ser_skey_size = 0, l_ser_pkey_size = 0;
    uint64_t l_timestamp = a_key->last_used_timestamp;
    int32_t l_type = a_key->type;
    uint8_t *l_ser_skey = dap_enc_key_serialize_priv_key(a_key, &l_ser_skey_size);
    uint8_t *l_ser_pkey = dap_enc_key_serialize_pub_key(a_key, &l_ser_pkey_size);
    uint64_t l_buflen = sizeof(uint64_t) * 5 + sizeof(int32_t) + l_ser_skey_size + l_ser_pkey_size + a_key->_inheritor_size;
    uint8_t *l_ret = dap_serialize_multy(NULL, l_buflen, 18,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_ser_skey_size, (uint64_t)sizeof(uint64_t),
        &l_ser_pkey_size, (uint64_t)sizeof(uint64_t),
        &a_key->_inheritor_size, (uint64_t)sizeof(uint64_t),
        &l_timestamp, (uint64_t)sizeof(uint64_t),
        &l_type, (uint64_t)sizeof(int32_t),
        l_ser_skey, (uint64_t)l_ser_skey_size,
        l_ser_pkey, (uint64_t)l_ser_pkey_size,
        a_key->_inheritor, (uint64_t)a_key->_inheritor_size
    );
// out work
    DAP_DEL_MULTY(l_ser_skey, l_ser_pkey);
    a_buflen ? *a_buflen = l_buflen : 0;
    return l_ret;
}

/**
 * @brief dap_enc_key_deserialize
 * @param buf
 * @param buf_size
 * @return allocates dap_enc_key_t*. Use dap_enc_key_delete for free memory
 */
dap_enc_key_t *dap_enc_key_deserialize(const void *buf, size_t a_buf_size)
{
// sanity check
    uint64_t l_sizes_len = sizeof(uint64_t) * 5 + sizeof(int32_t);
    dap_return_val_if_pass(!buf || a_buf_size < l_sizes_len, NULL);
    int32_t l_type = DAP_ENC_KEY_TYPE_NULL;
    uint64_t l_timestamp = 0, l_ser_skey_size = 0, l_ser_pkey_size = 0, l_ser_inheritor_size = 0, l_buflen = 0;
    uint8_t *l_ser_skey = NULL, *l_ser_pkey = NULL;
// get sizes
    int l_res_des = dap_deserialize_multy((const uint8_t *)buf, l_sizes_len, 12,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_ser_skey_size, (uint64_t)sizeof(uint64_t),
        &l_ser_pkey_size, (uint64_t)sizeof(uint64_t),
        &l_ser_inheritor_size, (uint64_t)sizeof(uint64_t),
        &l_timestamp, (uint64_t)sizeof(uint64_t),
        &l_type, (uint64_t)sizeof(int32_t)
    );
    if (l_res_des) {
        log_it(L_ERROR, "Enc_key size deserialisation error");
        return NULL;
    }
// memory alloc
    dap_enc_key_t *l_ret = dap_enc_key_new(l_type);
    if (!l_ret) {
        log_it(L_ERROR, "Enc_key type deserialisation error");
        return NULL;
    }
    if (l_ser_skey_size)
        DAP_NEW_Z_SIZE_RET_VAL(l_ser_skey, uint8_t, l_ser_skey_size, NULL, l_ret);
    if (l_ser_pkey_size)
        DAP_NEW_Z_SIZE_RET_VAL(l_ser_pkey, uint8_t, l_ser_pkey_size, NULL, l_ser_skey, l_ret);
    if (l_ser_inheritor_size)
        DAP_NEW_Z_SIZE_RET_VAL(l_ret->_inheritor, void, l_ser_inheritor_size, NULL, l_ser_pkey, l_ser_skey, l_ret);
// deser keys
    l_res_des = dap_deserialize_multy((const uint8_t *)(buf + l_sizes_len), (uint64_t)(a_buf_size - l_sizes_len), 6,
        l_ser_skey, (uint64_t)l_ser_skey_size,
        l_ser_pkey, (uint64_t)l_ser_pkey_size,
        (uint8_t *)l_ret->_inheritor, (uint64_t)l_ser_inheritor_size
    );
    if (l_res_des || (l_ser_pkey_size && dap_enc_key_deserialize_pub_key(l_ret, l_ser_pkey, l_ser_pkey_size)) || (l_ser_skey_size && dap_enc_key_deserialize_priv_key(l_ret, l_ser_skey, l_ser_skey_size)) ) {
            DAP_DEL_MULTY(l_ret->_inheritor, l_ser_pkey, l_ser_skey, l_ret);
            log_it(L_ERROR, "Enc_key pub and priv keys deserialisation error");
            return NULL;
        }
// out work
    l_ret->last_used_timestamp = l_timestamp;
    l_ret->priv_key_data_size = l_ser_skey_size;
    l_ret->pub_key_data_size = l_ser_pkey_size;
    l_ret->_inheritor_size = l_ser_inheritor_size;
    return l_ret;
}

/**
 * @brief create copy to current key
 * @param a_key to copy
 * @return pointer to new key or in error NULL
 */
dap_enc_key_t *dap_enc_key_dup(dap_enc_key_t *a_key)
{
// sanity check
    dap_return_val_if_pass(!a_key || a_key->type == DAP_ENC_KEY_TYPE_INVALID, NULL);
// func work
    size_t l_buflen = 0;
    uint8_t *l_ser_key = dap_enc_key_serialize(a_key, &l_buflen);
    dap_enc_key_t *l_ret = dap_enc_key_deserialize(l_ser_key, l_buflen);
    DAP_DEL_Z(l_ser_key);
    return l_ret;
}

/**
 * @brief creating new enc_key
 * @param a_key_type to creating key
 * @return pointer to new key or NULL
 */
dap_enc_key_t *dap_enc_key_new(dap_enc_key_type_t a_key_type)
{
// sanity check
    dap_return_val_if_pass(DAP_ENC_KEY_TYPE_INVALID == a_key_type, NULL);
// memory alloc
    dap_enc_key_t * l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, dap_enc_key_t, NULL, NULL);
// func work
    if(s_callbacks[a_key_type].new_callback){
        s_callbacks[a_key_type].new_callback(l_ret);
    } else {
        l_ret->type = a_key_type;
    }
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
// sanity check
    dap_return_val_if_pass(DAP_ENC_KEY_TYPE_INVALID == a_key_type, NULL);
// func work
    dap_enc_key_t * l_ret = dap_enc_key_new(a_key_type);
    if(s_callbacks[a_key_type].new_generate_callback) {
        s_callbacks[a_key_type].new_generate_callback( l_ret, a_kex_buf, a_kex_size, a_seed, a_seed_size, a_key_size);
    }
    return l_ret;
}

/**
 * @brief dap_enc_key_update
 * @param a_key key to update
 */
void dap_enc_key_update(dap_enc_key_t *a_key)
{
// sanity check
    dap_return_if_pass(!a_key);
// func work
    switch (a_key->type) {
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
            dap_enc_sig_picnic_update(a_key);
            break;
        default:
            break;
    }
}

/**
 * @brief calc serialized private key size
 * @param a_key to calc
 * @return calced size or 0
 */
size_t dap_enc_ser_priv_key_size (dap_enc_key_t *a_key)
{
// sanity check
    dap_return_val_if_pass(!a_key, 0);
// func work
    if(s_callbacks[a_key->type].ser_priv_key_size) {
        return s_callbacks[a_key->type].ser_priv_key_size(a_key->priv_key_data);
    }
    log_it(L_WARNING, "No callback for key private size calculate");
    return a_key->priv_key_data_size;
}

/**
 * @brief calc serialized public key size
 * @param a_key to calc
 * @return calced size or 0
 */
size_t dap_enc_ser_pub_key_size(dap_enc_key_t *a_key)
{
// sanity check
    dap_return_val_if_pass(!a_key, 0);
// func work
    if(s_callbacks[a_key->type].ser_pub_key_size) {
        return s_callbacks[a_key->type].ser_pub_key_size(a_key->priv_key_data);
    }
    log_it(L_WARNING, "No callback for key public size calculate");
    return a_key->pub_key_data_size;
}

int dap_enc_gen_key_public(dap_enc_key_t *a_key, void *a_output)
{
// sanity check
    dap_return_val_if_pass(!a_key, -1);
// func work
    if(s_callbacks[a_key->type].gen_key_public) {
        return s_callbacks[a_key->type].gen_key_public(a_key, a_output);
    }
    log_it(L_ERROR, "No callback for key public generate action");
    return -2;
}

/**
 * @brief sign delete
 * @param a_key_type - key type to callback
 * @param a_sig_buf - sign buf
 */
void dap_enc_key_signature_delete(dap_enc_key_type_t a_key_type, uint8_t *a_sig_buf)
{
// sanity check
    dap_return_if_pass(DAP_ENC_KEY_TYPE_INVALID == a_key_type || !a_sig_buf);
// func work
    switch (a_key_type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        s_callbacks[a_key_type].del_sign(a_sig_buf);
        break;
    default:
        break;
    }
    DAP_DEL_Z(a_sig_buf);
}

/**
 * @brief dap_enc_key_delete
 * @param a_key to delete
 */
void dap_enc_key_delete(dap_enc_key_t * a_key)
{
// sanity check
    dap_return_if_pass(!a_key);
// func work
    if(s_callbacks[a_key->type].delete_callback) {
        s_callbacks[a_key->type].delete_callback(a_key);
    } else {
        log_it(L_ERROR, "delete callback is null. Can be leak memory!");
    }
    /* a_key->_inheritor must be cleaned in delete_callback func */
    DAP_DEL_MULTY(a_key->pub_key_data, a_key->priv_key_data, a_key);
}

/**
 * @brief calc enc size
 * @param a_key to calc
 * @param a_buf_in_size in buf size to calc
 * @return calced size or 0
 */
size_t dap_enc_key_get_enc_size(dap_enc_key_t *a_key, const size_t a_buf_in_size)
{
// sanity check
    dap_return_val_if_pass(!a_key, 0);
// func work
    if(s_callbacks[a_key->type].enc_out_size) {
        return s_callbacks[a_key->type].enc_out_size(a_buf_in_size);
    }
    log_it(L_ERROR, "enc_out_size not realize for current key type");
    return 0;
}

/**
 * @brief calc dec size
 * @param a_key to calc
 * @param a_buf_in_size in buf size to calc
 * @return calced size or 0
 */
size_t dap_enc_key_get_dec_size(dap_enc_key_t *a_key, const size_t a_buf_in_size)
{
// sanity check
    dap_return_val_if_pass(!a_key, 0);
// func work
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


size_t dap_enc_calc_signature_unserialized_size(dap_enc_key_t *a_key)
{
// sanity check
    dap_return_val_if_pass(!a_key, 0);
    size_t l_sign_size = 0;
    switch (a_key->type){
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM: 
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED:
            l_sign_size = s_callbacks[a_key->type].deser_sign_size(a_key);
            break;
#ifdef DAP_PQRL
        case DAP_ENC_KEY_TYPE_SIG_PQLR_DILITHIUM: l_sign_size = dap_pqlr_dilithium_calc_signature_size(a_key); break;
#endif
        default : return 0;
    }
    return l_sign_size;
}

/**
 * @brief create new key with merged all keys 
 * @param a_keys pointer to keys
 * @param a_count keys count
 * @return pointer to key, NULL if error
 */
dap_enc_key_t *dap_enc_merge_keys_to_multisign_key(dap_enc_key_t **a_keys, size_t a_count)
{
// sanity check
    dap_return_val_if_pass(!a_keys  || !a_count, NULL);
// memory alloc
    dap_enc_key_t *l_ret = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED);
    if (!l_ret) {
        log_it(L_ERROR, "Can't create multisign key");
        return NULL;
    }
// func work
    dap_multi_sign_params_t *l_params = dap_multi_sign_params_make(SIG_TYPE_MULTI_CHAINED, a_keys, a_count, NULL, a_count);
    dap_enc_sig_multisign_forming_keys(l_ret, l_params);
    l_ret->_pvt = l_params;
    return l_ret;
}
