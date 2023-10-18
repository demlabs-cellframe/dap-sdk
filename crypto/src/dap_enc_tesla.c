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

void dap_enc_sig_tesla_key_new(dap_enc_key_t *key) {

    key->type = DAP_ENC_KEY_TYPE_SIG_TESLA;
    key->enc = NULL;
    key->sign_get = dap_enc_sig_tesla_get_sign;
    key->sign_verify = dap_enc_sig_tesla_verify_sign;
    key->ser_sign = dap_enc_tesla_write_signature;
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
void dap_enc_sig_tesla_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
        size_t kex_size, const void * seed, size_t seed_size,
        size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) key_size;

    int32_t retcode;

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
        tesla_private_and_public_keys_delete((tesla_private_key_t *) key->pub_key_data,
                (tesla_public_key_t *) key->pub_key_data);
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
    tesla_private_and_public_keys_delete((tesla_private_key_t *) key->priv_key_data,
            (tesla_public_key_t *) key->pub_key_data);
}

size_t dap_enc_tesla_calc_signature_size(void)
{
    return sizeof(tesla_signature_t);
}

size_t dap_enc_tesla_calc_signature_serialized_size(tesla_signature_t* a_sign)
{
    return sizeof(size_t) + sizeof(tesla_kind_t) + a_sign->sig_len + sizeof(unsigned long long);
}

/* Serialize a signature */
uint8_t *dap_enc_tesla_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    tesla_signature_t *l_sign = (tesla_signature_t *)a_sign;
// func work
    size_t l_buflen = dap_enc_tesla_calc_signature_serialized_size(l_sign);
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 8,
        &l_buflen, sizeof(size_t),
        &l_sign->kind, sizeof(tesla_kind_t),
        &l_sign->sig_len, sizeof(unsigned long long),
        l_sign->sig_data, l_sign->sig_len
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
tesla_signature_t* dap_enc_tesla_read_signature(uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(size_t) + sizeof(tesla_kind_t)))
        return NULL ;
    tesla_kind_t kind;
    size_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(size_t));
    memcpy(&kind, a_buf + sizeof(size_t), sizeof(tesla_kind_t));
    if(l_buflen != a_buflen)
        return NULL ;
    tesla_param_t p;
    if(!tesla_params_init(&p, kind))
        return NULL ;

    tesla_signature_t* l_sign = DAP_NEW(tesla_signature_t);
    l_sign->kind = kind;
    size_t l_shift_mem = sizeof(size_t) + sizeof(tesla_kind_t);
    memcpy(&l_sign->sig_len, a_buf + l_shift_mem, sizeof(unsigned long long));
    l_shift_mem += sizeof(unsigned long long);
    l_sign->sig_data = DAP_NEW_SIZE(unsigned char, l_sign->sig_len);
    memcpy(l_sign->sig_data, a_buf + l_shift_mem, l_sign->sig_len);
    l_shift_mem += l_sign->sig_len;
    return l_sign;
}

/* Serialize a private key. */
uint8_t *dap_enc_tesla_write_private_key(const tesla_private_key_t *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    tesla_param_t p;
    dap_return_val_if_pass(!tesla_params_init(&p, a_private_key->kind), NULL);
// func work
    size_t l_buflen = sizeof(size_t) + sizeof(tesla_kind_t) + p.CRYPTO_SECRETKEYBYTES; //CRYPTO_PUBLICKEYBYTES;
    uint8_t *l_buf =  dap_serialize_multy(NULL, l_buflen, 6,
        &l_buflen, sizeof(size_t),
        &a_private_key->kind, sizeof(tesla_kind_t),
        a_private_key->data, p.CRYPTO_SECRETKEYBYTES
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Serialize a public key. */
uint8_t* dap_enc_tesla_write_public_key(const tesla_public_key_t* a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    tesla_param_t p;
    dap_return_val_if_pass(!tesla_params_init(&p, a_public_key->kind), NULL);
// func work
    size_t l_buflen = sizeof(size_t) + sizeof(tesla_kind_t) + p.CRYPTO_PUBLICKEYBYTES;
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 6,
        &l_buflen, sizeof(size_t),
        &a_public_key->kind, sizeof(tesla_kind_t),
        a_public_key->data, p.CRYPTO_PUBLICKEYBYTES
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
tesla_private_key_t* dap_enc_tesla_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(size_t) + sizeof(tesla_kind_t)))
        return NULL;
    tesla_kind_t kind;
    size_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(size_t));
    memcpy(&kind, a_buf + sizeof(size_t), sizeof(tesla_kind_t));
    if(l_buflen != a_buflen)
        return NULL;
    tesla_param_t p;
    if(!tesla_params_init(&p, kind))
        return NULL;
    tesla_private_key_t* l_private_key = DAP_NEW(tesla_private_key_t);
    l_private_key->kind = kind;

    l_private_key->data = DAP_NEW_SIZE(unsigned char, p.CRYPTO_SECRETKEYBYTES);
    memcpy(l_private_key->data, a_buf + sizeof(size_t) + sizeof(tesla_kind_t), p.CRYPTO_SECRETKEYBYTES);
    return l_private_key;
}

/* Deserialize a public key. */
tesla_public_key_t* dap_enc_tesla_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(size_t) + sizeof(tesla_kind_t)))
        return NULL;
    tesla_kind_t kind;
    size_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(size_t));
    memcpy(&kind, a_buf + sizeof(size_t), sizeof(tesla_kind_t));
    if(l_buflen != a_buflen)
        return NULL;
    tesla_param_t p;
    if(!tesla_params_init(&p, kind))
        return NULL;
    tesla_public_key_t* l_public_key = DAP_NEW(tesla_public_key_t);
    l_public_key->kind = kind;

    l_public_key->data = DAP_NEW_SIZE(unsigned char, p.CRYPTO_PUBLICKEYBYTES);
    memcpy(l_public_key->data, a_buf + sizeof(size_t) + sizeof(tesla_kind_t), p.CRYPTO_PUBLICKEYBYTES);
    return l_public_key;
}
