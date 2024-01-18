#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "dap_enc_ecdsa.h"
#include "ecdsa_params.h"
#include "dap_common.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_enc_sig_ecdsa"



static enum DAP_ECDSA_SIGN_SECURITY _ecdsa_type = ECDSA_MIN_SIZE; // by default


void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type)
{
    _ecdsa_type = type;
}

void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key) {

    a_key->type = DAP_ENC_KEY_TYPE_ECDSA;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_ecdsa_get_sign;
    a_key->sign_verify = dap_enc_sig_ecdsa_verify_sign;
}


void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t * key, const void *kex_buf,
        size_t kex_size, const void * seed, size_t seed_size,
        size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) key_size;

    int retcode;

    /*not sure we need this for ECDSA
    /*dap_enc_sig_ecdsa_set_type(ECDSA_MAX_SPEED);*/

    //int32_t type = 2;
    key->priv_key_data_size = 32;
    key->pub_key_data_size = sizeof(secp256k1_pubkey);
    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_data_size);

    secp256k1_context* ctx=sec256k1_context_create_(SECP256K1_CONTEXT_NONE);
    if (!fill_random(randomize, sizeof(randomize))) {
        printf("Failed to generate randomness\n");
        return 1;
    }

    retcode = secp256k1_context_randomize(ctx, randomize);
    assert(retcode);

    retcode = secp256k1_ec_pubkey_create(ctx, &pubkey, seckey);
    assert(retcode);

    if(retcode) {
        log_it(L_CRITICAL, "Error generating ECDSA key pair");
        secp256k1_context_destroy(ctx);
	secure_erase(seckey, sizeof(seckey));
	return;
    }
}


int dap_enc_sig_ecdsa_get_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
    if(a_sig_size < sizeof(ecdsa_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }

    return ecdsa_crypto_sign((dilithium_signature_t *)a_sig, (const unsigned char *) a_msg, a_msg_size, a_key->priv_key_data);
}


void dap_enc_sig_ecdsa_key_delete(dap_enc_key_t *a_key)
	
{
    dap_return_if_pass(!a_key);

    secp256k1_context_preallocated_destroy
    secp256k1_memczero(a_key->pub_key_data, sizeof(
    dilithium_private_and_public_keys_delete(a_key->priv_key_data, a_key->pub_key_data);

    a_key->pub_key_data = NULL;
    a_key->priv_key_data = NULL;
}
