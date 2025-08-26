#ifndef _DAP_ENC_ECDSA_H_
#define _DAP_ENC_ECDSA_H_
#include "dap_enc_key.h"
#include "sig_ecdsa/ecdsa_params.h"

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

uint8_t *dap_enc_sig_ecdsa_write_signature(const void* a_sign, size_t *a_sign_out);
uint8_t *dap_enc_sig_ecdsa_write_public_key(const void *a_public_key, size_t *a_buflen_out);
void* dap_enc_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen);
void* dap_enc_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen);
bool dap_enc_sig_ecdsa_hash_fast(const unsigned char *a_data, size_t a_data_size, dap_hash_fast_t *a_out);

DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_key_size(UNUSED_ARG const void *a_in) {
    return ECDSA_PRIVATE_KEY_SIZE;
}
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_pkey_size(UNUSED_ARG const void *a_in) {
    return ECDSA_PKEY_SERIALIZED_SIZE;
}
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_key_size(UNUSED_ARG const void *a_in) {
    return ECDSA_PRIVATE_KEY_SIZE;
}
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_pkey_size(UNUSED_ARG const void *a_in) {
    return ECDSA_PUBLIC_KEY_SIZE;
}

DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_signature_size(UNUSED_ARG const void* a_arg) {
    return ECDSA_SIG_SIZE;
}

void dap_enc_sig_ecdsa_signature_delete(void *a_sig);
void dap_enc_sig_ecdsa_private_key_delete(void* privateKey);
void dap_enc_sig_ecdsa_public_key_delete(void* publicKey);
void dap_enc_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t* a_key);
void dap_enc_sig_ecdsa_deinit();

#endif
