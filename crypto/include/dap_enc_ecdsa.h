#ifndef _DAP_ENC_ECDSA_H_
#define _DAP_ENC_ECDSA_H_

#include <secp256k1.h>
#include "sig_ecdsa/ecdsa_params.h"
#include "dap_enc_key.h"




enum DAP_ECDSA_SIGN_SECURITY {
    ECDSA_TOY = 0, ECDSA_MAX_SPEED, ECDSA_MIN_SIZE, ECDSA_MAX_SECURITY
};

void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type);

void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key);

void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                    size_t kex_size, const void *seed, size_t seed_size,
                                    size_t key_size);
void *dap_enc_sig_ecdsa_key_delete(dap_enc_key_t *a_key);

int dap_enc_sig_ecdsa_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size);

int dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature,
                                      const size_t signature_size);

uint8_t *dap_enc_sig_ecdsa_write_signature(const ecdsa_signature_t* a_sign, size_t *a_sign_out);
uint8_t *dap_enc_sig_ecdsa_write_private_key(const ecdsa_private_key_t* a_private_key, size_t *a_buflen_out);
uint8_t *dap_enc_sig_ecdsa_write_public_key(const ecdsa_public_key_t* a_public_key, size_t *a_buflen_out);
ecdsa_signature_t* dap_enc_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen);
ecdsa_private_key_t* dap_enc_sig_ecdsa_read_private_key(const uint8_t *a_buf, size_t a_buflen);
ecdsa_public_key_t *dap_enc_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen);


void *dap_enc_ecdsa_signature_delete(void *a_sig);
void *dap_enc_ecdsa_private_key_delete(ecdsa_private_key_t* privateKey);
void *dap_enc_ecdsa_public_key_delete(ecdsa_public_key_t* publicKey);
void *dap_enc_ecdsa_private_and_public_keys_delete(ecdsa_private_key_t* privateKey, ecdsa_public_key_t* publicKey);



//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_sig_size(UNUSED_ARG const void *a_in)
//{
//    return sizeof(ecdsa_signature_t);
//}

//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_private_key_size(UNUSED_ARG const void *a_in)
//{
//    return sizeof(ecdsa_private_key_t);
//}

//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_public_key_size(UNUSED_ARG const void *a_in)
//{
//    return sizeof(ecdsa_public_key_t);
//}

//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_sig_size(const void *a_sign)
//{
//    if (!a_sign)
//        return 0;
//    return sizeof(uint64_t) * 2 + sizeof(uint32_t) + ECDSA_SIG_SIZE;
//}

//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_private_key_size(const void *a_skey)
//{
//// sanity check
//// TEMPORARILY DISABLED
////    if(!a_skey || !ecdsa_params_init(&l_p, ((ecdsa_private_key_t *)a_skey)->kind))
////        return 0;
//// func work
//    return sizeof(uint64_t) + sizeof(uint32_t);
//}

//DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_public_key_size(const void *a_pkey)
//{
//// sanity check
//// TEMPORARILY DISABLED
////    if(!a_pkey || !ecdsa_params_init(&l_p, ((ecdsa_public_key_t *)a_pkey)->kind))
////        return 0;
//// func work
//    return sizeof(uint64_t) + sizeof(uint32_t);
//}
#endif
