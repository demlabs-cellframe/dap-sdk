#include "dap_enc_sphincsplus.h"
// XKCP includes moved here from header for encapsulation
#include "SimpleFIPS202.h"
//#include "sphincsplus/randombytes.h"
#include "api.h"
#include "dap_hash.h"
#include "dap_rand.h"
#include "fips202.h"

#define LOG_TAG "dap_enc_sig_sphincsplus"

#ifndef DAP_CRYPTO_TESTS
static const sphincsplus_config_t s_default_config = SPHINCSPLUS_SHA2_128F;
static const sphincsplus_difficulty_t s_default_difficulty = SPHINCSPLUS_SIMPLE;
#else
static _Thread_local sphincsplus_config_t s_default_config = SPHINCSPLUS_SHA2_128F;
static _Thread_local sphincsplus_difficulty_t s_default_difficulty = SPHINCSPLUS_SIMPLE;
#endif


void dap_enc_sig_sphincsplus_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS;
    a_key->enc = NULL;
    a_key->enc_na = dap_enc_sig_sphincsplus_get_sign_msg;
    a_key->dec_na = dap_enc_sig_sphincsplus_open_sign_msg;
    a_key->sign_get = dap_enc_sig_sphincsplus_get_sign;
    a_key->sign_verify = dap_enc_sig_sphincsplus_verify_sign;
}

void dap_enc_sig_sphincsplus_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf, UNUSED_ARG size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    sphincsplus_private_key_t *l_skey = NULL;
    sphincsplus_public_key_t *l_pkey = NULL;
    sphincsplus_base_params_t l_params = {0};

    if (sphincsplus_set_config(s_default_config) || sphincsplus_get_params(s_default_config, &l_params)) {
        log_it(L_CRITICAL, "Error load sphincsplus config");
        return;
    }
    
    // seed norming
    size_t l_seed_buf_size = dap_enc_sig_sphincsplus_crypto_sign_seedbytes();
    unsigned char *l_seed_buf = DAP_NEW_Z_SIZE_RET_IF_FAIL(unsigned char, l_seed_buf_size, NULL);
    if(a_seed && a_seed_size > 0) {
        shake256(l_seed_buf, l_seed_buf_size, (const unsigned char *) a_seed, a_seed_size);
    } else {
        randombytes(l_seed_buf, l_seed_buf_size);
    }
    // creating key pair
    dap_enc_sig_sphincsplus_key_new(a_key);
    a_key->priv_key_data_size = sizeof(sphincsplus_private_key_t);
    a_key->pub_key_data_size = sizeof(sphincsplus_public_key_t);

    l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(sphincsplus_private_key_t, a_key->priv_key_data_size, l_seed_buf);
    l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(sphincsplus_public_key_t, a_key->pub_key_data_size, l_seed_buf, l_skey);
    l_skey->data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes(), l_seed_buf, l_skey, l_pkey);
    l_pkey->data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, dap_enc_sig_sphincsplus_crypto_sign_publickeybytes(), l_seed_buf, l_skey->data, l_skey, l_pkey);

    if(sphincsplus_crypto_sign_seed_keypair(l_pkey->data, l_skey->data, l_seed_buf)) {
        log_it(L_CRITICAL, "Error generating Sphincs key pair");
        DAP_DEL_MULTY(l_skey->data, l_pkey->data, l_skey, l_pkey, l_seed_buf);
        return;
    }
    DAP_DEL_Z(l_seed_buf);
    l_skey->params = l_params;
    l_pkey->params = l_params;
    a_key->priv_key_data = l_skey;
    a_key->pub_key_data = l_pkey;
}


int dap_enc_sig_sphincsplus_get_sign(dap_enc_key_t *a_key, const void *a_msg_in, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max)
{
    dap_return_val_if_pass(!a_key || !a_key->priv_key_data || !a_msg_in || !a_msg_size || !a_sign_out, -1);
    
    if(a_out_size_max < sizeof(sphincsplus_signature_t)) {
        log_it(L_ERROR, "Bad signature size");
        return -2;
    }

    sphincsplus_private_key_t *l_skey = a_key->priv_key_data;
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_out;
    if (sphincsplus_set_params(&l_skey->params)) {
        return -3;
    }

    l_sign->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, dap_enc_sig_sphincsplus_crypto_sign_bytes(), -3);
    l_sign->sig_params = l_skey->params;
    return sphincsplus_crypto_sign_signature(l_sign->sig_data, &l_sign->sig_len, (const unsigned char *)a_msg_in, a_msg_size, l_skey->data);
}

size_t dap_enc_sig_sphincsplus_get_sign_msg(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max)
{

    sphincsplus_private_key_t *l_skey = a_key->priv_key_data;
    if (sphincsplus_set_params(&l_skey->params)) {
        return 0;
    }

    uint32_t l_sign_bytes = dap_enc_sig_sphincsplus_crypto_sign_bytes();
    
    if(a_out_size_max < l_sign_bytes) {
        log_it(L_ERROR, "Bad signature size");
        return 0;
    }

    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_out;
    l_sign->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sign_bytes + a_msg_size, 0);

    l_sign->sig_params = l_skey->params;
    int l_ret = sphincsplus_crypto_sign(l_sign->sig_data, &l_sign->sig_len, (const unsigned char *)a_msg, a_msg_size, l_skey->data);

    return l_ret ? 0 : l_sign->sig_len;
}

int dap_enc_sig_sphincsplus_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sign,
        const size_t a_sign_size)
{
    if(a_sign_size < sizeof(sphincsplus_signature_t)) {
        log_it(L_ERROR, "Bad signature size");
        return -1;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
    sphincsplus_public_key_t *l_pkey = a_key->pub_key_data;

    if(memcmp(&l_sign->sig_params, &l_pkey->params, sizeof(sphincsplus_base_params_t))) {
        log_it(L_ERROR, "Sphincs key params have not equal sign params");
        return -3;
    }

    if(sphincsplus_set_params(&l_sign->sig_params)) {
        return -2;
    }
    int l_ret = sphincsplus_crypto_sign_verify(l_sign->sig_data, l_sign->sig_len, a_msg, a_msg_size, l_pkey->data);
    
    return l_ret;
}


size_t dap_enc_sig_sphincsplus_open_sign_msg(dap_enc_key_t *a_key, const void *a_sign_in, const size_t a_sign_size, void *a_msg_out,
        const size_t a_out_size_max)
{
    sphincsplus_public_key_t *l_pkey = a_key->pub_key_data;
    if(sphincsplus_set_params(&l_pkey->params)) {
        return 0;
    }
    size_t l_sign_bytes = dap_enc_sig_sphincsplus_crypto_sign_bytes();

    if(a_out_size_max < l_sign_bytes) {
        log_it(L_ERROR, "Bad signature size");
        return 0;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_in;

    if(memcmp(&l_sign->sig_params, &l_pkey->params, sizeof(sphincsplus_base_params_t))) {
        log_it(L_ERROR, "Sphincs key params have not equal sign params");
        return 0;
    }

 
    uint64_t l_res_size = 0;
    if (sphincsplus_crypto_sign_open(a_msg_out, &l_res_size, l_sign->sig_data, l_sign->sig_len, l_pkey->data))
        log_it(L_ERROR, "Failed to verify signature");

    return l_res_size;
}

void dap_enc_sig_sphincsplus_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    sphincsplus_private_and_public_keys_delete(a_key->priv_key_data, a_key->pub_key_data);

    a_key->pub_key_data = NULL;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data_size = 0;
    a_key->priv_key_data_size = 0;
}

/* Serialize a private key. */
uint8_t *dap_enc_sig_sphincsplus_write_private_key(const void *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_private_key, NULL);
    sphincsplus_private_key_t *l_private_key = (sphincsplus_private_key_t *)a_private_key;
// func work
    sphincsplus_set_params(&l_private_key->params);
    uint64_t l_secret_length = dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes();
    uint64_t l_buflen = dap_enc_sig_sphincsplus_ser_private_key_size((void *)l_private_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_private_key->params, (uint64_t)sizeof(sphincsplus_base_params_t),
        l_private_key->data, (uint64_t)l_secret_length
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
void *dap_enc_sig_sphincsplus_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
// in work
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(sphincsplus_base_params_t), NULL);
// func work
    uint64_t l_buflen = 0;
    uint64_t l_skey_len = a_buflen -  sizeof(uint64_t) - sizeof(sphincsplus_base_params_t);

    sphincsplus_private_key_t *l_skey = DAP_NEW_Z_RET_VAL_IF_FAIL(sphincsplus_private_key_t, NULL);
    l_skey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_skey_len, NULL, l_skey);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_skey->params, (uint64_t)sizeof(sphincsplus_base_params_t),
        l_skey->data, (uint64_t)l_skey_len
    );
// out work
    sphincsplus_set_params(&l_skey->params);
    uint64_t l_skey_len_exp = dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes();
    if (l_res_des) {
        log_it(L_ERROR,"::read_private_key() deserialise public key, err code %d", l_res_des);
        DAP_DEL_MULTY(l_skey->data, l_skey);
        return NULL;
    }
    if (l_skey_len != l_skey_len_exp) {
        log_it(L_ERROR,"::read_private_key() l_pkey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %"DAP_UINT64_FORMAT_U"", l_skey_len, l_skey_len_exp);
        DAP_DEL_MULTY(l_skey->data, l_skey);
        return NULL;
    }
    return l_skey;
}

/* Serialize a public key. */
uint8_t *dap_enc_sig_sphincsplus_write_public_key(const void* a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_public_key, NULL);
    sphincsplus_public_key_t *l_public_key = (sphincsplus_public_key_t *)a_public_key;
// func work
    sphincsplus_set_params(&l_public_key->params);
    uint64_t l_public_length = dap_enc_sig_sphincsplus_crypto_sign_publickeybytes();
    uint64_t l_buflen = dap_enc_sig_sphincsplus_ser_public_key_size(a_public_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen, 
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_public_key->params, (uint64_t)sizeof(sphincsplus_base_params_t),
        l_public_key->data, (uint64_t)l_public_length
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a public key. */
void *dap_enc_sig_sphincsplus_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
// in work
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(sphincsplus_base_params_t), NULL);
// func work
    uint64_t l_buflen = 0;
    uint64_t l_pkey_len = a_buflen -  sizeof(uint64_t) - sizeof(sphincsplus_base_params_t);

    sphincsplus_public_key_t *l_pkey = DAP_NEW_Z_RET_VAL_IF_FAIL(sphincsplus_public_key_t, NULL);
    l_pkey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_pkey_len, NULL, l_pkey);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_pkey->params, (uint64_t)sizeof(sphincsplus_base_params_t),
        l_pkey->data, (uint64_t)l_pkey_len
    );
// out work
    sphincsplus_set_params(&l_pkey->params);
    uint64_t l_pkey_len_exp = dap_enc_sig_sphincsplus_crypto_sign_publickeybytes();
    if (l_res_des) {
        log_it(L_ERROR,"::read_public_key() deserialise public key, err code %d", l_res_des);
        DAP_DEL_MULTY(l_pkey->data, l_pkey);
        return NULL;
    }
    if (l_pkey_len != l_pkey_len_exp) {
        log_it(L_ERROR,"::read_public_key() l_pkey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %"DAP_UINT64_FORMAT_U"", l_pkey_len, l_pkey_len_exp);
        DAP_DEL_MULTY(l_pkey->data, l_pkey);
        return NULL;
    }
    return l_pkey;
}

/* Serialize a signature */
uint8_t *dap_enc_sig_sphincsplus_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
// func work
    uint64_t l_buflen = dap_enc_sig_sphincsplus_ser_sig_size(a_sign);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen, 
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_sign->sig_params, (uint64_t)sizeof(sphincsplus_base_params_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sign->sig_len
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
void *dap_enc_sig_sphincsplus_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) * 2 + sizeof(sphincsplus_base_params_t), NULL);
// func work
    uint64_t l_buflen;
    uint64_t l_sig_len = a_buflen - sizeof(uint64_t) * 2 - sizeof(sphincsplus_base_params_t);
    sphincsplus_signature_t* l_sign = DAP_NEW_Z_RET_VAL_IF_FAIL(sphincsplus_signature_t, NULL);
    l_sign->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sig_len, NULL, l_sign);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_sign->sig_params, (uint64_t)sizeof(sphincsplus_base_params_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sig_len
    );
// out work
    int l_res_check = sphincsplus_check_params(&l_sign->sig_params);
    if (l_res_des || l_res_check) {
        log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_sign->sig_data, l_sign);
        return NULL;
    }
    return l_sign;
}

void sphincsplus_private_and_public_keys_delete(void *a_skey, void *a_pkey) 
{
    if(a_skey)
        sphincsplus_private_key_delete(a_skey);
    if(a_pkey)
        sphincsplus_public_key_delete(a_pkey);
}

void sphincsplus_private_key_delete(void *a_skey)
{
    dap_return_if_pass(!a_skey);
    DAP_DEL_MULTY(((sphincsplus_private_key_t *)a_skey)->data, a_skey);
}

void sphincsplus_public_key_delete(void *a_pkey)
{
    dap_return_if_pass(!a_pkey);
    DAP_DEL_MULTY(((sphincsplus_public_key_t *)a_pkey)->data, a_pkey);
}

void sphincsplus_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);

    DAP_DEL_Z(((sphincsplus_signature_t *)a_sig)->sig_data);
    ((sphincsplus_signature_t *)a_sig)->sig_len = 0;
}

/*
 * Returns the length of a secret key, in bytes
 */
inline uint64_t dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes()
{
    return sphincsplus_crypto_sign_secretkeybytes();
}

/*
 * Returns the length of a public key, in bytes
 */
inline uint64_t dap_enc_sig_sphincsplus_crypto_sign_publickeybytes()
{
    return sphincsplus_crypto_sign_publickeybytes();
}

/*
 * Returns the length of the seed required to generate a key pair, in bytes
 */
inline uint64_t dap_enc_sig_sphincsplus_crypto_sign_seedbytes()
{
    return sphincsplus_crypto_sign_seedbytes();
}

/*
 * Returns the length of a signature, in bytes
 */
inline uint64_t dap_enc_sig_sphincsplus_crypto_sign_bytes()
{
    return sphincsplus_crypto_sign_bytes();
}

inline uint64_t dap_enc_sig_sphincsplus_calc_signature_unserialized_size()
{
    return sizeof(sphincsplus_signature_t); 
}


#ifdef DAP_CRYPTO_TESTS
inline void dap_enc_sig_sphincsplus_set_default_config(sphincsplus_config_t  a_new_config) 
{
    s_default_config = a_new_config;
}
inline int dap_enc_sig_sphincsplus_get_configs_count() 
{
    return SPHINCSPLUS_CONFIG_MAX_ARG - 1;
}
#endif
