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


void dap_enc_sig_sphincsplus_key_new(struct dap_enc_key *a_key) {
    a_key->type = DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS;
    a_key->enc = NULL;
    a_key->enc_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_sphincsplus_get_sign;
    a_key->dec_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_sphincsplus_verify_sign;
}

void dap_enc_sig_sphincsplus_key_new_generate(struct dap_enc_key *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, size_t a_key_size) {

    (void) a_kex_buf;
    (void) a_kex_size;
    (void) a_key_size;
    
    // seed norming
    unsigned long long l_key_size = sphincsplus_crypto_sign_seedbytes();
    unsigned char l_seedbuf[l_key_size];

    if(a_seed && a_seed_size > 0) {
        SHA3_256((unsigned char *) l_seedbuf, (const unsigned char *) a_seed, a_seed_size);
    } else {
        randombytes(l_seedbuf, l_key_size);
    }
    // creating key pair
    dap_enc_sig_sphincsplus_key_new(a_key);

    a_key->priv_key_data_size = sphincsplus_crypto_sign_secretkeybytes();
    a_key->pub_key_data_size = sphincsplus_crypto_sign_publickeybytes();
    if ( !(a_key->priv_key_data = malloc(a_key->priv_key_data_size)) || !(a_key->pub_key_data = malloc(a_key->pub_key_data_size)) ) {
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    };

    if(sphincsplus_crypto_sign_seed_keypair((unsigned char *)a_key->pub_key_data, (unsigned char *)a_key->priv_key_data, l_seedbuf)) {
        DAP_DEL_Z(a_key->priv_key_data);
        DAP_DEL_Z(a_key->pub_key_data);
        log_it(L_CRITICAL, "Error generating Sphincs key pair");
        return;
    }
}

size_t dap_enc_sig_sphincsplus_get_sign(struct dap_enc_key *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign, const size_t a_sign_size){

    if(a_sign_size < sphincsplus_crypto_sign_bytes()) {
        log_it(L_ERROR, "Bad signature size");
        return 0;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
    l_sign->sig_data = DAP_NEW_Z_SIZE(uint8_t, sphincsplus_crypto_sign_bytes());
    if (!l_sign->sig_data) {
        log_it(L_CRITICAL, "Memory allocation error");
        return 0;
    }
    size_t l_sig_len = 0;

    sphincsplus_crypto_sign_signature(l_sign->sig_data, &l_sig_len, (const unsigned char *)a_msg, a_msg_size, a_key->priv_key_data);

    l_sign->sig_params.sSPX_BYTES = SPX_BYTES;
    l_sign->sig_params.sSPX_D = SPX_D;
    l_sign->sig_params.sSPX_FORS_BYTES = SPX_FORS_BYTES;
    l_sign->sig_params.sSPX_FORS_MSG_BYTES = SPX_FORS_MSG_BYTES;
    l_sign->sig_params.sSPX_N = SPX_N;
    l_sign->sig_params.sSPX_TREE_HEIGHT = SPX_TREE_HEIGHT;
    l_sign->sig_params.sSPX_WOTS_BYTES = SPX_WOTS_BYTES;
    l_sign->sig_params.sSPX_WOTS_LEN = SPX_WOTS_LEN;
    l_sign->sig_len = SPX_BYTES;

    return l_sig_len;
}

size_t dap_enc_sig_sphincsplus_verify_sign(struct dap_enc_key *a_key, const void *a_msg, const size_t a_msg_size, const void *a_sign,
        const size_t a_sign_size)
{
    if(a_sign_size < sphincsplus_crypto_sign_bytes()) {
        log_it(L_ERROR, "Bad signature size");
        return 0;
    }
    sphincsplus_signature_t *l_sign = (sphincsplus_signature_t *)a_sign;
    int l_ret = sphincsplus_crypto_sign_verify(l_sign->sig_data, l_sign->sig_len, a_msg, a_msg_size, a_key->pub_key_data);
    if (l_ret)
        log_it(L_ERROR, "Failed to verify signature");
    return l_ret < 0 ? 0 : l_ret;
}

void dap_enc_sig_sphincsplus_key_delete(struct dap_enc_key *key) {

    if (key->priv_key_data) {
        memset(key->priv_key_data, 0, key->priv_key_data_size);
        DAP_DEL_Z(key->priv_key_data);
    }
    if (key->pub_key_data) {
        memset(key->pub_key_data, 0, key->pub_key_data_size);
        DAP_DEL_Z(key->pub_key_data);
    }
}

// // Serialize a public key into a buffer.
// uint8_t* dap_enc_falcon_write_public_key(const falcon_public_key_t* a_public_key, size_t* a_buflen_out) {
//     //Serialized key have format:
//     // 8 first bytes - size of overall serialized key
//     // 4 bytes - degree of key
//     // 4 bytes - kind of key
//     // 4 bytes - type of key
//     // n bytes - public key data

//     uint64_t l_buflen =
//             sizeof(uint64_t) +
//             sizeof(uint32_t) * 3 +
//             FALCON_PUBKEY_SIZE(a_public_key->degree);

//     uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
//     if (!l_buf) {
//         log_it(L_CRITICAL, "Memory allocation error");
//         return NULL;
//     }
//     uint32_t l_degree = a_public_key->degree;
//     uint32_t l_kind = a_public_key->kind;
//     uint32_t l_type = a_public_key->type;

//     uint8_t *l_ptr = l_buf;
//     *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);
//     *(uint32_t *)l_ptr = l_degree; l_ptr += sizeof(uint32_t);
//     *(uint32_t *)l_ptr = l_kind; l_ptr += sizeof(uint32_t);
//     *(uint32_t *)l_ptr = l_type; l_ptr += sizeof(uint32_t);
//     memcpy(l_ptr, a_public_key->data, FALCON_PUBKEY_SIZE(a_public_key->degree));
//     assert(l_ptr + FALCON_PUBKEY_SIZE(a_public_key->degree) - l_buf == (int64_t)l_buflen);

//     if (a_buflen_out)
//         *a_buflen_out = l_buflen;

//     return l_buf;
// }

/* Serialize a private key. */
uint8_t* dap_enc_sphincsplus_write_private_key(const sphincsplus_private_key_t* a_private_key, size_t *a_buflen_out)
{
    if (!a_private_key)
        return NULL;
    size_t l_secret_length = dap_sphincsplus_crypto_sign_secretkeybytes();
    uint64_t l_buflen = sizeof(uint64_t) + l_secret_length;
    byte_t *l_buf = DAP_NEW_Z_SIZE(byte_t, l_buflen);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    memcpy(l_buf, &l_buflen, sizeof(uint64_t));
    memcpy(l_buf + sizeof(uint64_t), a_private_key->data, l_secret_length);

    if(a_buflen_out)
        *a_buflen_out = l_buflen;
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
    size_t l_secret_length = dap_sphincsplus_crypto_sign_secretkeybytes();

    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    if(l_buflen != (uint64_t) a_buflen)
        return NULL;

    if(a_buflen < (sizeof(uint64_t) + l_secret_length) ){
        log_it(L_ERROR,"::read_private_key() Buflen %zd is smaller than all fields together(%zd)", a_buflen,
               sizeof(uint64_t) + l_secret_length );
        return NULL;
    }

    sphincsplus_private_key_t* l_ret = DAP_NEW(sphincsplus_private_key_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    l_ret->data = DAP_NEW_SIZE(uint8_t, l_secret_length);
    memcpy(l_ret->data, a_buf + sizeof(uint64_t), l_secret_length);
    return l_ret;
}

// falcon_public_key_t* dap_enc_falcon_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
//     if (!a_buf) {
//         log_it(L_ERROR, "::read_public_key() a_buf is NULL");
//         return NULL;
//     }

//     if (a_buflen < sizeof(uint32_t) * 3) {
//         log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is smaller than first four fields(%zu)", a_buflen, sizeof(uint32_t) * 3);
//         return NULL;
//     }

//     uint64_t l_buflen = 0;
//     uint32_t l_degree = 0;
//     uint32_t l_kind = 0;
//     uint32_t l_type = 0;
//     uint8_t *l_ptr = (uint8_t *)a_buf;

//     l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
//     if (a_buflen < l_buflen) {
//         log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is less than l_buflen %"DAP_UINT64_FORMAT_U, a_buflen, l_buflen);
//         return NULL;
//     }

//     l_degree = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//     if (l_degree != FALCON_512 && l_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
//         log_it(L_ERROR, "::read_public_key() l_degree %ul is not supported", l_degree);
//         return NULL;
//     }
//     if (l_buflen != (sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PUBKEY_SIZE(l_degree))) {
//         log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
//                         a_buflen, (sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PUBKEY_SIZE(l_degree)));
//         return NULL;
//     }

//     l_kind = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//     if (l_kind != FALCON_COMPRESSED && l_kind != FALCON_PADDED && l_kind != FALCON_CT) { // we are now supporting only 512 and 1024 degrees
//         log_it(L_ERROR, "::read_public_key() l_kind %ul is not supported", l_kind);
//         return NULL;
//     }

//     l_type = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//     if (l_type != FALCON_DYNAMIC && l_type != FALCON_TREE) { // we are now supporting only 512 and 1024 degrees
//         log_it(L_ERROR, "::read_public_key() l_type %ul is not supported", l_type);
//         return NULL;
//     }

//     falcon_public_key_t* l_public_key = DAP_NEW_Z(falcon_public_key_t);
//     if (!l_public_key) {
//         log_it(L_CRITICAL, "Memory allocation error");
//         return NULL;
//     }
//     l_public_key->degree = l_degree;
//     l_public_key->kind = l_kind;
//     l_public_key->type = l_type;
//     l_public_key->data = DAP_NEW_Z_SIZE(uint8_t, FALCON_PUBKEY_SIZE(l_degree));
//     if (!l_public_key->data) {
//         log_it(L_CRITICAL, "Memory allocation error");
//         DAP_DEL_Z(l_public_key);
//         return NULL;
//     }
//     memcpy(l_public_key->data, l_ptr, FALCON_PUBKEY_SIZE(l_degree));
//     assert(l_ptr + FALCON_PUBKEY_SIZE(l_degree) - a_buf == (int64_t)l_buflen);

//     return l_public_key;
// }

/* Serialize a signature */
uint8_t *dap_enc_sphincsplus_write_signature(const sphincsplus_signature_t *a_sign, size_t *a_size_out)
{
    if(!a_sign ) {
        return NULL;
    }

    size_t l_shift_mem = 0;
    uint64_t l_buflen = a_sign->sig_len + sizeof(uint64_t) * 2;

    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_buflen);
    if(! l_buf)
        return NULL;

    memcpy(l_buf, &l_buflen, sizeof(uint64_t));
    l_shift_mem += sizeof(uint64_t);

    memcpy(l_buf + l_shift_mem, &a_sign->sig_len, sizeof(uint64_t));
    l_shift_mem += sizeof(uint64_t);

    memcpy(l_buf + l_shift_mem, a_sign->sig_data, a_sign->sig_len );
    l_shift_mem += a_sign->sig_len;
    assert(l_shift_mem == l_buflen);
    if(a_size_out)
        *a_size_out = l_buflen;
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

    sphincsplus_signature_t* l_sign = DAP_NEW_Z(sphincsplus_signature_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

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
    }else{
        memcpy(l_sign->sig_data, a_buf + l_shift_mem, l_sign->sig_len);
        return l_sign;
    }
}


// void falcon_private_and_public_keys_delete(falcon_private_key_t* privateKey, falcon_public_key_t* publicKey) {
//     falcon_private_key_delete(privateKey);
//     falcon_public_key_delete(publicKey);
// }

// void falcon_private_key_delete(falcon_private_key_t* privateKey) {
//     if (privateKey) {
//         memset(privateKey->data, 0, FALCON_PRIVKEY_SIZE(privateKey->degree));
//         DAP_DEL_Z(privateKey->data);
//         privateKey->degree = 0;
//         privateKey->type = 0;
//         privateKey->kind = 0;
//     }
// }

// void falcon_public_key_delete(falcon_public_key_t* publicKey) {
//     if (publicKey) {
//         memset(publicKey->data, 0, FALCON_PUBKEY_SIZE(publicKey->degree));
//         DAP_DEL_Z(publicKey->data);
//         publicKey->degree = 0;
//         publicKey->type = 0;
//         publicKey->kind = 0;
//     }
// }

/*
 * Returns the length of a secret key, in bytes
 */
unsigned long long dap_sphincsplus_crypto_sign_secretkeybytes()
{
    return sphincsplus_crypto_sign_secretkeybytes();
}

/*
 * Returns the length of a public key, in bytes
 */
unsigned long long dap_sphincsplus_crypto_sign_publickeybytes()
{
    return sphincsplus_crypto_sign_publickeybytes();
}

/*
 * Returns the length of a signature, in bytes
 */
unsigned long long dap_sphincsplus_crypto_sign_size()
{
    return sizeof(sphincsplus_signature_t);
}

/*
 * Returns the length of the seed required to generate a key pair, in bytes
 */
unsigned long long dap_sphincsplus_crypto_sign_seedbytes()
{
    return sphincsplus_crypto_sign_seedbytes();
}