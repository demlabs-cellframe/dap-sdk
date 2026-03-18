/*
 * DAP KEM wrapper for Streamlined NTRU Prime 761.
 */

#include <string.h>
#include "dap_enc_ntru_prime.h"
#include "dap_sntrup761.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_ntru_prime"

void dap_enc_ntru_prime_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME;
    a_key->dec = NULL;
    a_key->enc = NULL;
    a_key->gen_bob_shared_key = dap_enc_ntru_prime_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_ntru_prime_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;
}

void dap_enc_ntru_prime_key_generate(dap_enc_key_t *a_key,
        UNUSED_ARG const void *kex_buf, UNUSED_ARG size_t kex_size,
        UNUSED_ARG const void *seed, UNUSED_ARG size_t seed_size,
        UNUSED_ARG size_t key_size)
{
    dap_return_if_pass(!a_key);
    uint8_t *l_pk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, SNTRUP761_PUBLICKEYBYTES);
    uint8_t *l_sk = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, SNTRUP761_SECRETKEYBYTES, l_pk);

    if (sntrup761_keypair(l_pk, l_sk)) {
        DAP_DEL_MULTY(l_pk, l_sk);
        log_it(L_ERROR, "NTRU Prime key generation failed");
        return;
    }

    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_sk;
    a_key->pub_key_data = l_pk;
    a_key->_inheritor_size = SNTRUP761_SECRETKEYBYTES;
    a_key->pub_key_data_size = SNTRUP761_PUBLICKEYBYTES;
}

void dap_enc_ntru_prime_key_new_from_data_public(UNUSED_ARG dap_enc_key_t *a_key,
        UNUSED_ARG const void *a_in, UNUSED_ARG size_t a_in_size)
{
}

void dap_enc_ntru_prime_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->shared_key);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_DEL_Z(a_key->_inheritor);
    a_key->shared_key_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

size_t dap_enc_ntru_prime_gen_bob_shared_key(dap_enc_key_t *a_bob_key,
        const void *a_alice_pub, size_t a_alice_pub_size, void **a_cypher_msg)
{
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg
            || a_alice_pub_size < SNTRUP761_PUBLICKEYBYTES, 0);

    uint8_t *l_ss = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, SNTRUP761_BYTES, 0);
    uint8_t *l_ct = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, SNTRUP761_CIPHERTEXTBYTES, 0, l_ss);

    if (sntrup761_enc(l_ct, l_ss, (const uint8_t *)a_alice_pub)) {
        DAP_DEL_MULTY(l_ct, l_ss);
        return 0;
    }

    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_ct;
    a_bob_key->shared_key = l_ss;
    a_bob_key->shared_key_size = SNTRUP761_BYTES;
    return SNTRUP761_CIPHERTEXTBYTES;
}

size_t dap_enc_ntru_prime_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
        UNUSED_ARG const void *a_alice_priv,
        size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg
            || a_cypher_msg_size < SNTRUP761_CIPHERTEXTBYTES, 0);

    uint8_t *l_ss = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, SNTRUP761_BYTES, 0);

    if (sntrup761_dec(l_ss, a_cypher_msg, (const uint8_t *)a_alice_key->_inheritor)) {
        DAP_DELETE(l_ss);
        return 0;
    }

    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_ss;
    a_alice_key->shared_key_size = SNTRUP761_BYTES;
    return a_alice_key->shared_key_size;
}
