/**
 * @file dap_enc_aes.c
 * @brief Unified AES-256-CBC — delegates to IAES (software T-table).
 *
 * Future: runtime dispatch to AES-NI / ARM CE via dap_cpu_features.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_enc_aes.h"
#include "dap_enc_iaes.h"

void dap_enc_aes256_key_new(dap_enc_key_t *a_key)
{
    dap_enc_aes_key_new(a_key);
}

void dap_enc_aes256_key_generate(dap_enc_key_t *a_key, const void *a_kex_buf,
        size_t a_kex_size, const void *a_seed, size_t a_seed_size, size_t a_key_size)
{
    dap_enc_aes_key_generate(a_key, a_kex_buf, a_kex_size, a_seed, a_seed_size, a_key_size);
}

void dap_enc_aes256_key_delete(dap_enc_key_t *a_key)
{
    dap_enc_aes_key_delete(a_key);
}

size_t dap_enc_aes256_cbc_calc_encode_size(size_t a_size)
{
    return dap_enc_iaes256_calc_encode_size(a_size);
}

size_t dap_enc_aes256_cbc_calc_decode_size(size_t a_size)
{
    return dap_enc_iaes256_calc_decode_max_size(a_size);
}

size_t dap_enc_aes256_cbc_encrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    return dap_enc_iaes256_cbc_encrypt(a_key, a_in, a_in_size, a_out);
}

size_t dap_enc_aes256_cbc_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    return dap_enc_iaes256_cbc_decrypt(a_key, a_in, a_in_size, a_out);
}

size_t dap_enc_aes256_cbc_encrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    return dap_enc_iaes256_cbc_encrypt_fast(a_key, a_in, a_in_size, a_out, a_out_size);
}

size_t dap_enc_aes256_cbc_decrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    return dap_enc_iaes256_cbc_decrypt_fast(a_key, a_in, a_in_size, a_out, a_out_size);
}
