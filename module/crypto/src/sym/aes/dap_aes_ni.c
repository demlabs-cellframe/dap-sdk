/**
 * @file dap_aes_ni.c
 * @brief AES-256-CBC using Intel AES-NI intrinsics.
 *
 * Produces standard AES-256-CBC output, byte-compatible with the IAES
 * T-table backend and with OpenSSL / libcrypto.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

#include "dap_aes_ni.h"
#include "dap_enc_iaes.h"
#include "dap_common.h"
#include <string.h>
#include <wmmintrin.h>

#define AES256_ROUNDS 14

/* struct dap_enc_aes_key_t and DAP_ENC_AES_KEY defined in dap_enc_iaes.h */

/* ========== AES-256 key expansion (encrypt direction) ========== */

static inline __m128i s_key_exp_step1(__m128i key, __m128i keygen)
{
    keygen = _mm_shuffle_epi32(keygen, 0xFF);
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygen);
}

static inline __m128i s_key_exp_step2(__m128i key1, __m128i key2)
{
    __m128i keygen = _mm_aeskeygenassist_si128(key1, 0);
    keygen = _mm_shuffle_epi32(keygen, 0xAA);
    key2 = _mm_xor_si128(key2, _mm_slli_si128(key2, 4));
    key2 = _mm_xor_si128(key2, _mm_slli_si128(key2, 4));
    key2 = _mm_xor_si128(key2, _mm_slli_si128(key2, 4));
    return _mm_xor_si128(key2, keygen);
}

static void s_aes256_expand_key(const uint8_t *a_key, __m128i *a_enc_keys)
{
    __m128i k0 = _mm_loadu_si128((const __m128i *)a_key);
    __m128i k1 = _mm_loadu_si128((const __m128i *)(a_key + 16));
    a_enc_keys[0] = k0;
    a_enc_keys[1] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x01));
    a_enc_keys[2] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[3] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x02));
    a_enc_keys[4] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[5] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x04));
    a_enc_keys[6] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[7] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x08));
    a_enc_keys[8] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[9] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x10));
    a_enc_keys[10] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[11] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x20));
    a_enc_keys[12] = k0;
    k1 = s_key_exp_step2(k0, k1);
    a_enc_keys[13] = k1;

    k0 = s_key_exp_step1(k0, _mm_aeskeygenassist_si128(k1, 0x40));
    a_enc_keys[14] = k0;
}

static void s_aes256_derive_dec_keys(const __m128i *a_enc_keys, __m128i *a_dec_keys)
{
    a_dec_keys[0] = a_enc_keys[AES256_ROUNDS];
    for (int i = 1; i < AES256_ROUNDS; i++)
        a_dec_keys[i] = _mm_aesimc_si128(a_enc_keys[AES256_ROUNDS - i]);
    a_dec_keys[AES256_ROUNDS] = a_enc_keys[0];
}

/* ========== CBC encrypt / decrypt ========== */

static inline __m128i s_aes256_encrypt_block(__m128i a_block, const __m128i *a_rk)
{
    a_block = _mm_xor_si128(a_block, a_rk[0]);
    a_block = _mm_aesenc_si128(a_block, a_rk[1]);
    a_block = _mm_aesenc_si128(a_block, a_rk[2]);
    a_block = _mm_aesenc_si128(a_block, a_rk[3]);
    a_block = _mm_aesenc_si128(a_block, a_rk[4]);
    a_block = _mm_aesenc_si128(a_block, a_rk[5]);
    a_block = _mm_aesenc_si128(a_block, a_rk[6]);
    a_block = _mm_aesenc_si128(a_block, a_rk[7]);
    a_block = _mm_aesenc_si128(a_block, a_rk[8]);
    a_block = _mm_aesenc_si128(a_block, a_rk[9]);
    a_block = _mm_aesenc_si128(a_block, a_rk[10]);
    a_block = _mm_aesenc_si128(a_block, a_rk[11]);
    a_block = _mm_aesenc_si128(a_block, a_rk[12]);
    a_block = _mm_aesenc_si128(a_block, a_rk[13]);
    return _mm_aesenclast_si128(a_block, a_rk[14]);
}

static inline __m128i s_aes256_decrypt_block(__m128i a_block, const __m128i *a_dk)
{
    a_block = _mm_xor_si128(a_block, a_dk[0]);
    a_block = _mm_aesdec_si128(a_block, a_dk[1]);
    a_block = _mm_aesdec_si128(a_block, a_dk[2]);
    a_block = _mm_aesdec_si128(a_block, a_dk[3]);
    a_block = _mm_aesdec_si128(a_block, a_dk[4]);
    a_block = _mm_aesdec_si128(a_block, a_dk[5]);
    a_block = _mm_aesdec_si128(a_block, a_dk[6]);
    a_block = _mm_aesdec_si128(a_block, a_dk[7]);
    a_block = _mm_aesdec_si128(a_block, a_dk[8]);
    a_block = _mm_aesdec_si128(a_block, a_dk[9]);
    a_block = _mm_aesdec_si128(a_block, a_dk[10]);
    a_block = _mm_aesdec_si128(a_block, a_dk[11]);
    a_block = _mm_aesdec_si128(a_block, a_dk[12]);
    a_block = _mm_aesdec_si128(a_block, a_dk[13]);
    return _mm_aesdeclast_si128(a_block, a_dk[14]);
}

/* ========== Cached key schedule helpers ========== */

static inline const __m128i *s_get_enc_schedule(dap_enc_aes_key_t *a_aes, const uint8_t *a_raw_key)
{
    __m128i *l_sched = (__m128i *)a_aes->hw_enc_schedule;
    if (!(a_aes->hw_schedule_ready & 1)) {
        s_aes256_expand_key(a_raw_key, l_sched);
        a_aes->hw_schedule_ready |= 1;
    }
    return (const __m128i *)l_sched;
}

static inline const __m128i *s_get_dec_schedule(dap_enc_aes_key_t *a_aes, const uint8_t *a_raw_key)
{
    const __m128i *l_enc = s_get_enc_schedule(a_aes, a_raw_key);
    __m128i *l_dec = (__m128i *)a_aes->hw_dec_schedule;
    if (!(a_aes->hw_schedule_ready & 2)) {
        s_aes256_derive_dec_keys(l_enc, l_dec);
        a_aes->hw_schedule_ready |= 2;
    }
    return (const __m128i *)l_dec;
}

/* ========== Public fast functions (same signature as IAES) ========== */

__attribute__((target("aes,sse2")))
size_t dap_aes_ni_cbc_encrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                   size_t a_in_size, void *a_out, size_t a_out_size)
{
    size_t l_enc_size = dap_enc_iaes256_calc_encode_size(a_in_size);
    if (a_out_size < l_enc_size)
        return 0;

    dap_enc_aes_key_t *l_aes = DAP_ENC_AES_KEY(a_key);
    const __m128i *l_enc_keys = s_get_enc_schedule(l_aes, a_key->priv_key_data);

    __m128i l_feedback = _mm_loadu_si128((const __m128i *)l_aes->ivec);

    const uint8_t *l_in = (const uint8_t *)a_in;
    uint8_t *l_out = (uint8_t *)a_out;
    size_t l_full_blocks = a_in_size / IAES_BLOCK_SIZE;

    for (size_t i = 0; i < l_full_blocks; i++) {
        __m128i l_block = _mm_loadu_si128((const __m128i *)(l_in + i * IAES_BLOCK_SIZE));
        l_block = _mm_xor_si128(l_block, l_feedback);
        l_feedback = s_aes256_encrypt_block(l_block, l_enc_keys);
        _mm_storeu_si128((__m128i *)(l_out + i * IAES_BLOCK_SIZE), l_feedback);
    }

    uint8_t l_last_block[IAES_BLOCK_SIZE];
    size_t l_tail = a_in_size % IAES_BLOCK_SIZE;
    memcpy(l_last_block, l_in + l_full_blocks * IAES_BLOCK_SIZE, l_tail);
    int l_pad = (int)(IAES_BLOCK_SIZE - l_tail);
    for (int i = 0; i < l_pad - 1; i++)
        l_last_block[l_tail + i] = 16;
    l_last_block[IAES_BLOCK_SIZE - 1] = (uint8_t)l_pad;

    __m128i l_block = _mm_loadu_si128((const __m128i *)l_last_block);
    l_block = _mm_xor_si128(l_block, l_feedback);
    l_feedback = s_aes256_encrypt_block(l_block, l_enc_keys);
    _mm_storeu_si128((__m128i *)(l_out + l_full_blocks * IAES_BLOCK_SIZE), l_feedback);

    return l_enc_size;
}

__attribute__((target("aes,sse2")))
size_t dap_aes_ni_cbc_decrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                   size_t a_in_size, void *a_out, size_t a_out_size)
{
    if (a_in_size == 0 || a_in_size % IAES_BLOCK_SIZE != 0)
        return 0;

    dap_enc_aes_key_t *l_aes = DAP_ENC_AES_KEY(a_key);
    const __m128i *l_dec_keys = s_get_dec_schedule(l_aes, a_key->priv_key_data);

    __m128i l_feedback = _mm_loadu_si128((const __m128i *)l_aes->ivec);

    const uint8_t *l_in = (const uint8_t *)a_in;
    uint8_t *l_out = (uint8_t *)a_out;
    size_t l_blocks = a_in_size / IAES_BLOCK_SIZE;

    for (size_t i = 0; i < l_blocks; i++) {
        __m128i l_cipher = _mm_loadu_si128((const __m128i *)(l_in + i * IAES_BLOCK_SIZE));
        __m128i l_plain = s_aes256_decrypt_block(l_cipher, l_dec_keys);
        l_plain = _mm_xor_si128(l_plain, l_feedback);
        l_feedback = l_cipher;
        _mm_storeu_si128((__m128i *)(l_out + i * IAES_BLOCK_SIZE), l_plain);
    }

    uint8_t l_padding = l_out[a_in_size - 1];
    if (l_padding == 0 || l_padding > IAES_BLOCK_SIZE)
        return a_in_size;
    return a_in_size - l_padding;
}

#endif /* x86 */
