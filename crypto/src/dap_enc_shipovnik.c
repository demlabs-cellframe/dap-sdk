#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "shipovnik.h"
#include "dap_enc_shipovnik.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "sig_shipovnik/shipovnik_params.h"


#define LOG_TAG "dap_enc_sig_shipovnik"


static enum DAP_SHIPOVNIK_SIGN_SECURITY _shipovnik_type = SHIPOVNIK_MIN_SIZE; // by default

void dap_enc_sig_shipovnik_key_new(dap_enc_key_t *a_key) {
    a_key->type = DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_shipovnik_get_sign;
    a_key->sign_verify = dap_enc_sig_shipovnik_verify_sign;
}


void dap_enc_sig_shipovnik_key_new_generate(dap_enc_key_t * key, const void *kex_buf,
        size_t kex_size, const void * seed, size_t seed_size,
        size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) key_size;


    key->priv_key_data_size =SHIPOVNIK_SECRETKEYBYTES;
    key->pub_key_data_size = SHIPOVNIK_PUBLICKEYBYTES;

    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_data_size);


    shipovnik_generate_keys(key->priv_key_data,key->pub_key_data);

//    if (retcode != 0) {
//        dap_enc_sig_shipovnik_key_delete(key);
//        log_it(L_ERROR, "Failed to generate shipovnik keypair");
//        return;
}


size_t dap_enc_sig_shipovnik_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size){

    if (signature_size != SHIPOVNIK_SIGBYTES) {
        log_it(L_ERROR, "Invalid shipovnik signature size");
        return -10;
    }

    if (key->priv_key_data_size != SHIPOVNIK_PUBLICKEYBYTES) {
        log_it(L_ERROR, "Invalid shipovnik secret key");
        return -11;
    }

    if (key->pub_key_data_size != SHIPOVNIK_SECRETKEYBYTES) {
        log_it(L_ERROR, "Invalid shipovnik secret key");
        return -11;
    }


    uint8_t sig[SHIPOVNIK_SIGBYTES];
    size_t sig_len;
    shipovnik_sign(key->priv_key_data, msg, sizeof(msg), sig, &sig_len);

//    if (retcode != 0){
//        log_it(L_ERROR, "Failed to sign message");
//    return retcode;
//    }
}



size_t dap_enc_sig_shipovnik_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature,
                                      const size_t signature_size)
{

    if (key->pub_key_data_size != SHIPOVNIK_SECRETKEYBYTES) {
        log_it(L_ERROR, "Invalid shipovnik secret key");
        return -11;
    }

    if (signature_size != SHIPOVNIK_SIGBYTES) {
        log_it(L_ERROR, "Invalid shipovnik signature size");
        return -10;
    }

    shipovnik_verify(key->pub_key_data, signature, msg, msg_size);
//    if (retcode != 0)
//        log_it(L_ERROR, "Failed to verify signature");
//    return retcode;

}

uint8_t* dap_enc_sig_shipovnik_write_public_key(const uint8_t* a_public_key, size_t* a_buflen_out) {

    uint64_t l_buflen =
            sizeof(uint64_t) +
            SHIPOVNIK_PUBLICKEYBYTES;

    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);

    memcpy(l_ptr, a_public_key, SHIPOVNIK_PUBLICKEYBYTES);
    assert(l_ptr + SHIPOVNIK_PUBLICKEYBYTES - l_buf == (int64_t)l_buflen);

    if (a_buflen_out)
        *a_buflen_out = l_buflen;

    return l_buf;
}


uint8_t* dap_enc_sig_shipovnik_write_private_key(const uint8_t* a_private_key, size_t* a_buflen_out) {


    uint64_t l_buflen =
            sizeof(uint64_t) +
            SHIPOVNIK_SECRETKEYBYTES;

    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);
    memcpy(l_ptr, a_private_key, SHIPOVNIK_SECRETKEYBYTES);
    assert(l_ptr + SHIPOVNIK_SECRETKEYBYTES - l_buf == (int64_t)l_buflen);

    if(a_buflen_out)
        *a_buflen_out = l_buflen;
    return l_buf;
}


uint8_t* dap_enc_sig_shipovnik_read_private_key(const uint8_t *a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_private_key() a_buf is NULL");
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen < l_buflen) {
        log_it(L_ERROR, "::read_private_key() a_buflen %"DAP_UINT64_FORMAT_U" is less than l_buflen %"DAP_UINT64_FORMAT_U, a_buflen, l_buflen);
        return NULL;
    }

    uint8_t* l_private_key = DAP_NEW_Z(uint8_t);
    if (!l_private_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    if (!l_private_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_private_key);
        return NULL;
    }
    memcpy(l_private_key, l_ptr, SHIPOVNIK_SECRETKEYBYTES);
    assert(l_ptr + SHIPOVNIK_SECRETKEYBYTES - a_buf == (int64_t)l_buflen);

    return l_private_key;
}

uint8_t* dap_enc_sig_shipovnik_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_public_key() a_buf is NULL");
        return NULL;
    }

    if (a_buflen < sizeof(uint32_t) * 3) {
        log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is smaller than first four fields(%zu)", a_buflen, sizeof(uint32_t) * 3);
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen < l_buflen) {
        log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is less than l_buflen %"DAP_UINT64_FORMAT_U, a_buflen, l_buflen);
        return NULL;
    }

    uint8_t* l_public_key = DAP_NEW_Z(uint8_t);
    if (!l_public_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
 //   l_public_key->data = DAP_NEW_Z_SIZE(uint8_t, ECDSA_PUBLIC_KEY_SIZE);
    if (!l_public_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_public_key);
        return NULL;
    }
    memcpy(l_public_key, l_ptr, SHIPOVNIK_PUBLICKEYBYTES);
    assert(l_ptr + SHIPOVNIK_PUBLICKEYBYTES - a_buf == (int64_t)l_buflen);

    return l_public_key;
}


uint8_t *dap_enc_sig_shipovnik_write_signature(const uint8_t* a_sign, size_t *a_sign_out)
{
    if (!a_sign) {
        log_it(L_ERROR, "::write_signature() a_sign is NULL");
        return NULL;
    }
    size_t l_buflen = sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + SHIPOVNIK_SIGBYTES;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_ERROR, "::write_signature() l_buf is NULL — memory allocation error");
        return NULL;
    }


    uint64_t l_sig_len = SHIPOVNIK_SIGBYTES;
    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);
    *(uint64_t *)l_ptr = l_sig_len; l_ptr += sizeof(uint64_t);
    memcpy(l_ptr, a_sign, SHIPOVNIK_SIGBYTES);
    assert(l_ptr + l_sig_len - l_buf == (int64_t)l_buflen);

    if (a_sign_out)
        *a_sign_out = l_buflen;

    return l_buf;

}

uint8_t* dap_enc_sig_shipovnik_read_signature(const uint8_t* a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_signature() a_buf is NULL");
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint64_t l_sig_len = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen != l_buflen) {
        log_it(L_ERROR, "::read_signature() a_buflen %zu is not equal to sign size (%"DAP_UINT64_FORMAT_U")",
                        a_buflen, l_buflen);
        return NULL;
    }

    l_sig_len = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (l_buflen != sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len) {
        log_it(L_ERROR, "::read_signature() l_buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
               l_buflen, sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len);
        return NULL;
    }

    uint8_t *l_sign = DAP_NEW(uint8_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    memcpy(l_sign, l_ptr, SHIPOVNIK_SIGBYTES);
    assert(l_ptr + SHIPOVNIK_SIGBYTES - a_buf == (int64_t)l_buflen);

    return l_sign;
}

void *dap_enc_sig_shipovnik_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    memset(((uint8_t *)a_sig),0,SHIPOVNIK_SIGBYTES);

}

void *dap_enc_sig_shipovnik_private_key_delete(uint8_t* privateKey) {
    if (privateKey){
        DAP_DELETE(privateKey);
    }
}

void *dap_enc_sig_shipovnik_public_key_delete(uint8_t* publicKey) {
    if (publicKey) {
        memset(publicKey, 0, SHIPOVNIK_PUBLICKEYBYTES);
    }
}

void *dap_enc_sig_shipovnik_private_and_public_keys_delete(uint8_t* privateKey, uint8_t* publicKey) {
        shipovnik_private_key_delete(privateKey);
        shipovnik_public_key_delete(publicKey);
}



