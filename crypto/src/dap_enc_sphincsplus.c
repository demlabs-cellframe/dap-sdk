#include "dap_enc_sphincsplus.h"
#include "api.h"

#define LOG_TAG "dap_enc_sig_sphincsplus"

// static falcon_sign_degree_t s_falcon_sign_degree = FALCON_512;
// static falcon_kind_t s_falcon_kind = FALCON_COMPRESSED;
// static falcon_sign_type_t s_falcon_type = FALCON_DYNAMIC;


// void dap_enc_sig_falcon_set_degree(falcon_sign_degree_t a_falcon_sign_degree)
// {
//     if (a_falcon_sign_degree != FALCON_512 && a_falcon_sign_degree != FALCON_1024) {
//         log_it(L_ERROR, "Wrong falcon degree");
//         return;
//     }
//     s_falcon_sign_degree = a_falcon_sign_degree;
// }

// void dap_enc_sig_falcon_set_kind(falcon_kind_t a_falcon_kind)
// {
//     if (a_falcon_kind != FALCON_COMPRESSED && a_falcon_kind != FALCON_PADDED && a_falcon_kind != FALCON_CT) {
//         log_it(L_ERROR, "Wrong falcon kind");
//         return;
//     }
//     s_falcon_kind = a_falcon_kind;
// }

// void dap_enc_sig_falcon_set_type(falcon_sign_type_t a_falcon_type)
// {
//     if (a_falcon_type != FALCON_DYNAMIC && a_falcon_type != FALCON_TREE) {
//         log_it(L_ERROR, "Wrong falcon type");
//         return;
//     }
//     s_falcon_type = a_falcon_type;
// }


void dap_enc_sig_sphincsplus_key_new(dap_enc_key_t *a_key) {
    a_key->type = DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS;
    a_key->enc = NULL;
    a_key->enc_na = dap_enc_sig_sphincsplus_get_sign_msg;
    a_key->dec_na = dap_enc_sig_sphincsplus_open_sign_msg;
    a_key->sign_get = dap_enc_sig_sphincsplus_get_sign;
    a_key->sign_verify = dap_enc_sig_sphincsplus_verify_sign;
}

void dap_enc_sig_sphincsplus_key_new_generate(dap_enc_key_t *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, size_t a_key_size) {

    (void) a_kex_buf;
    (void) a_kex_size;
    (void) a_key_size;

    sphincsplus_private_key_t *l_skey = NULL;
    sphincsplus_public_key_t *l_pkey = NULL;
    
    // seed norming
    uint64_t l_key_size = sphincsplus_crypto_sign_seedbytes();
    unsigned char l_seedbuf[l_key_size];

    if(a_seed && a_seed_size > 0) {
        SHA3_256((unsigned char *) l_seedbuf, (const unsigned char *) a_seed, a_seed_size);
    } else {
        randombytes(l_seedbuf, l_key_size);
    }
    // creating key pair
    dap_enc_sig_sphincsplus_key_new(a_key);
    a_key->priv_key_data_size = sizeof(sphincsplus_private_key_t);
    a_key->pub_key_data_size = sizeof(sphincsplus_public_key_t);

    DAP_NEW_Z_RET(l_skey, sphincsplus_private_key_t, NULL);
    DAP_NEW_Z_RET(l_pkey, sphincsplus_public_key_t, l_skey);
    DAP_NEW_Z_SIZE_RET(l_skey->data, uint8_t, sphincsplus_crypto_sign_secretkeybytes(), l_skey, l_pkey);
    DAP_NEW_Z_SIZE_RET(l_pkey->data, uint8_t, sphincsplus_crypto_sign_publickeybytes(), l_skey->data, l_skey, l_pkey);

    if(sphincsplus_crypto_sign_seed_keypair(l_pkey->data, l_skey->data, l_seedbuf)) {
        DAP_DEL_MULTY(l_skey->data, l_pkey->data, l_skey, l_pkey);
        log_it(L_CRITICAL, "Error generating Sphincs key pair");
        return;
    }
    a_key->priv_key_data = l_skey;
    a_key->pub_key_data = l_pkey;
}


int dap_enc_sig_sphincsplus_get_sign(dap_enc_key_t *a_key, const void *a_msg_in, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max){

    if(a_out_size_max < sizeof(sphincsplus_signature_t)) {
        log_it(L_ERROR, "Bad signature size");
        return -1;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_out;
    DAP_NEW_Z_SIZE_RET_VAL(l_sign->sig_data, uint8_t, sphincsplus_crypto_sign_bytes(), -2, NULL);

    sphincsplus_private_key_t *l_skey = a_key->priv_key_data;
    return sphincsplus_crypto_sign_signature(l_sign->sig_data, &l_sign->sig_len, (const unsigned char *)a_msg_in, a_msg_size, l_skey->data);
}

size_t dap_enc_sig_sphincsplus_get_sign_msg(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max) {

    if(a_out_size_max < sphincsplus_crypto_sign_bytes()) {
        log_it(L_ERROR, "Bad signature size");
        return -1;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_out;
    DAP_NEW_Z_SIZE_RET_VAL(l_sign->sig_data, uint8_t, sphincsplus_crypto_sign_bytes() + a_msg_size, 0, NULL);

    sphincsplus_private_key_t *l_skey = a_key->priv_key_data;

    int l_ret = sphincsplus_crypto_sign(l_sign->sig_data, &l_sign->sig_len, (const unsigned char *)a_msg, a_msg_size, l_skey->data);
    return l_ret < 0 ? 0 : l_sign->sig_len;
}

int dap_enc_sig_sphincsplus_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sign,
        const size_t a_sign_size)
{
    if(a_sign_size < sizeof(sphincsplus_signature_t)) {
        log_it(L_ERROR, "Bad signature size");
        return -1;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
    sphincsplus_private_key_t *l_pkey = a_key->pub_key_data;

    return sphincsplus_crypto_sign_verify(l_sign->sig_data, l_sign->sig_len, a_msg, a_msg_size, l_pkey->data);
}


size_t dap_enc_sig_sphincsplus_open_sign_msg(dap_enc_key_t *a_key, const void *a_sign_in, const size_t a_sign_size, void *a_msg_out,
        const size_t a_out_size_max)
{
    if(a_out_size_max < sphincsplus_crypto_sign_bytes()) {
        log_it(L_ERROR, "Bad signature size");
        return 0;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign_in;
    sphincsplus_private_key_t *l_pkey = a_key->pub_key_data;

    uint64_t l_res_size = 0;
    if (sphincsplus_crypto_sign_open(a_msg_out, &l_res_size, l_sign->sig_data, l_sign->sig_len, l_pkey->data))
        log_it(L_ERROR, "Failed to verify signature");
    return l_res_size;
}

void dap_enc_sig_sphincsplus_key_delete(dap_enc_key_t *key) {

    sphincsplus_private_and_public_keys_delete((sphincsplus_private_key_t *) key->priv_key_data,
        (sphincsplus_public_key_t *) key->pub_key_data);

    DAP_DEL_Z(key->pub_key_data)
    DAP_DEL_Z(key->priv_key_data);
}

/* Serialize a private key. */
uint8_t *dap_enc_sphincsplus_write_private_key(const void *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_private_key, NULL);
    sphincsplus_private_key_t *l_private_key = (sphincsplus_private_key_t *)a_private_key;
// func work
    size_t l_secret_length = dap_enc_sphincsplus_crypto_sign_secretkeybytes();
    uint64_t l_buflen = sizeof(uint64_t) + l_secret_length;
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 4,
        &l_buflen, sizeof(uint64_t), 
        l_private_key->data, l_secret_length
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
sphincsplus_private_key_t *dap_enc_sphincsplus_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf ){
        return NULL;
    }

    if(a_buflen < sizeof(uint64_t)){
        log_it(L_ERROR,"::read_private_key() Buflen %zd is smaller than first two fields(%zd)", a_buflen,sizeof(uint64_t) );
        return NULL;
    }
    
    uint64_t l_buflen = 0;
    size_t l_secret_length = dap_enc_sphincsplus_crypto_sign_secretkeybytes();

    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    if(l_buflen != (uint64_t) a_buflen)
        return NULL;

    if(a_buflen < (sizeof(uint64_t) + l_secret_length) ){
        log_it(L_ERROR,"::read_private_key() Buflen %zd is smaller than all fields together(%zd)", a_buflen,
               sizeof(uint64_t) + l_secret_length );
        return NULL;
    }

    sphincsplus_private_key_t *l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, sphincsplus_private_key_t, NULL, NULL);
    DAP_NEW_Z_SIZE_RET_VAL(l_ret->data, uint8_t, l_secret_length, NULL, l_ret);

    memcpy(l_ret->data, a_buf + sizeof(uint64_t), l_secret_length);
    return l_ret;
}

/* Serialize a public key. */
uint8_t *dap_enc_sphincsplus_write_public_key(const void* a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_public_key, NULL);
    sphincsplus_public_key_t *l_public_key = (sphincsplus_public_key_t *)a_public_key;
// func work
    size_t l_public_length = dap_enc_sphincsplus_crypto_sign_publickeybytes();
    uint64_t l_buflen = sizeof(uint64_t) + l_public_length;
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 4, 
        &l_buflen, sizeof(uint64_t), 
        l_public_key->data, l_public_length
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
sphincsplus_public_key_t *dap_enc_sphincsplus_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf ){
        return NULL;
    }

    if(a_buflen < sizeof(uint64_t)){
        log_it(L_ERROR,"::read_public_key() Buflen %zd is smaller than first two fields(%zd)", a_buflen,sizeof(uint64_t) );
        return NULL;
    }
    
    uint64_t l_buflen = 0;
    size_t l_public_length = dap_enc_sphincsplus_crypto_sign_publickeybytes();

    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    if(l_buflen != (uint64_t) a_buflen)
        return NULL;

    if(a_buflen < (sizeof(uint64_t) + l_public_length) ){
        log_it(L_ERROR,"::read_public_key() Buflen %zd is smaller than all fields together(%zd)", a_buflen,
               sizeof(uint64_t) + l_public_length );
        return NULL;
    }

    sphincsplus_public_key_t* l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, sphincsplus_public_key_t, NULL, NULL);
    DAP_NEW_Z_SIZE_RET_VAL(l_ret->data, uint8_t, l_public_length, NULL, l_ret);

    memcpy(l_ret->data, a_buf + sizeof(uint64_t), l_public_length);
    return l_ret;
}

/* Serialize a signature */
uint8_t *dap_enc_sphincsplus_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// out work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
// func work
    uint64_t l_buflen = l_sign->sig_len + sizeof(uint64_t) * 2;
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 6, 
        &l_buflen, sizeof(uint64_t),
        &l_sign->sig_len, sizeof(uint64_t),
        l_sign->sig_data, l_sign->sig_len
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
sphincsplus_signature_t *dap_enc_sphincsplus_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf){
        log_it(L_ERROR,"::read_signature() NULL buffer on input");
        return NULL;
    }
    if(a_buflen < sizeof(uint64_t) * 2){
        log_it(L_ERROR,"::read_signature() Buflen %zd is smaller than first fields(%zd)", a_buflen,
               sizeof(uint64_t) * 2);
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

    sphincsplus_signature_t* l_sign = NULL;
    DAP_NEW_Z_RET_VAL(l_sign, sphincsplus_signature_t, NULL, NULL);

    memcpy(&l_sign->sig_len, a_buf + l_shift_mem, sizeof(uint64_t));
    l_shift_mem += sizeof(uint64_t);

    if( l_sign->sig_len > (UINT64_MAX - l_shift_mem ) ){
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

    DAP_NEW_Z_SIZE_RET_VAL(l_sign->sig_data, uint8_t, l_sign->sig_len, NULL, l_sign);
    memcpy(l_sign->sig_data, a_buf + l_shift_mem, l_sign->sig_len);
    return l_sign;
}

void sphincsplus_private_and_public_keys_delete(sphincsplus_private_key_t *a_skey,
        sphincsplus_public_key_t *a_pkey) 
{
    sphincsplus_private_key_delete(a_skey);
    sphincsplus_public_key_delete(a_pkey);
}

void sphincsplus_private_key_delete(sphincsplus_private_key_t *a_skey)
{
    if (a_skey) {
        DAP_DEL_Z(a_skey->data);
    }
}

void sphincsplus_public_key_delete(sphincsplus_public_key_t *a_pkey)
{
    if (a_pkey) {
        DAP_DEL_Z(a_pkey->data);
    }
}

void sphincsplus_signature_delete(sphincsplus_signature_t *a_sig){
    assert(a_sig);
    DAP_DEL_Z(a_sig->sig_data);
    a_sig->sig_len = 0;
}

/*
 * Returns the length of a secret key, in bytes
 */
uint64_t dap_enc_sphincsplus_crypto_sign_secretkeybytes()
{
    return sphincsplus_crypto_sign_secretkeybytes();
}

/*
 * Returns the length of a public key, in bytes
 */
uint64_t dap_enc_sphincsplus_crypto_sign_publickeybytes()
{
    return sphincsplus_crypto_sign_publickeybytes();
}

/*
 * Returns the length of the seed required to generate a key pair, in bytes
 */
uint64_t dap_enc_sphincsplus_crypto_sign_seedbytes()
{
    return sphincsplus_crypto_sign_seedbytes();
}

size_t dap_enc_sphincsplus_calc_signature_unserialized_size()
{
    return sizeof(sphincsplus_signature_t); 
}