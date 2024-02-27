#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <secp256k1.h>
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

    a_key->_inheritor = NULL;
    a_key->_inheritor_size =0;
    a_key->type = DAP_ENC_KEY_TYPE_SIG_ECDSA;
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
    unsigned char randomize[32];

    int retcode;

    //not sure we need this for ECDSA
    //dap_enc_sig_ecdsa_set_type(ECDSA_MAX_SPEED)

    //int32_t type = 2;
    key->priv_key_data_size =sizeof(ecdsa_private_key_t);
    key->pub_key_data_size = sizeof(ecdsa_public_key_t);
    key->_inheritor_size=sizeof(ecdsa_context_t);
    key->priv_key_data = malloc(key->priv_key_data_size);
    key->pub_key_data = malloc(key->pub_key_data_size);
    key->_inheritor=malloc(key->_inheritor_size);

    ecdsa_context_t* ctx=secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    key->_inheritor=ctx;

    if (!randombytes(randomize, sizeof(randomize))) {
        printf("Failed to generate randomness\n");
        return;
    }

    retcode = secp256k1_context_randomize(ctx, randomize);
    assert(retcode);

    retcode = secp256k1_ec_pubkey_create(ctx,(ecdsa_public_key_t*)key->pub_key_data,(const unsigned char*) key->priv_key_data);
    assert(retcode);

    if(retcode) {
        log_it(L_CRITICAL, "Error generating ECDSA key pair");
        secp256k1_context_destroy(ctx);
	/*secure_erase(key, sizeof(seckey));finish this bit*/
	return;
    }
}


//int dap_enc_sig_ecdsa_get_sign(dap_enc_key_t *a_key, const void *a_msg,
//        const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
//{
//    if(a_sig_size < sizeof(ecdsa_signature_t)) {
//        log_it(L_ERROR, "bad signature size");
//        return -1;
//    }

//    return ecdsa_crypto_sign((ecdsa_signature_t *)a_sig, (const unsigned char *) a_msg, a_msg_size, a_key->priv_key_data);
//}


size_t dap_enc_sig_ecdsa_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size)
{   unsigned char randomize[32];

    if (signature_size != sizeof(ecdsa_signature_t)) {
        log_it(L_ERROR, "Invalid ecdsa signature size");
        return -10;
    }

    if (!randombytes(randomize, sizeof(randomize))) {
        printf("Failed to generate randomness\n");
        return;
    }

    int retcode = secp256k1_context_randomize(key->_inheritor, randomize);
    assert(retcode);


    if (key->priv_key_data_size != sizeof(ecdsa_private_key_t)) {
        log_it(L_ERROR, "Invalid ecdsa key");
        return -11;
    }
    ecdsa_private_key_t *privateKey = key->priv_key_data;


    ecdsa_signature_t *sig = signature;
//    sig->data = DAP_NEW_SIZE(byte_t,ECDSA_SIG_SIZE);

    retcode = secp256k1_ecdsa_sign(key->_inheritor, sig, msg, privateKey, NULL, NULL);

    if (retcode != 0)
        log_it(L_ERROR, "Failed to sign message");
    return retcode;
}

size_t dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature,
                                      const size_t signature_size)
{

    if (key->pub_key_data_size != sizeof(ecdsa_private_key_t)) {
        log_it(L_ERROR, "Invalid ecdsa key");
        return -11;
    }
    ecdsa_private_key_t *publicKey = key->pub_key_data;

    ecdsa_signature_t *sig = signature;
    if (sizeof(ecdsa_signature_t) != signature_size)
        return -1;

    int retcode = secp256k1_ecdsa_verify(key->_inheritor, sig, msg, publicKey);
    if (retcode != 0)
        log_it(L_ERROR, "Failed to verify signature");
    return retcode;
}





///* Deserialize a signature */
//void *dap_enc_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen)
//{
//    // sanity check
//        dap_return_val_if_pass(!a_buf || a_buflen < sizeof(ecdsa_signature_t) + sizeof(ecdsa_context_t), NULL);
//    // func work
//        uint64_t l_buflen;
//        uint64_t l_sig_len = a_buflen - sizeof(uint64_t) * 2 - sizeof(ecdsa_context_t);
//        ecdsa_signature_t* l_sign = NULL;
//        DAP_NEW_Z_RET_VAL(l_sign, ecdsa_signature_t, NULL, NULL);
//        DAP_NEW_Z_SIZE_RET_VAL(l_sign->sig_data, uint8_t, l_sig_len, NULL, l_sign);

//        int l_res_des = dap_deserialize_multy(a_buf, a_buflen, 4,
//            &l_buflen, (uint64_t)sizeof(uint64_t),
//            l_sign->sig_data, (uint64_t)l_sig_len);
//    // out work
//        int l_res_check = sphincsplus_check_params(&l_sign->sig_params);
//        if (l_res_des || l_res_check) {
//            log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
//            DAP_DEL_MULTY(l_sign->sig_data, l_sign);
//            return NULL;
//        }
//        return l_sign;


//}

// Serialize a public key into a buffer.
uint8_t* dap_enc_sig_ecdsa_write_public_key(const ecdsa_public_key_t* a_public_key, size_t* a_buflen_out) {

    uint64_t l_buflen =
            sizeof(uint64_t) +
            ECDSA_PUBLIC_KEY_SIZE;

    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);

    memcpy(l_ptr, a_public_key->data, ECDSA_PUBLIC_KEY_SIZE);
    assert(l_ptr + ECDSA_PUBLIC_KEY_SIZE - l_buf == (int64_t)l_buflen);

    if (a_buflen_out)
        *a_buflen_out = l_buflen;

    return l_buf;
}



///* Serialize a public key. */
//uint8_t *dap_enc_sig_dilithium_write_public_key(const void *a_public_key, size_t *a_buflen_out)
//{
//// in work
//    a_buflen_out ? *a_buflen_out = 0 : 0;
//    ecdsa_public_key_t *l_public_key = (ecdsa_public_key_t *)a_public_key;
//   /* dilithium_param_t p;*/
//    dap_return_val_if_pass(!l_public_key/* || !dilithium_params_init(&p, l_public_key->kind)*/, NULL);
//// func work
//    uint64_t l_buflen = dap_enc_sig_ecdsa_ser_public_key_size(a_public_key);
//    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 6,
//                        &l_buflen, (uint64_t)sizeof(uint64_t),
//                      /*  &l_public_key->kind, (uint64_t)sizeof(uint32_t),*/
//                        l_public_key->data, (uint64_t)p.CRYPTO_PUBLICKEYBYTES);
    
//// out work
//    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
//    return l_buf;
//}


///* Serialize a private key. */
//uint8_t *dap_enc_sig_ecdsa_write_private_key(const void *a_private_key, size_t *a_buflen_out)
//{
//// in work
//    a_buflen_out ? *a_buflen_out = 0 : 0;
//    ecdsa_private_key_t *l_private_key = (ecdsa_private_key_t *)a_private_key;

//   /*
//    dilithium_param_t p;
//    dap_return_val_if_pass(!l_private_key || !dilithium_params_init(&p, l_private_key->kind), NULL);
//    */


//    // func work
//    uint64_t l_buflen = dap_enc_sig_ecdsa_ser_private_key_size(a_private_key);
//    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 6,
//                        &l_buflen, (uint64_t)sizeof(uint64_t),
//                       /* &l_private_key->kind, (uint64_t)sizeof(uint32_t),*/
//                        l_private_key->data, (uint64_t)p.CRYPTO_SECRETKEYBYTES);
//// out work
//    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
//    return l_buf;
//}

uint8_t* dap_enc_sig_ecdsa_write_private_key(const ecdsa_private_key_t* a_private_key, size_t* a_buflen_out) {


    uint64_t l_buflen =
            sizeof(uint64_t) +
            ECDSA_PRIVATE_KEY_SIZE;

    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);
    memcpy(l_ptr, a_private_key->data, ECDSA_PRIVATE_KEY_SIZE);
    assert(l_ptr + ECDSA_PRIVATE_KEY_SIZE - l_buf == (int64_t)l_buflen);

    if(a_buflen_out)
        *a_buflen_out = l_buflen;
    return l_buf;
}


ecdsa_private_key_t* dap_enc_sig_ecdsa_read_private_key(const uint8_t *a_buf, size_t a_buflen) {
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

    ecdsa_private_key_t* l_private_key = DAP_NEW_Z(ecdsa_private_key_t);
    if (!l_private_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
 //   l_private_key->data = DAP_NEW_Z_SIZE(uint8_t, ECDSA_PRIVATE_KEY_SIZE);
    if (!l_private_key->data) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_private_key);
        return NULL;
    }
    memcpy(l_private_key->data, l_ptr, ECDSA_PRIVATE_KEY_SIZE);
    assert(l_ptr + ECDSA_PRIVATE_KEY_SIZE - a_buf == (int64_t)l_buflen);

    return l_private_key;
}

///* Deserialize a private key. */
//void *dap_enc_sig_ecdsa_read_private_key(const uint8_t *a_buf, size_t a_buflen)
//{
//   // in work
//        dap_return_val_if_pass(!a_buf || a_buflen < sizeof(ecdsa_signature_t) + sizeof(ecdsa_context_t), NULL);

//   // func work
//        uint64_t l_buflen = 0;
//        uint64_t l_pkey_len = a_buflen -  sizeof(uint64_t) - sizeof(ecdsa_context_t);

//        ecdsa_private_key_t *l_skey = NULL;
//        DAP_NEW_Z_RET_VAL(l_skey, ecdsa_private_key_t, NULL, NULL);
//        DAP_NEW_Z_SIZE_RET_VAL(l_skey->data, uint8_t, ECDSA_PRIVATE_KEY_SIZE, NULL, l_skey);

//        int l_res_des = dap_deserialize_multy(a_buf, a_buflen, 4,
//                                              &l_buflen, (uint64_t)sizeof(uint64_t),
//                                              l_skey->data, (uint64_t)l_skey_len);
//        // out work
//        uint64_t l_skey_len_exp = ECDSA_PRIVATE_KEY_SIZE/8;;
//        if (l_res_des) {
//            log_it(L_ERROR,"::read_private_key() deserialise public key, err code %d", l_res_des);
//            DAP_DEL_MULTY(l_skey->data, l_skey);
//            return NULL;
//        }
//        if (l_skey_len != l_skey_len_exp) {
//            log_it(L_ERROR,"::read_private_key() l_pkey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %"DAP_UINT64_FORMAT_U"", l_skey_len, l_skey_len_exp);
//            DAP_DEL_MULTY(l_skey->data, l_skey);
//            return NULL;
//        }
//        return l_skey;

//}


ecdsa_public_key_t* dap_enc_sig_ecdsa_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
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

    ecdsa_public_key_t* l_public_key = DAP_NEW_Z(ecdsa_public_key_t);
    if (!l_public_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
 //   l_public_key->data = DAP_NEW_Z_SIZE(uint8_t, ECDSA_PUBLIC_KEY_SIZE);
    if (!l_public_key->data) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_public_key);
        return NULL;
    }
    memcpy(l_public_key->data, l_ptr, ECDSA_PUBLIC_KEY_SIZE);
    assert(l_ptr + ECDSA_PUBLIC_KEY_SIZE - a_buf == (int64_t)l_buflen);

    return l_public_key;
}






///* Deserialize a public key. */
//void *dap_enc_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen)
//{
//    // in work
//        dap_return_val_if_pass(!a_buf || a_buflen < sizeof(ecdsa_signature_t) + sizeof(ecdsa_context_t), NULL);
//    // func work
//        uint64_t l_buflen = 0;
//        uint64_t l_pkey_len = a_buflen -  sizeof(uint64_t) - sizeof(ecdsa_context_t);

//        ecdsa_public_key_t *l_pkey = NULL;
//        DAP_NEW_Z_RET_VAL(l_pkey, ecdsa_public_key_t, NULL, NULL);
//        DAP_NEW_Z_SIZE_RET_VAL(l_pkey->data, uint8_t, l_pkey_len, NULL, l_pkey);

//        int l_res_des = dap_deserialize_multy(a_buf, a_buflen, 4,
//            &l_buflen, (uint64_t)sizeof(uint64_t),
//            l_pkey->data, (uint64_t)l_pkey_len
//        );
//    // out work
//        uint64_t l_pkey_len_exp = sizeof(secp256k1_pubkey)/8;
//        if (l_res_des) {
//            log_it(L_ERROR,"::read_public_key() deserialise public key, err code %d", l_res_des);
//            DAP_DEL_MULTY(l_pkey->data, l_pkey);
//            return NULL;
//        }
//        if (l_pkey_len != l_pkey_len_exp) {
//            log_it(L_ERROR,"::read_public_key() l_pkey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %"DAP_UINT64_FORMAT_U"", l_pkey_len, l_pkey_len_exp);
//            DAP_DEL_MULTY(l_pkey->data, l_pkey);
//            return NULL;
//        }
//        return l_pkey;
//}


uint8_t *dap_enc_sig_ecdsa_write_signature(const ecdsa_signature_t* a_sign, size_t *a_sign_out)
{
    if (!a_sign) {
        log_it(L_ERROR, "::write_signature() a_sign is NULL");
        return NULL;
    }
    size_t l_buflen = sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + ECDSA_SIG_SIZE;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    if (!l_buf) {
        log_it(L_ERROR, "::write_signature() l_buf is NULL — memory allocation error");
        return NULL;
    }


    uint64_t l_sig_len = ECDSA_SIG_SIZE;
    uint8_t *l_ptr = l_buf;
    *(uint64_t *)l_ptr = l_buflen; l_ptr += sizeof(uint64_t);
    *(uint64_t *)l_ptr = l_sig_len; l_ptr += sizeof(uint64_t);
    memcpy(l_ptr, a_sign->data, ECDSA_SIG_SIZE);
    assert(l_ptr + l_sig_len - l_buf == (int64_t)l_buflen);

    if (a_sign_out)
        *a_sign_out = l_buflen;

    return l_buf;
//    // in work
//    a_buflen_out ? *a_buflen_out = 0 : 0;
//    dap_return_val_if_pass(!a_sign, NULL);
//    ecdsa_signature_t *l_sign = (ecdsa_signature_t *)a_sign;
//    // func work
//    uint64_t l_buflen = dap_enc_sig_ecdsa_ser_sig_size(l_sign);
//    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 4,
//                                         &l_buflen, (uint64_t)sizeof(uint64_t),

//                                         l_sign->data, DAP_ENC_ECDSA_SKEY_LEN);
//    // out work
//    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
//    return l_buf;
}


ecdsa_signature_t* dap_enc_sig_ecdsa_read_signature(const uint8_t* a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_signature() a_buf is NULL");
        return NULL;
    }

    uint64_t l_buflen = 0;
//    uint32_t l_degree = 0;
//    uint32_t l_kind = 0;
//    uint32_t l_type = 0;
    uint64_t l_sig_len = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen != l_buflen) {
        log_it(L_ERROR, "::read_signature() a_buflen %zu is not equal to sign size (%"DAP_UINT64_FORMAT_U")",
                        a_buflen, l_buflen);
        return NULL;
    }

//    l_degree = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//    if (l_degree != FALCON_512 && l_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
//        log_it(L_ERROR, "::read_signature() l_degree %ul is not supported", l_degree);
//        return NULL;
//    }

//    l_kind = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//    if (l_kind != FALCON_COMPRESSED && l_kind != FALCON_PADDED && l_kind != FALCON_CT) { // we are now supporting only compressed, padded and ct signatures
//        log_it(L_ERROR, "::read_signature() l_kind %ul is not supported", l_kind);
//        return NULL;
//    }

//    l_type = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
//    if (l_type != FALCON_DYNAMIC && l_type != FALCON_TREE) { // we are now supporting only sign and sign open signatures
//        log_it(L_ERROR, "::read_signature() l_type %ul is not supported", l_type);
//        return NULL;
//    }

    l_sig_len = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (l_buflen != sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len) {
        log_it(L_ERROR, "::read_signature() l_buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
               l_buflen, sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len);
        return NULL;
    }

    ecdsa_signature_t *l_sign = DAP_NEW(ecdsa_signature_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

//    l_sign->degree = l_degree;
//    l_sign->kind = l_kind;
//    l_sign->type = l_type;
//    l_sign->sig_len = l_sig_len;
  //  l_sign->data = DAP_NEW_SIZE(uint8_t, ECDSA_SIG_SIZE);
    memcpy(l_sign->data, l_ptr, ECDSA_SIG_SIZE);
    assert(l_ptr + ECDSA_SIG_SIZE - a_buf == (int64_t)l_buflen);

    return l_sign;
}

void *dap_enc_sig_ecdsa_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    memset(((ecdsa_signature_t *)a_sig)->data,0,ECDSA_SIG_SIZE);

}


void *dap_enc_sig_ecdsa_private_key_delete(ecdsa_private_key_t* privateKey) {
    if (privateKey){
        secure_erase(privateKey->data, sizeof(privateKey->data));

    }
}

void *dap_enc_sig_ecdsa_public_key_delete(ecdsa_public_key_t* publicKey) {
    if (publicKey) {
        memset(publicKey->data, 0, ECDSA_PUBLIC_KEY_SIZE);
    }
}

void *dap_enc_sig_ecdsa_private_and_public_keys_delete(ecdsa_private_key_t* privateKey, ecdsa_public_key_t* publicKey) {
        ecdsa_private_key_delete(privateKey);
        ecdsa_public_key_delete(publicKey);
}


//void *dap_enc_sig_ecdsa_key_delete(struct dap_enc_key *key) {

//    if (key->priv_key_data) {
//        ecdsa_private_key_delete(key->priv_key_data);
//    }
//    if (key->pub_key_data) {
//        ecdsa_public_key_delete(key->pub_key_data);
//    }
//}

