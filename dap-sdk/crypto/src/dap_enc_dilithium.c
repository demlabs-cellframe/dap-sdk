#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "dap_enc_dilithium.h"
#include "dap_enc.h"
#include "dap_common.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_enc_sig_dilithium"

static enum DAP_DILITHIUM_SIGN_SECURITY _dilithium_type = DILITHIUM_MIN_SIZE; // by default

//// WARNING! Its because of accident with wrong sizes on mobile 32bit platforms
//// Remove it after you'll update all mobile keys

void dap_enc_sig_dilithium_set_type(enum DAP_DILITHIUM_SIGN_SECURITY type)
{
    _dilithium_type = type;
}

void dap_enc_sig_dilithium_key_new(dap_enc_key_t *a_key) {

    a_key->type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_dilithium_get_sign;
    a_key->sign_verify = dap_enc_sig_dilithium_verify_sign;
}

// generation key pair for sign Alice
// OUTPUT:
// a_key->data  --- Alice's public key
// alice_priv  ---  Alice's private key
// alice_msg_len --- Alice's private key length
void dap_enc_sig_dilithium_key_new_generate(dap_enc_key_t * key, const void *kex_buf,
        size_t kex_size, const void * seed, size_t seed_size,
        size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) key_size;

    int32_t retcode;

    dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED);

    //int32_t type = 2;
    key->priv_key_data_size = sizeof(dilithium_private_key_t);
    key->pub_key_size = sizeof(dilithium_public_key_t);
    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_size);

    retcode = dilithium_crypto_sign_keypair(
            (dilithium_public_key_t *) key->pub_key_data,
            (dilithium_private_key_t *) key->priv_key_data,
            (dilithium_kind_t)_dilithium_type,
            seed, seed_size
            );
    if(retcode) {
        log_it(L_CRITICAL, "Error generating Dilithium key pair");
        dap_enc_sig_dilithium_key_delete(key);
        return;
    }
}

int dap_enc_sig_dilithium_get_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
    if(a_sig_size < sizeof(dilithium_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }

    return dilithium_crypto_sign((dilithium_signature_t *)a_sig, (const unsigned char *) a_msg, a_msg_size, a_key->priv_key_data);
}

int dap_enc_sig_dilithium_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
    if(a_sig_size < sizeof(dilithium_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }
    int l_ret = dilithium_crypto_sign_open( (unsigned char *)a_msg, a_msg_size, (dilithium_signature_t *)a_sig, a_key->pub_key_data);
    if(l_ret)
        debug_if(dap_enc_debug_more(), L_WARNING, "Wrong signature, can't open with code %d", l_ret);

    return l_ret;
}

void dap_enc_sig_dilithium_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    dilithium_private_and_public_keys_delete(a_key->priv_key_data, a_key->pub_key_data);

    a_key->pub_key_data = NULL;
    a_key->priv_key_data = NULL;
    a_key->pub_key_size = 0;
    a_key->priv_key_data_size = 0;
}

/* Serialize a signature */
uint8_t *dap_enc_sig_dilithium_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    dilithium_signature_t *l_sign = (dilithium_signature_t *)a_sign;
// func work
    uint64_t l_buflen = dap_enc_sig_dilithium_ser_sig_size(l_sign);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_sign->kind, (uint64_t)sizeof(uint32_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sign->sig_len
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
void *dap_enc_sig_dilithium_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf){
        log_it(L_ERROR,"::read_signature() NULL buffer on input");
        return NULL;
    }
    if(a_buflen < sizeof(uint64_t) * 2 + sizeof(uint32_t)){
        log_it(L_ERROR,"::read_signature() Buflen %zd is smaller than first three fields(%zd)", a_buflen,
               sizeof(uint64_t) * 2 + sizeof(uint32_t));
        return NULL;
    }

    uint64_t l_buflen;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    uint64_t l_shift_mem = sizeof(uint64_t);
    if (l_buflen != a_buflen) {
        if (l_buflen << 32 >> 32 != a_buflen) {
            log_it(L_ERROR,"::read_public_key() Buflen field inside buffer is %"DAP_UINT64_FORMAT_U" when expected to be %"DAP_UINT64_FORMAT_U,
                   l_buflen, (uint64_t)a_buflen);
            return NULL;
        }
        l_shift_mem = sizeof(uint32_t);
    }
    uint32_t kind;
    memcpy(&kind, a_buf + l_shift_mem, sizeof(uint32_t));
    l_shift_mem += sizeof(uint32_t);
    dilithium_param_t p;
    if(!dilithium_params_init(&p, kind))
        return NULL ;

    dilithium_signature_t* l_sign = DAP_NEW_Z(dilithium_signature_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_sign->kind = kind;
    memcpy(&l_sign->sig_len, a_buf + l_shift_mem, sizeof(uint64_t));
    l_shift_mem += sizeof(uint64_t);

    if( l_sign->sig_len> (UINT64_MAX - l_shift_mem ) ){
            log_it(L_ERROR,"::read_signature() Buflen inside signature %"DAP_UINT64_FORMAT_U" is too big ", l_sign->sig_len);
            DAP_DELETE(l_sign);
            return NULL;
    }

    if( (uint64_t) a_buflen < (l_shift_mem + l_sign->sig_len) ){
        log_it(L_ERROR,"::read_signature() Buflen %zd is smaller than all fields together(%"DAP_UINT64_FORMAT_U")", a_buflen,
               l_shift_mem + l_sign->sig_len  );
        DAP_DELETE(l_sign);
        return NULL;
    }

    l_sign->sig_data = DAP_NEW_SIZE(uint8_t, l_sign->sig_len);
    if (!l_sign->sig_data){
        log_it(L_ERROR,"::read_signature() Can't allocate sig_data %"DAP_UINT64_FORMAT_U" size", l_sign->sig_len);
        DAP_DELETE(l_sign);
        return NULL;
    }
    memcpy(l_sign->sig_data, a_buf + l_shift_mem, l_sign->sig_len);
    return l_sign;
}

/* Serialize a private key. */
uint8_t *dap_enc_sig_dilithium_write_private_key(const void *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dilithium_private_key_t *l_private_key = (dilithium_private_key_t *)a_private_key;
    dilithium_param_t p;
    dap_return_val_if_pass(!l_private_key || !dilithium_params_init(&p, l_private_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_dilithium_ser_private_key_size(a_private_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
                        &l_buflen, (uint64_t)sizeof(uint64_t),
                        &l_private_key->kind, (uint64_t)sizeof(uint32_t),
                        l_private_key->data, (uint64_t)p.CRYPTO_SECRETKEYBYTES);
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Serialize a public key. */
uint8_t *dap_enc_sig_dilithium_write_public_key(const void *a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dilithium_public_key_t *l_public_key = (dilithium_public_key_t *)a_public_key;
    dilithium_param_t p;
    dap_return_val_if_pass(!l_public_key || !dilithium_params_init(&p, l_public_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_dilithium_ser_public_key_size(a_public_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
                        &l_buflen, (uint64_t)sizeof(uint64_t),
                        &l_public_key->kind, (uint64_t)sizeof(uint32_t),
                        l_public_key->data, (uint64_t)p.CRYPTO_PUBLICKEYBYTES);
    
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
void *dap_enc_sig_dilithium_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf, NULL);

    if(a_buflen < (sizeof(uint64_t) + sizeof(uint32_t))){
        log_it(L_ERROR,"::read_private_key() Buflen %zd is smaller than first two fields(%zd)", a_buflen,sizeof(uint64_t) + sizeof(dilithium_kind_t)  );
        return NULL;
    }

    dilithium_kind_t kind;
    uint64_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    if(l_buflen != (uint64_t) a_buflen)
        return NULL;
    memcpy(&kind, a_buf + sizeof(uint64_t), sizeof(uint32_t));
    dilithium_param_t p;
    if(!dilithium_params_init(&p, kind))
        return NULL;

    if(a_buflen < (sizeof(uint64_t) + sizeof(uint32_t) + p.CRYPTO_SECRETKEYBYTES ) ){
        log_it(L_ERROR,"::read_private_key() Buflen %zd is smaller than all fields together(%zd)", a_buflen,
               sizeof(uint64_t) + sizeof(uint32_t) + p.CRYPTO_SECRETKEYBYTES  );
        return NULL;
    }

    dilithium_private_key_t* l_private_key = DAP_NEW(dilithium_private_key_t);
    if (!l_private_key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_private_key->kind = kind;

    l_private_key->data = DAP_NEW_SIZE(uint8_t, p.CRYPTO_SECRETKEYBYTES);
    memcpy(l_private_key->data, a_buf + sizeof(uint64_t) + sizeof(uint32_t), p.CRYPTO_SECRETKEYBYTES);
    return l_private_key;
}

/* Deserialize a public key. */
void *dap_enc_sig_dilithium_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf){
        log_it(L_ERROR,"::read_public_key() NULL buffer on input");
        return NULL;
    }
    if( a_buflen < (sizeof(uint64_t) + sizeof(uint32_t))){
        log_it(L_ERROR,"::read_public_key() Buflen %zd is smaller than first two fields(%zd)", a_buflen,sizeof(uint64_t) + sizeof(uint32_t)  );
        return NULL;
    }

    uint32_t kind = 0;
    uint64_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    if (l_buflen != a_buflen) {
        if (l_buflen << 32 >> 32 != a_buflen) {
            log_it(L_ERROR,"::read_public_key() Buflen field inside buffer is %"DAP_UINT64_FORMAT_U" when expected to be %"DAP_UINT64_FORMAT_U,
                   l_buflen, (uint64_t)a_buflen);
            return NULL;
        }else {
            memcpy(&kind, a_buf + sizeof(uint32_t), sizeof(uint32_t));
        }
    } else {
        memcpy(&kind, a_buf + sizeof(uint64_t), sizeof(uint32_t));
    }
    dilithium_param_t p;
    if(!dilithium_params_init(&p, kind)){
        log_it(L_ERROR,"::read_public_key() Can't find params for signature kind %d", kind);
        return NULL;
    }

    if(a_buflen < (sizeof(uint64_t) + sizeof(uint32_t) + p.CRYPTO_PUBLICKEYBYTES ) ){
        log_it(L_ERROR,"::read_public_key() Buflen %zd is smaller than all fields together(%zd)", a_buflen,
               sizeof(uint64_t) + sizeof(uint32_t) + p.CRYPTO_PUBLICKEYBYTES  );
        return NULL;
    }

    dilithium_public_key_t* l_public_key = DAP_NEW_Z(dilithium_public_key_t);
    if (!l_public_key){
        log_it(L_CRITICAL,"::read_public_key() Can't allocate memory for public key");
        return NULL;
    }
    l_public_key->kind = kind;

    l_public_key->data = DAP_NEW_Z_SIZE(byte_t, p.CRYPTO_PUBLICKEYBYTES);
    if (!l_public_key->data){
        log_it(L_CRITICAL,"::read_public_key() Can't allocate memory for public key's data");
        DAP_DELETE(l_public_key);
        return NULL;
    }

    memcpy(l_public_key->data, a_buf + sizeof(uint64_t) + sizeof(uint32_t), p.CRYPTO_PUBLICKEYBYTES);
    return l_public_key;
}
