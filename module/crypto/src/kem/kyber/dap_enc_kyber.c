/**
 * @file dap_enc_kyber.c
 * @brief Backward-compatible Kyber512 KEM — delegates to ML-KEM-512.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_common.h"
#include "dap_memwipe.h"
#include "dap_enc_kyber.h"

#define LOG_TAG "dap_enc_kyber"

#define KYBER512_PK  800
#define KYBER512_SK  1632
#define KYBER512_CT  768
#define KYBER512_SS  32

int dap_mlkem512_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem512_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem512_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

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
    uint8_t *l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, KYBER512_SK),
            *l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, KYBER512_PK, l_skey);
    if (dap_mlkem512_kem_keypair(l_pkey, l_skey)) {
        DAP_DEL_MULTY(l_pkey, l_skey);
        return;
    }
    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->_inheritor_size = KYBER512_SK;
    a_key->pub_key_data_size = KYBER512_PK;
}

void dap_enc_kyber512_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_WIPE_AND_FREE(a_key->shared_key, a_key->shared_key_size);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_WIPE_AND_FREE(a_key->_inheritor, a_key->_inheritor_size);
    a_key->shared_key_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

size_t dap_enc_kyber512_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub,
        size_t a_alice_pub_size, void **a_cypher_msg)
{
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg
                           || a_alice_pub_size < KYBER512_PK, 0);
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, KYBER512_SS, 0),
            *l_cypher_msg = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, KYBER512_CT, 0, l_shared_key);
    if (dap_mlkem512_kem_enc(l_cypher_msg, l_shared_key, a_alice_pub)) {
        DAP_DEL_MULTY(l_cypher_msg, l_shared_key);
        return 0;
    }
    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_cypher_msg;
    a_bob_key->shared_key = l_shared_key;
    a_bob_key->shared_key_size = KYBER512_SS;
    return KYBER512_CT;
}

size_t dap_enc_kyber512_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
        UNUSED_ARG const void *a_alice_priv, size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg
                           || a_cypher_msg_size < KYBER512_CT, 0);
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, KYBER512_SS, 0);
    if (dap_mlkem512_kem_dec(l_shared_key, a_cypher_msg, a_alice_key->_inheritor))
        return DAP_DELETE(l_shared_key), 0;
    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared_key;
    a_alice_key->shared_key_size = KYBER512_SS;
    return a_alice_key->shared_key_size;
}
