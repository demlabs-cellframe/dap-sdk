#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "dap_enc_tesla.h"
#include "dap_common.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_enc_sig_tesla"

static enum DAP_TESLA_SIGN_SECURITY _tesla_type = HEURISTIC_MAX_SECURITY_AND_MAX_SPEED; // by default

void dap_enc_sig_tesla_set_type(enum DAP_TESLA_SIGN_SECURITY type)
{
    _tesla_type = type;
}


void dap_enc_sig_tesla_key_new(dap_enc_key_t *a_key) {

    a_key->type = DAP_ENC_KEY_TYPE_SIG_TESLA;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_tesla_get_sign;
    a_key->sign_verify = dap_enc_sig_tesla_verify_sign;
}

// generation key pair for sign Alice
// OUTPUT:
// a_key->data  --- Alice's public key
// alice_priv  ---  Alice's private key
// alice_msg_len --- Alice's private key length
/**
 * @brief dap_enc_sig_tesla_key_new_generate
 *
 * @param key
 * @param kex_buf
 * @param kex_size
 * @param seed
 * @param seed_size
 * @param key_size
 */
void dap_enc_sig_tesla_key_new_generate(dap_enc_key_t *key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, const void * seed, size_t seed_size,
        UNUSED_ARG size_t key_size)
{
    int32_t retcode = 0;

    dap_enc_sig_tesla_set_type(HEURISTIC_MAX_SECURITY_AND_MAX_SPEED);

    /* type is a param of sign-security
     * type = 0 - Heuristic qTESLA, NIST's security category 1
     * type = 1 - Heuristic qTESLA, NIST's security category 3 (option for size)
     * type = 2 - Heuristic qTESLA, NIST's security category 3 (option for speed)
     * type = 3 - Provably-secure qTESLA, NIST's security category 1
     * type = 4 - Provably-secure qTESLA, NIST's security category 3 (max security)
     */
    //int32_t type = 2;
    key->priv_key_data_size = sizeof(tesla_private_key_t);
    key->pub_key_data_size = sizeof(tesla_public_key_t);
    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_data_size);

    retcode = tesla_crypto_sign_keypair((tesla_public_key_t *) key->pub_key_data,
            (tesla_private_key_t *) key->priv_key_data, (tesla_kind_t)_tesla_type, seed, seed_size);
    if(retcode != 0) {
        dap_enc_sig_tesla_key_delete(key);
        log_it(L_CRITICAL, "Error");
        return;
    }
}

int dap_enc_sig_tesla_get_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
    if(a_sig_size < sizeof(tesla_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }

    return tesla_crypto_sign((tesla_signature_t *)a_sig, (const unsigned char *)a_msg, a_msg_size, a_key->priv_key_data);
}

int dap_enc_sig_tesla_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
    if(a_sig_size < sizeof(tesla_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -6;
    }

    return tesla_crypto_sign_open((tesla_signature_t *)a_sig, (unsigned char *)a_msg, a_msg_size, a_key->pub_key_data);
}

void dap_enc_sig_tesla_key_delete(dap_enc_key_t *key)
{
    dap_return_if_pass(!key);
    tesla_private_and_public_keys_delete(key->priv_key_data, key->pub_key_data);
    key->priv_key_data = NULL;
    key->pub_key_data = NULL;
    key->priv_key_data_size = 0;
    key->pub_key_data_size = 0;
}

/* Serialize a signature */
uint8_t *dap_enc_sig_tesla_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    tesla_signature_t *l_sign = (tesla_signature_t *)a_sign;
// func work
    uint64_t l_buflen = dap_enc_sig_tesla_ser_sig_size(l_sign);
    uint32_t l_kind = l_sign->kind;
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sign->sig_len
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
void *dap_enc_sig_tesla_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) * 2 + sizeof(uint32_t), NULL);
// func work
    uint64_t l_buflen;
    uint64_t l_sig_len = a_buflen - sizeof(uint64_t) * 2 - sizeof(uint32_t);
    tesla_signature_t* l_sign = DAP_NEW_Z_RET_VAL_IF_FAIL(tesla_signature_t, NULL);
    l_sign->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sig_len, NULL, l_sign);
    uint32_t l_kind = 0;
    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sig_len
    );
    l_sign->kind = l_kind;
// out work
    tesla_param_t l_p;
    int l_res_check = tesla_params_init(&l_p, l_sign->kind);
    if (l_res_des || !l_res_check) {
        log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_sign->sig_data, l_sign);
        return NULL;
    }
    return l_sign;
}

/* Serialize a private key. */
uint8_t *dap_enc_sig_tesla_write_private_key(const void *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    tesla_private_key_t *l_private_key = (tesla_private_key_t *)a_private_key;
    tesla_param_t p;
    dap_return_val_if_pass(!l_private_key || !tesla_params_init(&p, l_private_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_tesla_ser_private_key_size(a_private_key); //CRYPTO_PUBLICKEYBYTES;
    uint32_t l_kind = l_private_key->kind;
    uint8_t *l_buf =  DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_private_key->data, (uint64_t)p.CRYPTO_SECRETKEYBYTES
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
void *dap_enc_sig_tesla_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(uint32_t), NULL);
// func work
    uint64_t l_buflen;
    uint64_t l_skey_len = a_buflen - sizeof(uint64_t) - sizeof(uint32_t);
    tesla_private_key_t* l_skey = DAP_NEW_Z_RET_VAL_IF_FAIL(tesla_private_key_t, NULL);
    l_skey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_skey_len, NULL, l_skey);
    uint32_t l_kind = 0;
    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_skey->data, (uint64_t)l_skey_len
    );
    l_skey->kind = l_kind;
// out work
    tesla_param_t l_p;
    int l_res_check = tesla_params_init(&l_p, l_skey->kind);
    if (l_res_des || !l_res_check) {
        log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_skey->data, l_skey);
        return NULL;
    }
    return l_skey;
}


/* Serialize a public key. */
uint8_t *dap_enc_sig_tesla_write_public_key(const void *a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    tesla_public_key_t *l_public_key = (tesla_public_key_t *)a_public_key;
    tesla_param_t p;
    dap_return_val_if_pass(!l_public_key || !tesla_params_init(&p, l_public_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_tesla_ser_public_key_size(a_public_key);
    uint32_t l_kind = l_public_key->kind;
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_public_key->data, (uint64_t)p.CRYPTO_PUBLICKEYBYTES
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Deserialize a public key. */
void *dap_enc_sig_tesla_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(uint32_t), NULL);
// func work
    uint64_t l_buflen;
    uint64_t l_pkey_len = a_buflen - sizeof(uint64_t) - sizeof(uint32_t);
    tesla_public_key_t* l_pkey = DAP_NEW_Z_RET_VAL_IF_FAIL(tesla_public_key_t, NULL);
    l_pkey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_pkey_len, NULL, l_pkey);
    uint32_t l_kind = 0;
    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_pkey->data, (uint64_t)l_pkey_len
    );
    l_pkey->kind = l_kind;
// out work
    tesla_param_t l_p;
    int l_res_check = tesla_params_init(&l_p, l_pkey->kind);
    if (l_res_des || !l_res_check) {
        log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_pkey->data, l_pkey);
        return NULL;
    }
    return l_pkey;
}
