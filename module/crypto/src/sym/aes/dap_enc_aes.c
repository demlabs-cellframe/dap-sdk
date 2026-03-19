/**
 * @file dap_enc_aes.c
 * @brief Unified AES-256-CBC with runtime dispatch: AES-NI > ARM CE > IAES T-table.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_enc_aes.h"
#include "dap_enc_iaes.h"
#include "dap_cpu_detect.h"
#include <stddef.h>
#include <pthread.h>

/* Backend headers (guarded by platform ifdefs internally) */
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include "dap_aes_ni.h"
#endif
#if defined(__aarch64__) || (defined(__arm__) && defined(__ARM_FEATURE_CRYPTO))
#include "dap_aes_armce.h"
#endif

/* ========== Function pointer dispatch ========== */

typedef size_t (*dap_aes_cbc_fast_fn_t)(struct dap_enc_key *, const void *, size_t, void *, size_t);

static dap_aes_cbc_fast_fn_t s_encrypt_fast = NULL;
static dap_aes_cbc_fast_fn_t s_decrypt_fast = NULL;
static pthread_once_t s_aes_once = PTHREAD_ONCE_INIT;

static void s_aes_dispatch_init(void)
{
    s_encrypt_fast = dap_enc_iaes256_cbc_encrypt_fast;
    s_decrypt_fast = dap_enc_iaes256_cbc_decrypt_fast;

    dap_cpu_features_t l_cpu = dap_cpu_detect_features();
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    if (l_cpu.has_aes_ni) {
        s_encrypt_fast = dap_aes_ni_cbc_encrypt_fast;
        s_decrypt_fast = dap_aes_ni_cbc_decrypt_fast;
    }
#endif
#if defined(__aarch64__) || (defined(__arm__) && defined(__ARM_FEATURE_CRYPTO))
    if (l_cpu.has_arm_ce) {
        s_encrypt_fast = dap_aes_armce_cbc_encrypt_fast;
        s_decrypt_fast = dap_aes_armce_cbc_decrypt_fast;
    }
#endif
    (void)l_cpu;
}

static inline void s_ensure_init(void)
{
    pthread_once(&s_aes_once, s_aes_dispatch_init);
}

/* ========== Public API ========== */

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
    s_ensure_init();
    size_t l_out_size = dap_enc_iaes256_calc_encode_size(a_in_size);
    *a_out = DAP_NEW_SIZE(uint8_t, l_out_size);
    if (!*a_out)
        return 0;
    return s_encrypt_fast(a_key, a_in, a_in_size, *a_out, l_out_size);
}

size_t dap_enc_aes256_cbc_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    s_ensure_init();
    size_t l_out_size = dap_enc_iaes256_calc_decode_max_size(a_in_size);
    *a_out = DAP_NEW_SIZE(uint8_t, l_out_size);
    if (!*a_out)
        return 0;
    return s_decrypt_fast(a_key, a_in, a_in_size, *a_out, l_out_size);
}

size_t dap_enc_aes256_cbc_encrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    s_ensure_init();
    return s_encrypt_fast(a_key, a_in, a_in_size, a_out, a_out_size);
}

size_t dap_enc_aes256_cbc_decrypt_fast(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void *a_out, size_t a_out_size)
{
    s_ensure_init();
    return s_decrypt_fast(a_key, a_in, a_in_size, a_out, a_out_size);
}
