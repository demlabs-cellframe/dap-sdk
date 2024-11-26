#include "dap_common.h"
#include "dap_enc_picnic.h"
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "picnic.h"
#include "picnic_impl.h"

#define LOG_TAG "dap_enc_picnic_sig"

/**
 * Set the mark that valid keys are present
 */
static void set_picnic_params_t(dap_enc_key_t *key)
{
    picnic_params_t *param = (key) ? (picnic_params_t*) key->_inheritor : NULL;
    if(param && key->_inheritor_size == sizeof(picnic_params_t)){
        if(key->priv_key_data)
            *param = ((picnic_privatekey_t*) key->priv_key_data)->params;
        else if(key->pub_key_data)
            *param = ((picnic_publickey_t*) key->pub_key_data)->params;
    }
}

/**
 * Check present of valid keys
 */
static bool check_picnic_params_t(dap_enc_key_t *key)
{
    picnic_params_t *param = (key) ? (picnic_params_t*) key->_inheritor : NULL;
    if(param && *param > PARAMETER_SET_INVALID && *param < PARAMETER_SET_MAX_INDEX)
        return true;
    return false;
}

uint64_t dap_enc_sig_picnic_deser_sig_size(const void *a_key)
{
    const dap_enc_key_t *l_key = a_key;
    picnic_params_t *param = (picnic_params_t*) l_key->_inheritor;
    return picnic_signature_size(*param);
}

void dap_enc_sig_picnic_key_new(dap_enc_key_t *key) {

    key->type = DAP_ENC_KEY_TYPE_SIG_PICNIC;
    key->_inheritor = calloc(sizeof(picnic_params_t), 1);
    key->_inheritor_size = sizeof(picnic_params_t);
    key->enc = NULL;
    key->enc = NULL;
    key->gen_bob_shared_key = NULL; //(dap_enc_gen_bob_shared_key) dap_enc_sig_picnic_get_sign;
    key->gen_alice_shared_key = NULL; //(dap_enc_gen_alice_shared_key) dap_enc_sig_picnic_verify_sign;
    key->sign_get = dap_enc_sig_picnic_get_sign;
    key->sign_verify = dap_enc_sig_picnic_verify_sign;
    key->priv_key_data = NULL;
    key->pub_key_data = NULL;
    key->dec_na = NULL;
    key->enc_na = NULL;
}

void dap_enc_sig_picnic_key_delete(dap_enc_key_t *key)
{
    DAP_DEL_Z(key->priv_key_data);
    DAP_DEL_Z(key->pub_key_data);
    DAP_DEL_Z(key->_inheritor);
    key->priv_key_data_size = 0;
    key->pub_key_data_size = 0;
    key->_inheritor_size = 0;
}

void dap_enc_sig_picnic_update(dap_enc_key_t *a_key)
{
    if(a_key) {
        if(!a_key->priv_key_data ||
           !picnic_validate_keypair((picnic_privatekey_t *) a_key->priv_key_data, (picnic_publickey_t *) a_key->pub_key_data))
            set_picnic_params_t(a_key);
    }
}

void dap_enc_sig_picnic_key_new_generate(dap_enc_key_t *key, const void *kex_buf, size_t kex_size,
        const void * seed, size_t seed_size, size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) key_size;
    picnic_params_t parameters;
    // Parameter name from Picnic_L1_FS = 1 to PARAMETER_SET_MAX_INDEX
    if(seed_size >= sizeof(unsigned char) && seed)
        parameters = (((unsigned char*) seed)[0] % (PARAMETER_SET_MAX_INDEX - 1)) + 1;
    else
        parameters = DAP_PICNIC_SIGN_PARAMETR;

    key->priv_key_data_size = sizeof(picnic_privatekey_t);
    key->pub_key_data_size = sizeof(picnic_publickey_t);
    key->priv_key_data = calloc(1, key->priv_key_data_size);
    key->pub_key_data = calloc(1, key->pub_key_data_size);

    picnic_keys_gen((picnic_privatekey_t *) key->priv_key_data, (picnic_publickey_t *) key->pub_key_data, parameters, seed, seed_size);
    if(!picnic_validate_keypair((picnic_privatekey_t *) key->priv_key_data, (picnic_publickey_t *) key->pub_key_data))
        set_picnic_params_t(key);
}

int dap_enc_sig_picnic_get_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_len,
        void *a_sig, size_t a_sig_len)
{
    // var init and first checks
    dap_return_val_if_pass(!check_picnic_params_t(a_key), -1);
    int l_ret = 0;
    picnic_privatekey_t* sk = a_key->priv_key_data;
    paramset_t paramset;
    dap_return_val_if_pass((l_ret = get_param_set(sk->params, &paramset)) != EXIT_SUCCESS, l_ret);
    signature_t *l_sig = DAP_NEW_Z_RET_VAL_IF_FAIL(signature_t, -1);

    allocateSignature(l_sig, &paramset);
    dap_return_val_if_pass(!l_sig, -1);

    l_ret = sign((uint32_t*) sk->data, (uint32_t*) sk->pk.ciphertext, (uint32_t*) sk->pk.plaintext, (const uint8_t*)a_msg,
            a_msg_len, l_sig, &paramset);
    if(l_ret != EXIT_SUCCESS) {
        freeSignature(l_sig, &paramset);
        DAP_DELETE(l_sig);
        return -1;
    }
    l_ret = serializeSignature(l_sig, (uint8_t*)a_sig, a_sig_len, &paramset);
    if(l_ret == -1) {
        freeSignature(l_sig, &paramset);
        DAP_DELETE(l_sig);
        return -1;
    }
//    *signature_len = ret;
    freeSignature(l_sig, &paramset);
    DAP_DELETE(l_sig);
    return 0;
}

int dap_enc_sig_picnic_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_len,
        void* a_sig, size_t a_sig_len)
{
    // var init and first checks
    dap_return_val_if_pass(!check_picnic_params_t(a_key), -1);
    int l_ret = 0;
    picnic_publickey_t* pk = a_key->pub_key_data;
    paramset_t paramset;
    dap_return_val_if_pass((l_ret = get_param_set(pk->params, &paramset)) != EXIT_SUCCESS, l_ret);
    // memory alloc
    signature_t *l_sig = DAP_NEW_Z_RET_VAL_IF_FAIL(signature_t, -1);
    allocateSignature(l_sig, &paramset);

    l_ret = deserializeSignature(l_sig, (const uint8_t*)a_sig, a_sig_len, &paramset);
    if(l_ret != EXIT_SUCCESS) {
        freeSignature(l_sig, &paramset);
        DAP_DELETE(l_sig);
        return -1;
    }

    l_ret = verify(l_sig, (uint32_t*) pk->ciphertext,
            (uint32_t*) pk->plaintext, (const uint8_t*)a_msg, a_msg_len, &paramset);
    if(l_ret != EXIT_SUCCESS) {
        /* Signature is invalid, or verify function failed */
        freeSignature(l_sig, &paramset);
        DAP_DELETE(l_sig);
        return -1;
    }

    freeSignature(l_sig, &paramset);
    DAP_DELETE(l_sig);
    return 0;
}

/*
uint8_t* dap_enc_sig_picnic_write_public_key(dap_enc_key_t *a_key, size_t *a_buflen_out)
{
    const picnic_publickey_t *l_key = a_key->pub_key_data;
    size_t buflen = picnic_get_public_key_size(l_key); // Get public key size for serialize
    uint8_t* l_buf = DAP_NEW_SIZE(uint8_t, buflen);
    // Serialize public key
    if(picnic_write_public_key(l_key, l_buf, buflen)>0){
        if(a_buflen_out)
            *a_buflen_out = buflen;
        return l_buf;
    }
    return NULL;
}

uint8_t* dap_enc_sig_picnic_read_public_key(dap_enc_key_t *a_key, uint8_t a_buf, size_t *a_buflen)
{
   const picnic_publickey_t *l_key = a_key->pub_key_data;
    size_t buflen = picnic_get_public_key_size(l_key);  Get public key size for serialize
    uint8_t* l_buf = DAP_NEW_SIZE(uint8_t, buflen);
    // Deserialize public key
    if(!picnic_read_public_key(l_key, a_l_buf, buflen)>0){
        if(a_buflen_out)
            *a_buflen_out = buflen;
        return l_buf;
    }
    return NULL;
}*/

