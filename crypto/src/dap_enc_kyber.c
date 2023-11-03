#include "dap_common.h"
#include "dap_enc_kyber.h"


#define LOG_TAG "dap_enc_kyber"
#include "symmetric.h"
/**
 * @brief dap_enc_kyber_key_new
 * @param a_key
 */
void dap_enc_kyber512_key_new( dap_enc_key_t* a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_KEM_KYBER512;
    a_key->dec = NULL;
    a_key->enc = NULL;
    a_key->gen_bob_shared_key = dap_enc_kyber512_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_kyber512_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
}

/**
 * @brief dap_enc_kyber_key_new_from_data_public
 * @param a_key
 * @param a_in
 * @param a_in_size
 */
void dap_enc_kyber512_key_new_from_data_public(dap_enc_key_t * a_key, const void * a_in, size_t a_in_size)
{
    (void)a_key;
    (void)a_in;
    (void)a_in_size;
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
void dap_enc_kyber512_key_generate( dap_enc_key_t * a_key, const void *kex_buf,
                                size_t kex_size, const void * seed, size_t seed_size,
                                size_t key_size)
{
    (void)kex_buf; (void)kex_size;
    (void)seed; (void)seed_size; (void)key_size;

    DAP_NEW_Z_SIZE_RET(a_key->_inheritor, uint8_t, CRYPTO_SECRETKEYBYTES, NULL);
    DAP_NEW_Z_SIZE_RET(a_key->pub_key_data, uint8_t, CRYPTO_PUBLICKEYBYTES, a_key->_inheritor);
    
    a_key->_inheritor_size = CRYPTO_SECRETKEYBYTES;
    a_key->pub_key_data_size = CRYPTO_PUBLICKEYBYTES;

    crypto_kem_keypair(a_key->pub_key_data, a_key->_inheritor);

}

/**
 * @brief dap_enc_kyber_key_delete
 * @param a_key
 */
void dap_enc_kyber512_key_delete(struct dap_enc_key* a_key)
{
    if( a_key->_inheritor)
        DAP_DELETE(a_key->_inheritor);
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
size_t dap_enc_kyber512_gen_bob_shared_key (dap_enc_key_t *a_key, const void *a_pub,
                                           size_t a_cypher_msg_size, void ** a_cypher_msg)
{
    dap_return_val_if_pass(!a_cypher_msg || a_cypher_msg_size < CRYPTO_CIPHERTEXTBYTES, 0);
// memory free and alloc
    if (a_key->shared_key) {
        DAP_DELETE(a_key->shared_key);
        a_key->shared_key_size = 0;
    }
    DAP_NEW_Z_SIZE_RET_VAL(a_key->shared_key, uint8_t, CRYPTO_BYTES, 0, NULL);
    if (!*a_cypher_msg)
        DAP_NEW_Z_SIZE_RET_VAL(*a_cypher_msg, uint8_t, CRYPTO_CIPHERTEXTBYTES, 0, a_key->shared_key);
// func work
    if(crypto_kem_enc( *a_cypher_msg, a_key->shared_key,  a_pub )) {
        DAP_DEL_Z(a_key->shared_key);
        return 0;
    }
    a_key->shared_key_size = CRYPTO_BYTES;
    return a_key->shared_key_size;
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
size_t dap_enc_kyber512_gen_alice_shared_key(struct dap_enc_key *a_key, const void *a_priv,
                                             size_t a_cypher_msg_size, byte_t *a_cypher_msg)
{
    dap_return_val_if_pass(!a_key, 0);

    if (a_key->shared_key) {
        DAP_DELETE(a_key->shared_key);
        a_key->shared_key_size = 0;
    }
    DAP_NEW_Z_SIZE_RET_VAL(a_key->shared_key, uint8_t, CRYPTO_BYTES, 0, NULL);

    if (crypto_kem_dec(  a_key->shared_key, a_cypher_msg, a_key->_inheritor) ) {
        DAP_DEL_Z(a_key->shared_key);
        return 0;
    }
    a_key->shared_key_size = CRYPTO_BYTES;
    return a_key->shared_key_size;
}
