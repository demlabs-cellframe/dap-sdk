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
void dap_enc_msrln_key_generate(dap_enc_key_t *a_key, const void *kex_buf,
                                size_t kex_size, const void * seed, size_t seed_size,
                                size_t key_size)
{
    (void)kex_buf; (void)kex_size;
    (void)seed; (void)seed_size; (void)key_size;
// input check
    dap_return_if_pass(!a_key);
// memory alloc
    /* alice_msg is alice's public key */
    DAP_NEW_Z_SIZE_RET(a_key->pub_key_data, void, MSRLN_PKA_BYTES, NULL);
    DAP_NEW_Z_SIZE_RET(a_key->priv_key_data, void, MSRLN_PKA_BYTES * sizeof(uint32_t), a_key->pub_key_data);
// crypto calc
    PLatticeCryptoStruct PLCS = LatticeCrypto_allocate();
    LatticeCrypto_initialize(PLCS, (RandomBytes)randombytes, MSRLN_generate_a, MSRLN_get_error);

    if (MSRLN_KeyGeneration_A((int32_t *) a_key->priv_key_data,
                              (unsigned char *) a_key->pub_key_data, PLCS) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_Z(a_key->pub_key_data);
        DAP_DEL_Z(a_key->priv_key_data);
        return;
    }
    DAP_DELETE(PLCS);
// post func work
    a_key->pub_key_data_size = MSRLN_PKA_BYTES;
    a_key->priv_key_data_size = MSRLN_SHAREDKEY_BYTES;
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
size_t dap_enc_msrln_gen_bob_shared_key(dap_enc_key_t *b_key, const void *a_pub, size_t a_pub_size, void **b_pub)
{
// input check
    dap_return_val_if_pass(!b_key || !b_pub || a_pub_size != MSRLN_PKA_BYTES, 0);
// memory alloc
    if(!b_key->priv_key_data_size) { // need allocate memory for priv key
        DAP_NEW_Z_SIZE_RET_VAL(b_key->priv_key_data, void, MSRLN_SHAREDKEY_BYTES, 0, NULL);
        b_key->priv_key_data_size = MSRLN_SHAREDKEY_BYTES;
    }
    DAP_DEL_Z(*b_pub);
    DAP_NEW_Z_SIZE_RET_VAL(*b_pub, void, MSRLN_PKB_BYTES, 0, b_key->priv_key_data);
// crypto calc
    uint8_t *l_bob_tmp_pub = *b_pub;
    PLatticeCryptoStruct PLCS = LatticeCrypto_allocate();
    LatticeCrypto_initialize(PLCS, (RandomBytes)randombytes, MSRLN_generate_a, MSRLN_get_error);
    if (MSRLN_SecretAgreement_B((unsigned char *)a_pub, (unsigned char *)b_key->priv_key_data, (unsigned char *)l_bob_tmp_pub, PLCS) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_Z(*b_pub);
        DAP_DEL_Z(b_key->priv_key_data);
        return 0;
    }
    DAP_DELETE(PLCS);
// post func work
    b_key->priv_key_data_size = MSRLN_SHAREDKEY_BYTES;
    // b_key->pub_key_data_size = MSRLN_PKB_BYTES;
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
size_t dap_enc_msrln_gen_alice_shared_key(dap_enc_key_t *a_key, const void* a_priv, const size_t b_key_len, unsigned char * b_pub)
{
// input check
    dap_return_val_if_pass(b_key_len != MSRLN_PKB_BYTES, 0);
// memory alloc
    if(a_key->priv_key_data_size == 0)// need allocate mamory for priv key
        DAP_NEW_Z_SIZE_RET_VAL(a_key->priv_key_data, void, MSRLN_SHAREDKEY_BYTES, 0, NULL);
// crypto calc
    if (MSRLN_SecretAgreement_A((unsigned char *) b_pub, (int32_t *) a_priv, (unsigned char *) a_key->priv_key_data) != CRYPTO_MSRLN_SUCCESS) {
        DAP_DEL_Z(a_key->priv_key_data);
        return 0;
    }
// post func work
    a_key->priv_key_data_size = MSRLN_SHAREDKEY_BYTES;
    return MSRLN_SHAREDKEY_BYTES;
}

/**
 * @brief dap_enc_msrln_key_new_from_data_public
 * @param a_key
 * @param a_in
 * @param a_in_size
 */
void dap_enc_msrln_key_new_from_data_public(dap_enc_key_t * a_key, const void * a_in, size_t a_in_size)
{
    (void)a_key;
    (void)a_in;
    (void)a_in_size;
}

/**
 * @brief dap_enc_msrln_key_delete
 * @param a_key
 */
void dap_enc_msrln_key_delete(dap_enc_key_t *a_key)
{
    (void) a_key;
    if(!a_key){
        return;
    }
//    DAP_DELETE(a_key);
}

/**
 * @brief dap_enc_msrln_key_public_base64
 * @param a_key
 * @return
 */
char* dap_enc_msrln_key_public_base64(dap_enc_key_t *a_key)
{
    (void)a_key;
    return NULL;
}

/**
 * @brief dap_enc_msrln_key_public_raw
 * @param a_key
 * @param a_key_public
 * @return
 */
size_t dap_enc_msrln_key_public_raw(dap_enc_key_t *a_key, void ** a_key_public)
{
    (void)a_key;
    (void)a_key_public;
    return 0;
}
