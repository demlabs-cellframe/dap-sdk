#pragma once

#include "dap_enc_key.h"

/*
 * Unified AES-256-CBC symmetric encryption.
 * Replaces the separate IAES/OAES modules with a single implementation.
 * Detects AES-NI at runtime and uses hardware acceleration when available.
 */

void dap_enc_aes256_key_new(dap_enc_key_t *a_key);
void dap_enc_aes256_key_generate(dap_enc_key_t *a_key, const void *kex_buf,
        size_t kex_size, const void *seed, size_t seed_size, size_t key_size);
void dap_enc_aes256_key_delete(dap_enc_key_t *a_key);

size_t dap_enc_aes256_cbc_calc_encode_size(size_t a_size);
size_t dap_enc_aes256_cbc_calc_decode_size(size_t a_size);

size_t dap_enc_aes256_cbc_encrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out);
size_t dap_enc_aes256_cbc_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out);
size_t dap_enc_aes256_cbc_encrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size);
size_t dap_enc_aes256_cbc_decrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size);
