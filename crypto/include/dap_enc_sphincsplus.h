#ifndef _DAP_ENC_SPHINCSPLUS_H
#define _DAP_ENC_SPHINCSPLUS_H

#include "dap_enc_key.h"
#include "sphincsplus/sphincsplus_params.h"

void dap_enc_sig_sphincsplus_key_new(dap_enc_key_t *a_key);
void sphincsplus_public_key_delete(sphincsplus_public_key_t *a_pkey);
void sphincsplus_private_key_delete(sphincsplus_private_key_t *a_skey);
void sphincsplus_private_and_public_keys_delete(sphincsplus_private_key_t *a_skey,
        sphincsplus_public_key_t *a_pkey);
void sphincsplus_signature_delete(sphincsplus_signature_t *a_sig);

void dap_enc_sig_sphincsplus_key_new_generate(dap_enc_key_t *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, size_t a_key_size);

int dap_enc_sig_sphincsplus_get_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign, const size_t a_sign_size);
size_t dap_enc_sig_sphincsplus_get_sign_msg(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max);

int dap_enc_sig_sphincsplus_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sign,
const size_t a_sign_size);
size_t dap_enc_sig_sphincsplus_open_sign_msg(dap_enc_key_t *a_key, const void *a_sign_in, const size_t a_sign_size, void *a_msg_out,
        const size_t a_out_size_max);

void dap_enc_sig_sphincsplus_key_delete(dap_enc_key_t *a_key);

uint8_t *dap_enc_sphincsplus_write_signature(const void *a_sign, size_t *a_buflen_out);
sphincsplus_signature_t *dap_enc_sphincsplus_read_signature(const uint8_t *a_buf, size_t a_buflen);

uint8_t *dap_enc_sphincsplus_write_private_key(const void *a_private_key, size_t *a_buflen_out);
uint8_t *dap_enc_sphincsplus_write_public_key(const void* a_public_key, size_t *a_buflen_out);
sphincsplus_private_key_t *dap_enc_sphincsplus_read_private_key(const uint8_t *a_buf, size_t a_buflen);
sphincsplus_public_key_t *dap_enc_sphincsplus_read_public_key(const uint8_t *a_buf, size_t a_buflen);

uint32_t dap_enc_sphincsplus_crypto_sign_secretkeybytes(const sphincsplus_base_params_t *a_params);
uint32_t dap_enc_sphincsplus_crypto_sign_publickeybytes(const sphincsplus_base_params_t *a_params);
uint32_t dap_enc_sphincsplus_crypto_sign_bytes(const sphincsplus_base_params_t *a_params);
uint32_t dap_enc_sphincsplus_crypto_sign_seedbytes(sphincsplus_config_t a_config);
uint32_t dap_enc_sphincsplus_calc_signature_unserialized_size();

#endif //_DAP_ENC_SPHINCSPLUS_H
