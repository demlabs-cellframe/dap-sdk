#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "dap_enc_shipovnik.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "sig_shipovnik/shipovnik_params.h"

#define LOG_TAG "dap_enc_sig_shipovnik"

static enum DAP_SHIPOVNIK_SIGN_SECURITY _shipovnik_type = SHIPOVNIK_MAX_SPEED; // by default

void dap_enc_sig_shipovnik_key_new(dap_enc_key_t *a_key) {
    if (!a_key)
        return;
    *a_key = (dap_enc_key_t) {
        .type = DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK,
        .sign_get = dap_enc_sig_shipovnik_get_sign,
        .sign_verify = dap_enc_sig_shipovnik_verify_sign
    };
}

void dap_enc_sig_shipovnik_key_new_generate(dap_enc_key_t * key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, const void * seed, size_t seed_size,
        UNUSED_ARG size_t key_size)
{
    key->priv_key_data_size =SHIPOVNIK_SECRETKEYBYTES;
    key->pub_key_data_size = SHIPOVNIK_PUBLICKEYBYTES;
    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_data_size);
    shipovnik_generate_keys(key->priv_key_data,key->pub_key_data);
}

int dap_enc_sig_shipovnik_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size)
{
    if (signature_size != SHIPOVNIK_SIGBYTES) {
        log_it(L_ERROR, "Invalid shipovnik signature size");
        return -10;
    }

    if (key->priv_key_data_size != SHIPOVNIK_SECRETKEYBYTES) {
        log_it(L_ERROR, "Invalid shipovnik secret key size");
        return -11;
    }
    shipovnik_sign(key->priv_key_data, msg, msg_size, signature, &signature_size);
    return signature_size ? 0 : log_it(L_ERROR, "Failed to sign message"), -1;
}

int dap_enc_sig_shipovnik_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size)
{
    if (key->pub_key_data_size != SHIPOVNIK_PUBLICKEYBYTES) {
        log_it(L_ERROR, "Invalid shipovnik public key size");
        return -12;
    }
    int l_ret = shipovnik_verify(key->pub_key_data, signature, msg, msg_size);
    return l_ret ? 0 : log_it(L_ERROR, "Failed to verify message, error %d, l_ret"), l_ret;
}

void dap_enc_sig_shipovnik_signature_delete(void *a_sig){
    dap_return_if_fail(!!a_sig);
    memset_safe(((uint8_t*)a_sig), 0, SHIPOVNIK_SIGBYTES);
}

void dap_enc_sig_shipovnik_private_key_delete(void* privateKey) {
    dap_return_if_fail(!!privateKey);
    memset_safe((uint8_t*)privateKey, 0, SHIPOVNIK_SECRETKEYBYTES);
}

void dap_enc_sig_shipovnik_public_key_delete(void* publicKey) {
    dap_return_if_fail(!!publicKey);
    memset_safe((uint8_t*)publicKey, 0, SHIPOVNIK_PUBLICKEYBYTES);
}

void dap_enc_sig_shipovnik_private_and_public_keys_delete(dap_enc_key_t *a_key) {
        dap_enc_sig_shipovnik_private_key_delete(a_key->priv_key_data);
        dap_enc_sig_shipovnik_public_key_delete(a_key->pub_key_data);
}



