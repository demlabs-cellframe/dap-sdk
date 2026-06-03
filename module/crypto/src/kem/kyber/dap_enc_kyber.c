/**
 * @file dap_enc_kyber.c
 * @brief Backward-compatible Kyber512 KEM — delegates to dap_kem ML-KEM-512
 *        with persistent ctx for optimized decaps.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_common.h"
#include "dap_memwipe.h"
#include "dap_enc_kyber.h"
#include "dap_kem.h"

#define LOG_TAG "dap_enc_kyber"

#define KYBER512_ALG DAP_KEM_ALG_ML_KEM_512

typedef struct {
    dap_kem_ctx_t ctx;
} dap_enc_kyber_priv_t;

void dap_enc_kyber512_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_KEM_KYBER512;
    a_key->dec = NULL;
    a_key->enc = NULL;
    a_key->gen_bob_shared_key = dap_enc_kyber512_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_kyber512_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;
}

void dap_enc_kyber512_key_new_from_data_public(UNUSED_ARG dap_enc_key_t *a_key,
        UNUSED_ARG const void *a_in, UNUSED_ARG size_t a_in_size) {}

void dap_enc_kyber512_key_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
        UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed,
        UNUSED_ARG size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(!a_key);

    size_t l_pk_sz = dap_kem_publickey_size(KYBER512_ALG);
    size_t l_sk_sz = dap_kem_secretkey_size(KYBER512_ALG);

    uint8_t *l_pk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_pk_sz),
            *l_sk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_sk_sz, l_pk);

    if (dap_kem_keypair(KYBER512_ALG, l_pk, l_sk)) {
        DAP_DEL_MULTY(l_pk, l_sk);
        return;
    }

    dap_enc_kyber_priv_t *l_priv = DAP_NEW_Z(dap_enc_kyber_priv_t);
    if (!l_priv) {
        dap_memwipe(l_sk, l_sk_sz);
        DAP_DEL_MULTY(l_pk, l_sk);
        return;
    }
    if (dap_kem_ctx_create(&l_priv->ctx, KYBER512_ALG, l_pk, l_sk)) {
        dap_memwipe(l_sk, l_sk_sz);
        DAP_DEL_MULTY(l_pk, l_sk, l_priv);
        return;
    }

    dap_memwipe(l_sk, l_sk_sz);
    DAP_DELETE(l_sk);

    if (a_key->_inheritor) {
        dap_enc_kyber_priv_t *l_old = a_key->_inheritor;
        dap_kem_ctx_destroy(&l_old->ctx);
        DAP_DELETE(l_old);
    }
    DAP_DEL_Z(a_key->pub_key_data);

    a_key->_inheritor = l_priv;
    a_key->_inheritor_size = sizeof(dap_enc_kyber_priv_t);
    a_key->pub_key_data = l_pk;
    a_key->pub_key_data_size = l_pk_sz;
}

void dap_enc_kyber512_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_WIPE_AND_FREE(a_key->shared_key, a_key->shared_key_size);
    DAP_DEL_Z(a_key->pub_key_data);
    if (a_key->_inheritor) {
        dap_enc_kyber_priv_t *l_priv = a_key->_inheritor;
        dap_kem_ctx_destroy(&l_priv->ctx);
        DAP_DELETE(l_priv);
        a_key->_inheritor = NULL;
    }
    a_key->shared_key_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

size_t dap_enc_kyber512_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub,
        size_t a_alice_pub_size, void **a_cypher_msg)
{
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg
                           || a_alice_pub_size < dap_kem_publickey_size(KYBER512_ALG), 0);

    size_t l_ct_sz = dap_kem_ciphertext_size(KYBER512_ALG);
    size_t l_ss_sz = dap_kem_sharedsecret_size(KYBER512_ALG);

    uint8_t *l_shared = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ss_sz, 0),
            *l_ct     = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ct_sz, 0, l_shared);

    if (dap_kem_encaps(KYBER512_ALG, l_ct, l_shared, a_alice_pub)) {
        DAP_DEL_MULTY(l_ct, l_shared);
        return 0;
    }

    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_ct;
    a_bob_key->shared_key = l_shared;
    a_bob_key->shared_key_size = l_ss_sz;
    return l_ct_sz;
}

size_t dap_enc_kyber512_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
        UNUSED_ARG const void *a_alice_priv, size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg
                           || a_cypher_msg_size < dap_kem_ciphertext_size(KYBER512_ALG), 0);

    dap_enc_kyber_priv_t *l_priv = a_alice_key->_inheritor;
    dap_return_val_if_pass(!l_priv, 0);

    size_t l_ss_sz = dap_kem_sharedsecret_size(KYBER512_ALG);
    uint8_t *l_shared = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_ss_sz, 0);

    if (dap_kem_ctx_decaps(l_shared, a_cypher_msg, &l_priv->ctx)) {
        DAP_DELETE(l_shared);
        return 0;
    }

    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared;
    a_alice_key->shared_key_size = l_ss_sz;
    return l_ss_sz;
}
