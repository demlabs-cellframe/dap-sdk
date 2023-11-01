#ifndef _DAP_ENC_DILITHIUM_H_
#define _DAP_ENC_DILITHIUM_H_

#include "sig_dilithium/dilithium_params.h"
#include "dap_enc_key.h"


enum DAP_DILITHIUM_SIGN_SECURITY {
    DILITHIUM_TOY = 0, DILITHIUM_MAX_SPEED, DILITHIUM_MIN_SIZE, DILITHIUM_MAX_SECURITY
};

void dap_enc_sig_dilithium_set_type(enum DAP_DILITHIUM_SIGN_SECURITY type);

void dap_enc_sig_dilithium_key_new(dap_enc_key_t *a_key);

void dap_enc_sig_dilithium_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                    size_t kex_size, const void *seed, size_t seed_size,
                                    size_t key_size);

int dap_enc_sig_dilithium_get_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size);

int dap_enc_sig_dilithium_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size);

void dap_enc_sig_dilithium_key_delete(dap_enc_key_t *key);

size_t dap_enc_dilithium_calc_signature_unserialized_size(void);

static inline size_t dap_enc_dilithium_calc_signagture_size(dilithium_signature_t *a_sign)
{
    return sizeof(uint64_t) * 2 + sizeof(uint32_t) + a_sign->sig_len;
}

uint8_t *dap_enc_dilithium_write_signature(const void *a_sign, size_t *a_buflen_out);
dilithium_signature_t *dap_enc_dilithium_read_signature(uint8_t *a_buf, size_t a_buflen);
dilithium_signature_t *dap_enc_dilithium_read_signature_old(uint8_t *a_buf, size_t a_buflen);
dilithium_signature_t *dap_enc_dilithium_read_signature_old2(uint8_t *a_buf, size_t a_buflen);

uint8_t *dap_enc_dilithium_write_private_key(const void *a_private_key, size_t *a_buflen_out);
uint8_t *dap_enc_dilithium_write_public_key(const void *a_public_key, size_t *a_buflen_out);
dilithium_private_key_t *dap_enc_dilithium_read_private_key(const uint8_t *a_buf, size_t a_buflen);
dilithium_private_key_t *dap_enc_dilithium_read_private_key_old(const uint8_t *a_buf, size_t a_buflen);

dilithium_public_key_t* dap_enc_dilithium_read_public_key(const uint8_t *a_buf, size_t a_buflen);
dilithium_public_key_t* dap_enc_dilithium_read_public_key_old(const uint8_t *a_buf, size_t a_buflen);
dilithium_public_key_t* dap_enc_dilithium_read_public_key_old2(const uint8_t *a_buf, size_t a_buflen);
#endif
