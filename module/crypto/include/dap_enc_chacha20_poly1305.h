#pragma once

#include "dap_enc_key.h"

void dap_enc_chacha20_poly1305_key_new(dap_enc_key_t *a_key);
void dap_enc_chacha20_poly1305_key_generate(dap_enc_key_t *a_key, const void *kex_buf,
        size_t kex_size, const void *seed, size_t seed_size, size_t key_size);
void dap_enc_chacha20_poly1305_key_delete(dap_enc_key_t *a_key);

size_t dap_enc_chacha20_poly1305_calc_encode_size(size_t a_size);
size_t dap_enc_chacha20_poly1305_calc_decode_size(size_t a_size);

size_t dap_enc_chacha20_poly1305_encrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out);
size_t dap_enc_chacha20_poly1305_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out);
