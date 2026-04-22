/**
 * @file dap_enc_newhope.c
 * @brief NewHope CPA-KEM wrapper — delegates to dap_kem API.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "dap_enc_newhope.h"
#include "dap_kem.h"
#include "dap_common.h"
#include "dap_memwipe.h"

#define LOG_TAG "dap_enc_newhope"

#define NEWHOPE_ALG DAP_KEM_ALG_NEWHOPE

DAP_NEWHOPE_SIGN_SECURITY _newhope_type = NEWHOPE_1024;

void dap_enc_newhope_pke_set_type(DAP_NEWHOPE_SIGN_SECURITY type)
{
    _newhope_type = type;
}

void dap_enc_newhope_kem_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM;
    a_key->enc = NULL;
    a_key->enc_na = NULL;
    a_key->dec_na = NULL;
    a_key->gen_bob_shared_key = dap_enc_newhope_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_newhope_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;
}

void dap_enc_newhope_kem_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
                                    UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed,
                                    UNUSED_ARG size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(!a_key);

    size_t l_pk_sz = dap_kem_publickey_size(NEWHOPE_ALG);
    size_t l_sk_sz = dap_kem_secretkey_size(NEWHOPE_ALG);

    uint8_t *l_sk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_sk_sz),
            *l_pk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_pk_sz, l_sk);

    if (dap_kem_keypair(NEWHOPE_ALG, l_pk, l_sk)) {
        DAP_DEL_MULTY(l_pk, l_sk);
        return;
    }

    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_sk;
    a_key->pub_key_data = l_pk;
    a_key->_inheritor_size = l_sk_sz;
    a_key->pub_key_data_size = l_pk_sz;
}

size_t dap_enc_newhope_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub,
        size_t a_alice_pub_size, void **a_cypher_msg)
{
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg
            || a_alice_pub_size < dap_kem_publickey_size(NEWHOPE_ALG), 0);

    size_t l_ct_sz = dap_kem_ciphertext_size(NEWHOPE_ALG);
    size_t l_ss_sz = dap_kem_sharedsecret_size(NEWHOPE_ALG);

    uint8_t *l_ss = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ss_sz, 0),
            *l_ct = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ct_sz, 0, l_ss);

    if (dap_kem_encaps(NEWHOPE_ALG, l_ct, l_ss, a_alice_pub)) {
        DAP_DEL_MULTY(l_ct, l_ss);
        return 0;
    }

    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_ct;
    a_bob_key->shared_key = l_ss;
    a_bob_key->shared_key_size = l_ss_sz;
    return l_ct_sz;
}

size_t dap_enc_newhope_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
        UNUSED_ARG const void *a_alice_priv,
        size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg
            || a_cypher_msg_size < dap_kem_ciphertext_size(NEWHOPE_ALG), 0);

    size_t l_ss_sz = dap_kem_sharedsecret_size(NEWHOPE_ALG);
    uint8_t *l_ss = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ss_sz, 0);

    if (dap_kem_decaps(NEWHOPE_ALG, l_ss, a_cypher_msg, a_alice_key->_inheritor)) {
        DAP_DELETE(l_ss);
        return 0;
    }

    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_ss;
    a_alice_key->shared_key_size = l_ss_sz;
    return l_ss_sz;
}

void dap_enc_newhope_kem_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_WIPE_AND_FREE(a_key->priv_key_data, a_key->priv_key_data_size);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_WIPE_AND_FREE(a_key->_inheritor, a_key->_inheritor_size);
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}
