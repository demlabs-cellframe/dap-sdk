#include <string.h>
#include "dap_common.h"
#include "dap_enc_msrln.h"
#include "msrln/msrln.h"


#define LOG_TAG "dap_enc_msrln"

void dap_enc_msrln_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_MSRLN;
    a_key->dec = NULL;
    a_key->enc = NULL;
    a_key->gen_bob_shared_key = dap_enc_msrln_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_msrln_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;
}

///**
// * @brief dap_enc_msrln_key_new_generate
// * @param a_key Struct for new key
// * @param a_size Not used
// */
//void dap_enc_msrln_key_new_generate(dap_enc_key_t *a_key, size_t a_size)
//{
//    (void)a_size;
//    a_key = DAP_NEW(dap_enc_key_t);
//    if(a_key == NULL) {
//        log_it(L_ERROR, "Can't allocate memory for key");
//        return;
//    }

//    a_key->type = DAP_ENC_KEY_TYPE_MSRLN;
//    a_key->dec = dap_enc_msrln_decode;
//    a_key->enc = dap_enc_msrln_encode;
//    a_key->_inheritor = DAP_NEW_Z(dap_enc_msrln_key_t);
//    //a_key->delete_callback = dap_enc_msrln_key_delete;
//}

/**
 * @brief dap_enc_msrln_key_generate
 * @param a_key
 * @param kex_buf
 * @param kex_size
 * @param seed
 * @param seed_size
 * @param key_size
 * @details allocate memory and generate private and public key
 */
void dap_enc_msrln_key_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
                                UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed,
                                UNUSED_ARG size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
// sanity check
    dap_return_if_pass(!a_key);
    uint8_t *l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, MSRLN_PKA_BYTES * sizeof(int32_t)),
            *l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, MSRLN_PKA_BYTES, l_skey);
    PLatticeCryptoStruct PLCS = LatticeCrypto_allocate();
    LatticeCrypto_initialize(PLCS, (RandomBytes)randombytes, MSRLN_generate_a, MSRLN_get_error);
    if (MSRLN_KeyGeneration_A((int32_t *) l_skey, l_pkey, PLCS) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_MULTY(l_skey, l_pkey, PLCS);
        return;
    }
    DAP_DELETE(PLCS);
// post func work, change in args only after all pass
    DAP_DEL_MULTY(a_key->priv_key_data, a_key->pub_key_data);
    a_key->priv_key_data = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->priv_key_data_size = MSRLN_SHAREDKEY_BYTES;
    a_key->pub_key_data_size = MSRLN_PKA_BYTES;
    return;
}


/**
 * @brief dap_enc_msrln_gen_bob_shared_key
 * @param b_key
 * @param a_pub
 * @param a_pub_size
 * @param b_pub
 * @return
 */
size_t dap_enc_msrln_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub, size_t a_alice_pub_size, void **a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg || a_alice_pub_size != MSRLN_PKA_BYTES, 0);
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, MSRLN_SHAREDKEY_BYTES, 0),
            *l_cypher_msg = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, MSRLN_PKB_BYTES, 0, l_shared_key);
// crypto calc
    PLatticeCryptoStruct PLCS = LatticeCrypto_allocate();
    LatticeCrypto_initialize(PLCS, (RandomBytes)randombytes, MSRLN_generate_a, MSRLN_get_error);
    if (MSRLN_SecretAgreement_B((unsigned char *)a_alice_pub, l_shared_key, l_cypher_msg, PLCS) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_MULTY(l_cypher_msg, l_shared_key, PLCS);
        return 0;
    }
    DAP_DEL_Z(PLCS);
// post func work, change in args only after all pass
    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_cypher_msg;
    a_bob_key->shared_key = l_shared_key;
    a_bob_key->shared_key_size = MSRLN_SHAREDKEY_BYTES;
    return MSRLN_PKB_BYTES;
}

/**
 * @brief dap_enc_msrln_decode
 * @param k
 * @param alice_msg
 * @param alice_msg_len
 * @param bob_msg
 * @param bob_msg_len
 * @param key
 * @param key_len
 * @return
 */
size_t dap_enc_msrln_gen_alice_shared_key(dap_enc_key_t *a_alice_key, const void *a_alice_priv,
                               size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_alice_key || !a_alice_priv || !a_cypher_msg || a_cypher_msg_size < MSRLN_PKB_BYTES, 0);
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, MSRLN_SHAREDKEY_BYTES, 0);
// crypto calc
    if (MSRLN_SecretAgreement_A(a_cypher_msg, (int32_t *) a_alice_priv, l_shared_key) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_Z(l_shared_key);
        return 0;
    }
// post func work
    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared_key;
    a_alice_key->shared_key_size = MSRLN_SHAREDKEY_BYTES;
    return a_alice_key->shared_key_size;
}

/**
 * @brief dap_enc_msrln_key_new_from_data_public
 * @param a_key
 * @param a_in
 * @param a_in_size
 */
void dap_enc_msrln_key_new_from_data_public(UNUSED_ARG dap_enc_key_t *a_key, UNUSED_ARG const void *a_in, UNUSED_ARG size_t a_in_size)
{

}

/**
 * @brief dap_enc_msrln_key_delete
 * @param a_key
 */
void dap_enc_msrln_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->priv_key_data);
    DAP_DEL_Z(a_key->pub_key_data);
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
}

/**
 * @brief dap_enc_msrln_key_public_base64
 * @param a_key
 * @return
 */
char *dap_enc_msrln_key_public_base64(UNUSED_ARG dap_enc_key_t *a_key)
{
    return NULL;
}

/**
 * @brief dap_enc_msrln_key_public_raw
 * @param a_key
 * @param a_key_public
 * @return
 */
size_t dap_enc_msrln_key_public_raw(UNUSED_ARG dap_enc_key_t *a_key, UNUSED_ARG void **a_key_public)
{
    return 0;
}
