#ifndef _DAP_ENC_SPHINCSPLUS_H
#define _DAP_ENC_SPHINCSPLUS_H

#include "dap_enc_key.h"


// void dap_enc_sig_falcon_set_degree(falcon_sign_degree_t a_falcon_sign_degree);
// void dap_enc_sig_falcon_set_kind(falcon_kind_t a_falcon_kind);
// void dap_enc_sig_falcon_set_type(falcon_sign_type_t a_falcon_sign_type);

void dap_enc_sig_sphincsplus_key_new(struct dap_enc_key *a_key);

void dap_enc_sig_sphincsplus_key_new_generate(struct dap_enc_key *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, size_t a_key_size);

size_t dap_enc_sig_sphincsplus_get_sign(struct dap_enc_key *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign, const size_t a_sign_size);

size_t dap_enc_sig_sphincsplus_verify_sign(struct dap_enc_key *a_key, const void *a_msg, const size_t a_msg_size, const void *a_sign,
        const size_t a_sign_size);

void dap_enc_sig_sphincsplus_key_delete(struct dap_enc_key *key);

// uint8_t* dap_enc_falcon_write_signature(const falcon_signature_t* a_sign, size_t *a_sign_out);
// falcon_signature_t* dap_enc_falcon_read_signature(const uint8_t* a_buf, size_t a_buflen);

// uint8_t* dap_enc_falcon_write_private_key(const falcon_private_key_t* a_private_key, size_t* a_buflen_out);
// uint8_t* dap_enc_falcon_write_public_key(const falcon_public_key_t* a_public_key, size_t* a_buflen_out);
// falcon_private_key_t* dap_enc_falcon_read_private_key(const uint8_t* a_buf, size_t a_buflen);
// falcon_public_key_t* dap_enc_falcon_read_public_key(const uint8_t* a_buf, size_t a_buflen);

// DAP_STATIC_INLINE size_t dap_enc_falcon_calc_signature_unserialized_size() { return sizeof(falcon_signature_t); }

unsigned long long dap_sphincsplus_crypto_sign_secretkeybytes();
unsigned long long dap_sphincsplus_crypto_sign_publickeybytes();
unsigned long long dap_sphincsplus_crypto_sign_bytes();
unsigned long long dap_sphincsplus_crypto_sign_seedbytes();

#endif //_DAP_ENC_SPHINCSPLUS_H
