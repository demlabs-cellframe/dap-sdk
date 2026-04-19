/**
 * @file dap_enc_aes.c
 * @brief Unified AES-256-CBC with runtime dispatch: AES-NI > ARM CE > IAES T-table.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_enc_aes.h"
#include "dap_enc_iaes.h"
#include "dap_arch_dispatch.h"
#include "dap_cpu_detect.h"
#include <stddef.h>

#if DAP_PLATFORM_X86
#include "dap_aes_ni.h"
#endif
#if DAP_PLATFORM_ARM
#include "dap_aes_armce.h"
#endif

/* ========== Function pointer dispatch ========== */

DAP_DISPATCH_DECLARE_RESOLVE(dap_enc_aes256_cbc_encrypt_fast, size_t,
        struct dap_enc_key *, const void *, size_t, void *, size_t);
DAP_DISPATCH_DECLARE_RESOLVE(dap_enc_aes256_cbc_decrypt_fast, size_t,
        struct dap_enc_key *, const void *, size_t, void *, size_t);

static inline dap_enc_aes256_cbc_encrypt_fast_fn_t
 dap_enc_aes256_cbc_encrypt_fast_resolve(void)
{
    dap_cpu_features_t l_cpu = dap_cpu_detect_features();
#if DAP_PLATFORM_X86
    if (l_cpu.has_aes_ni) {
        return dap_aes_ni_cbc_encrypt_fast;
    }
#endif
#if DAP_PLATFORM_ARM
    if (l_cpu.has_arm_ce) {
        return dap_aes_armce_cbc_encrypt_fast;
    }
#endif
    return dap_enc_iaes256_cbc_encrypt_fast;
}

static inline dap_enc_aes256_cbc_decrypt_fast_fn_t
 dap_enc_aes256_cbc_decrypt_fast_resolve(void)
{
    dap_cpu_features_t l_cpu = dap_cpu_detect_features();
#if DAP_PLATFORM_X86
    if (l_cpu.has_aes_ni) {
        return dap_aes_ni_cbc_decrypt_fast;
    }
#endif
#if DAP_PLATFORM_ARM
    if (l_cpu.has_arm_ce) {
        return dap_aes_armce_cbc_decrypt_fast;
    }
#endif
    return dap_enc_iaes256_cbc_decrypt_fast;
}

/* ========== Public API ========== */

void dap_enc_aes256_key_new(dap_enc_key_t *a_key)
{
    dap_enc_aes_key_new(a_key);
    a_key->type = DAP_ENC_KEY_TYPE_AES256_CBC;
    a_key->enc = (dap_enc_callback_dataop_t)dap_enc_aes256_cbc_encrypt;
    a_key->dec = (dap_enc_callback_dataop_t)dap_enc_aes256_cbc_decrypt;
    a_key->enc_na = (dap_enc_callback_dataop_na_t)dap_enc_aes256_cbc_encrypt_fast;
    a_key->dec_na = (dap_enc_callback_dataop_na_t)dap_enc_aes256_cbc_decrypt_fast;
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
    size_t l_out_size = dap_enc_iaes256_calc_encode_size(a_in_size);
    *a_out = DAP_NEW_SIZE(uint8_t, l_out_size);
    if (!*a_out)
        return 0;
    return DAP_DISPATCH_INLINE_CALL_RET(dap_enc_aes256_cbc_encrypt_fast, a_key, a_in, a_in_size, *a_out, l_out_size);
}

size_t dap_enc_aes256_cbc_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    size_t l_out_size = dap_enc_iaes256_calc_decode_max_size(a_in_size);
    *a_out = DAP_NEW_SIZE(uint8_t, l_out_size);
    if (!*a_out)
        return 0;
    return DAP_DISPATCH_INLINE_CALL_RET(dap_enc_aes256_cbc_decrypt_fast, a_key, a_in, a_in_size, *a_out, l_out_size);
}

size_t dap_enc_aes256_cbc_encrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    return DAP_DISPATCH_INLINE_CALL_RET(dap_enc_aes256_cbc_encrypt_fast, a_key, a_in, a_in_size, a_out, a_out_size);
}

size_t dap_enc_aes256_cbc_decrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    return DAP_DISPATCH_INLINE_CALL_RET(dap_enc_aes256_cbc_decrypt_fast, a_key, a_in, a_in_size, a_out, a_out_size);
}
