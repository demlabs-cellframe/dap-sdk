#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "dap_enc_ecdsa.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "sig_ecdsa/ecdsa_params.h"

#define LOG_TAG "dap_enc_sig_ecdsa"



static enum DAP_ECDSA_SIGN_SECURITY _ecdsa_type = ECDSA_MIN_SIZE; // by default


//void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type)
//{
//    _ecdsa_type = type;
//}

void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key) {
    *a_key = (dap_enc_key_t) {
        .type       = DAP_ENC_KEY_TYPE_SIG_ECDSA,
        .sign_get   = dap_enc_sig_ecdsa_get_sign,
        .sign_verify= dap_enc_sig_ecdsa_verify_sign
    };
}



void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t * key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, UNUSED_ARG const void * seed, UNUSED_ARG size_t seed_size,
        UNUSED_ARG size_t key_size)
{
    unsigned char randomize[32];
    if (!randombytes(randomize, sizeof(randomize))) {
        log_it(L_CRITICAL, "Failed to gen randomness");
        return;
    }
    int retcode = 0;

    ecdsa_context_t* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    retcode = secp256k1_context_randomize(ctx, randomize);
    assert(retcode);

    key->_inheritor = ctx;
    key->_inheritor_size = secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE);
    //not sure we need this for ECDSA
    //dap_enc_sig_ecdsa_set_type(ECDSA_MAX_SPEED)

    key->priv_key_data      = DAP_NEW(ecdsa_private_key_t);
    key->priv_key_data_size = sizeof(ecdsa_private_key_t);
    do {
        if ( !randombytes(key->priv_key_data, sizeof(ecdsa_private_key_t)) ) {
            log_it(L_CRITICAL, "Failed to gen randomness");
            secp256k1_context_destroy(ctx);
            DAP_DELETE(key->priv_key_data);
            return;
        }
    } while( !secp256k1_ec_seckey_verify(ctx, key->priv_key_data) );

    key->pub_key_data       = DAP_NEW(ecdsa_public_key_t);
    key->pub_key_data_size  = sizeof(ecdsa_public_key_t);
    retcode = secp256k1_ec_pubkey_create( ctx, (ecdsa_public_key_t*)key->pub_key_data, (const unsigned char*)key->priv_key_data );
    assert(retcode);

    if(retcode) {
        log_it(L_CRITICAL, "Error generating ECDSA key pair");
        secp256k1_context_destroy(ctx);
        DAP_DEL_MULTY(key->priv_key_data, key->pub_key_data);
    }
}

int dap_enc_sig_ecdsa_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size)
{
    if (signature_size != sizeof(ecdsa_signature_t)) {
        log_it(L_ERROR, "Invalid ecdsa signature size");
        return 1;
    }
    if (key->priv_key_data_size != sizeof(ecdsa_private_key_t)) {
        log_it(L_ERROR, "Invalid ecdsa key");
        return 2;
    }
    unsigned char randomize[32];
    if (!randombytes(randomize, sizeof(randomize))) {
        log_it(L_ERROR, "Failed to generate randomness");
        return 3;
    }

    int retcode = secp256k1_context_randomize(key->_inheritor, randomize);
    assert(retcode);

    ecdsa_private_key_t *privateKey = key->priv_key_data;
    ecdsa_signature_t *sig = signature;

    retcode = secp256k1_ecdsa_sign(key->_inheritor, sig, msg, privateKey->data, NULL, NULL) - 1;
    if ( retcode )
        log_it(L_ERROR, "Failed to sign message");
    return retcode;
}

int dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size)
{
    if (signature_size != sizeof(ecdsa_signature_t)) {
        log_it(L_ERROR, "Invalid ecdsa signature size");
        return 1;
    }
    if (key->pub_key_data_size != sizeof(ecdsa_public_key_t)) {
        log_it(L_ERROR, "Invalid ecdsa key");
        return 2;
    }
    ecdsa_public_key_t *publicKey = key->pub_key_data;
    ecdsa_signature_t *sig = signature;

    int retcode = secp256k1_ecdsa_verify(key->_inheritor, sig, msg, publicKey) - 1;
    if ( retcode )
        log_it(L_ERROR, "Failed to verify signature");
    return retcode;
}

uint8_t* dap_enc_sig_ecdsa_write_public_key(void* a_key, size_t* a_buflen_out)
{
    dap_return_val_if_fail_err(a_key, NULL, "Invalid arg");
    dap_enc_key_t* l_key = (dap_enc_key_t*)a_key;
    byte_t *l_buf = DAP_NEW_SIZE(byte_t, ECDSA_PKEY_SERIALIZED_SIZE);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    size_t l_len = ECDSA_PKEY_SERIALIZED_SIZE;
    if ( 1 != secp256k1_ec_pubkey_serialize( (const ecdsa_context_t*)l_key->_inheritor, l_buf, &l_len,
                                             (const ecdsa_public_key_t*)l_key->pub_key_data, SECP256K1_EC_UNCOMPRESSED ) )
    {
        log_it(L_CRITICAL, "Failed to serialize pkey");
        DAP_DELETE(l_buf);
        return NULL;
    }
    assert(l_len == ECDSA_PKEY_SERIALIZED_SIZE);
    if (a_buflen_out)
        *a_buflen_out = ECDSA_PKEY_SERIALIZED_SIZE;
    return l_buf;
}

void* dap_enc_sig_ecdsa_read_public_key(const uint8_t* a_buf, dap_enc_key_t *a_key, size_t a_buflen) {
    dap_return_val_if_fail_err(a_buf && a_key && a_buflen == ECDSA_PKEY_SERIALIZED_SIZE, NULL, "Invalid args");
    if ( !a_key->pub_key_data )
        a_key->pub_key_data = DAP_NEW(ecdsa_public_key_t);

    return secp256k1_ec_pubkey_parse( (const ecdsa_context_t*)a_key->_inheritor,
                                      (ecdsa_public_key_t*)a_key->pub_key_data,
                                      a_buf, a_buflen ) == 1
        ? a_key->pub_key_data : ( log_it(L_CRITICAL, "Failed to deserialize pkey"), NULL );
}

uint8_t *dap_enc_sig_ecdsa_write_signature(const void *a_sign, dap_enc_key_t *a_key, size_t *a_sign_len)
{
    dap_return_val_if_fail_err(a_sign && a_key && a_sign_len, NULL, "Invalid args");
    byte_t *l_buf = DAP_NEW_SIZE(byte_t, sizeof(ecdsa_signature_t));
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    return secp256k1_ecdsa_signature_serialize_compact( (const ecdsa_context_t*)a_key->_inheritor,
                                                        l_buf, (const ecdsa_signature_t*)a_sign ) == 1
        ? l_buf : ( DAP_DELETE(l_buf), log_it(L_ERROR, "Failed to serialize sign"), NULL );  
}


void* dap_enc_sig_ecdsa_read_signature(const uint8_t* a_buf, dap_enc_key_t *a_key, size_t a_buflen)
{
    dap_return_val_if_fail_err(a_buf && a_key && a_buflen == sizeof(ecdsa_signature_t), 1, "Invalid args");
    ecdsa_signature_t *l_ret = DAP_NEW(ecdsa_signature_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    return secp256k1_ecdsa_signature_parse_compact((const ecdsa_context_t*)a_key->_inheritor, l_ret, a_buf) == 1
        ? l_ret : ( DAP_DELETE(l_ret), log_it(L_ERROR, "Failed to deserialize sign"), NULL );
}

void dap_enc_sig_ecdsa_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    memset_safe(((ecdsa_signature_t *)a_sig)->data, 0, ECDSA_SIG_SIZE);
}

void dap_enc_sig_ecdsa_private_key_delete(void* privateKey) {
    dap_return_if_pass(!privateKey);
    memset_safe( ((ecdsa_private_key_t*)privateKey)->data, 0, ECDSA_PRIVATE_KEY_SIZE);
}

void dap_enc_sig_ecdsa_public_key_delete(void* publicKey) {
    dap_return_if_pass(!publicKey);
    memset_safe( ((ecdsa_public_key_t*)publicKey)->data, 0, ECDSA_PUBLIC_KEY_SIZE);
}

void dap_enc_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t* a_key) {
        dap_enc_sig_ecdsa_private_key_delete(a_key->priv_key_data);
        dap_enc_sig_ecdsa_public_key_delete(a_key->pub_key_data);
        secp256k1_context_destroy((ecdsa_context_t*)a_key->_inheritor);
}