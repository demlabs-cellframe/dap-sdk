#include "dap_common.h"
#include "dap_enc_kyber.h"


#define LOG_TAG "dap_enc_kyber"
#include "symmetric.h"
/**
 * @brief dap_enc_kyber_key_new
 * @param a_key
 */
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

/**
 * @brief dap_enc_kyber_key_new_from_data_public
 * @param a_key
 * @param a_in
 * @param a_in_size
 */
void dap_enc_kyber512_key_new_from_data_public(UNUSED_ARG dap_enc_key_t *a_key, UNUSED_ARG const void *a_in, UNUSED_ARG size_t a_in_size)
{

}

/**
 * @brief dap_enc_msrln_key_generate
 * @param a_key
 * @param kex_buf
 * @param kex_size
 * @param seed
 * @param seed_size
 * @param key_size
 */
void dap_enc_kyber512_key_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
                                UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed, 
                                UNUSED_ARG size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(!a_key);
    uint8_t *l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, CRYPTO_SECRETKEYBYTES),
            *l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, CRYPTO_PUBLICKEYBYTES, l_skey);
    if (crypto_kem_keypair(l_pkey, l_skey)) {
        DAP_DEL_MULTY(l_pkey, l_skey);
        return;
    }
    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->_inheritor_size = CRYPTO_SECRETKEYBYTES;
    a_key->pub_key_data_size = CRYPTO_PUBLICKEYBYTES;
}

/**
 * @brief dap_enc_kyber_key_delete
 * @param a_key
 */
void dap_enc_kyber512_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->shared_key);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_DEL_Z(a_key->_inheritor);
    a_key->shared_key_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

// key pair generation and generation of shared key at Bob's side
// INPUT:
// dap_enc_key *b_key
// a_cypher_msg  ---  Alice's public key
// a_cypher_msg_size --- Alice's public key length
// OUTPUT:
// b_pub  --- Bob's public key
// b_key->priv_key_data --- shared key
// b_key->priv_key_data_size --- shared key length
size_t dap_enc_kyber512_gen_bob_shared_key (dap_enc_key_t *a_bob_key, const void *a_alice_pub, size_t a_alice_pub_size, void **a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg || a_alice_pub_size < CRYPTO_PUBLICKEYBYTES, 0);
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, CRYPTO_BYTES, 0),
            *l_cypher_msg = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, CRYPTO_CIPHERTEXTBYTES, 0, l_shared_key);
// crypto calc
    if(crypto_kem_enc(l_cypher_msg, l_shared_key, a_alice_pub)) {
        DAP_DEL_MULTY(l_cypher_msg, l_shared_key);
        return 0;
    }
// post func work, change in args only after all pass
    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_cypher_msg;
    a_bob_key->shared_key = l_shared_key;
    a_bob_key->shared_key_size = CRYPTO_BYTES;
    return CRYPTO_CIPHERTEXTBYTES;
}

// generation of shared key at Alice's side
// INPUT:
// dap_enc_key *a_key
// a_priv  --- Alice's private key
// b_pub  ---  Bob's public key
// b_pub_size --- Bob public key size
// OUTPUT:
// a_key->priv_key_data  --- shared key
// a_key->priv_key_data_size --- shared key length
size_t dap_enc_kyber512_gen_alice_shared_key(dap_enc_key_t *a_alice_key, UNUSED_ARG const void *a_alice_priv,
                               size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg || a_cypher_msg_size < CRYPTO_CIPHERTEXTBYTES, 0);
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, CRYPTO_BYTES, 0);
// crypto calc
    if ( crypto_kem_dec(l_shared_key, a_cypher_msg, a_alice_key->_inheritor) )
        return DAP_DELETE(l_shared_key), 0;
// post func work
    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared_key;
    a_alice_key->shared_key_size = CRYPTO_BYTES;
    return a_alice_key->shared_key_size;
}
