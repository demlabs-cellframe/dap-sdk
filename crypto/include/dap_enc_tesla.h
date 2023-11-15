#pragma once

#include "sig_tesla/tesla_params.h"
#include "dap_enc_key.h"


enum DAP_TESLA_SIGN_SECURITY {
    TESLA_TOY = 0, HEURISTIC_MAX_SECURITY_AND_MIN_SIZE, HEURISTIC_MAX_SECURITY_AND_MAX_SPEED, PROVABLY_SECURITY, PROVABLY_MAX_SECURITY
};

void dap_enc_sig_tesla_set_type(enum DAP_TESLA_SIGN_SECURITY type);

void dap_enc_sig_tesla_key_new(dap_enc_key_t *a_key);

void dap_enc_sig_tesla_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                    size_t kex_size, const void * seed, size_t seed_size,
                                    size_t key_size);

int dap_enc_sig_tesla_get_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size);

int dap_enc_sig_tesla_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
        const size_t a_msg_size, void *a_sig, const size_t a_sig_size);

void dap_enc_sig_tesla_key_delete(dap_enc_key_t *key);

size_t dap_enc_tesla_calc_signature_size(void);

uint8_t *dap_enc_tesla_write_signature(const void *a_sign, size_t *a_buflen_out);
tesla_signature_t* dap_enc_tesla_read_signature(uint8_t *a_buf, size_t a_buflen);
uint8_t *dap_enc_tesla_write_private_key(const void *a_private_key, size_t *a_buflen_out);
uint8_t *dap_enc_tesla_write_public_key(const void *a_public_key, size_t *a_buflen_out);
tesla_private_key_t* dap_enc_tesla_read_private_key(const uint8_t *a_buf, size_t a_buflen);
tesla_public_key_t* dap_enc_tesla_read_public_key(const uint8_t *a_buf, size_t a_buflen);

