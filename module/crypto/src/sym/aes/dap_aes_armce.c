/**
 * @file dap_aes_armce.c
 * @brief AES-256-CBC using ARM Crypto Extensions (ARMv8 AES instructions).
 *
 * Produces standard AES-256-CBC output, byte-compatible with the IAES
 * T-table backend, AES-NI backend, and OpenSSL / libcrypto.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#if defined(__aarch64__) || (defined(__arm__) && defined(__ARM_FEATURE_CRYPTO))

#include "dap_aes_armce.h"
#include "dap_enc_iaes.h"
#include "dap_common.h"
#include <string.h>
#include <arm_neon.h>

#ifdef __ARM_FEATURE_CRYPTO
/* ARM CE is available at compile time */
#else
#pragma GCC target("+crypto")
#endif

#define AES256_ROUNDS 14

/* struct dap_enc_aes_key_t and DAP_ENC_AES_KEY defined in dap_enc_iaes.h */

/* ========== AES-256 key expansion ========== */

static inline uint8x16_t s_sub_word_rot(uint8x16_t a_in)
{
    uint8x16_t l_zero = vdupq_n_u8(0);
    uint8x16_t l_sub = vaeseq_u8(a_in, l_zero);
    /* vaeseq_u8 does SubBytes + ShiftRows. Undo ShiftRows by rotating back. */
    static const uint8_t l_undo_sr[16] = {
        0, 13, 10, 7, 4, 1, 14, 11, 8, 5, 2, 15, 12, 9, 6, 3
    };
    return vqtbl1q_u8(l_sub, vld1q_u8(l_undo_sr));
}

static void s_aes256_expand_key(const uint8_t *a_key, uint8x16_t *a_rk)
{
    static const uint8_t s_rcon[7] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 };

    uint32x4_t k0 = vreinterpretq_u32_u8(vld1q_u8(a_key));
    uint32x4_t k1 = vreinterpretq_u32_u8(vld1q_u8(a_key + 16));
    a_rk[0] = vreinterpretq_u8_u32(k0);
    a_rk[1] = vreinterpretq_u8_u32(k1);

    for (int i = 0; i < 7; i++) {
        /* Step 1: SubWord(RotWord(k1[3])) ^ RCON */
        uint32_t l_tmp = vgetq_lane_u32(k1, 3);
        l_tmp = (l_tmp >> 8) | (l_tmp << 24);
        uint8x16_t l_sw = s_sub_word_rot(vreinterpretq_u8_u32(vdupq_n_u32(l_tmp)));
        uint32_t l_sw32 = vgetq_lane_u32(vreinterpretq_u32_u8(l_sw), 0) ^ s_rcon[i];

        uint32x4_t l_rcon_vec = vdupq_n_u32(l_sw32);
        k0 = veorq_u32(k0, vextq_u32(vdupq_n_u32(0), k0, 3));
        k0 = veorq_u32(k0, vextq_u32(vdupq_n_u32(0), k0, 3));
        k0 = veorq_u32(k0, vextq_u32(vdupq_n_u32(0), k0, 3));
        k0 = veorq_u32(k0, l_rcon_vec);
        a_rk[2 + i * 2] = vreinterpretq_u8_u32(k0);

        if (i == 6)
            break;

        /* Step 2: SubWord(k0[3]) */
        uint32_t l_k03 = vgetq_lane_u32(k0, 3);
        uint8x16_t l_sw2 = s_sub_word_rot(vreinterpretq_u8_u32(vdupq_n_u32(l_k03)));
        uint32x4_t l_sw2_vec = vdupq_n_u32(vgetq_lane_u32(vreinterpretq_u32_u8(l_sw2), 0));

        k1 = veorq_u32(k1, vextq_u32(vdupq_n_u32(0), k1, 3));
        k1 = veorq_u32(k1, vextq_u32(vdupq_n_u32(0), k1, 3));
        k1 = veorq_u32(k1, vextq_u32(vdupq_n_u32(0), k1, 3));
        k1 = veorq_u32(k1, l_sw2_vec);
        a_rk[3 + i * 2] = vreinterpretq_u8_u32(k1);
    }
}

static void s_aes256_derive_dec_keys(const uint8x16_t *a_enc, uint8x16_t *a_dec)
{
    a_dec[0] = a_enc[AES256_ROUNDS];
    for (int i = 1; i < AES256_ROUNDS; i++)
        a_dec[i] = vaesimcq_u8(a_enc[AES256_ROUNDS - i]);
    a_dec[AES256_ROUNDS] = a_enc[0];
}

/* ========== Block encrypt / decrypt ========== */

static inline uint8x16_t s_aes256_encrypt_block(uint8x16_t a_block, const uint8x16_t *a_rk)
{
    for (int i = 0; i < AES256_ROUNDS - 1; i++) {
        a_block = vaeseq_u8(a_block, a_rk[i]);
        a_block = vaesmcq_u8(a_block);
    }
    a_block = vaeseq_u8(a_block, a_rk[AES256_ROUNDS - 1]);
    return veorq_u8(a_block, a_rk[AES256_ROUNDS]);
}

static inline uint8x16_t s_aes256_decrypt_block(uint8x16_t a_block, const uint8x16_t *a_dk)
{
    for (int i = 0; i < AES256_ROUNDS - 1; i++) {
        a_block = vaesdq_u8(a_block, a_dk[i]);
        a_block = vaesimcq_u8(a_block);
    }
    a_block = vaesdq_u8(a_block, a_dk[AES256_ROUNDS - 1]);
    return veorq_u8(a_block, a_dk[AES256_ROUNDS]);
}

/* ========== Cached key schedule helpers ========== */

static inline const uint8x16_t *s_get_enc_schedule(dap_enc_aes_key_t *a_aes, const uint8_t *a_raw_key)
{
    uint8x16_t *l_sched = (uint8x16_t *)a_aes->hw_enc_schedule;
    if (!(a_aes->hw_schedule_ready & 1)) {
        s_aes256_expand_key(a_raw_key, l_sched);
        a_aes->hw_schedule_ready |= 1;
    }
    return (const uint8x16_t *)l_sched;
}

static inline const uint8x16_t *s_get_dec_schedule(dap_enc_aes_key_t *a_aes, const uint8_t *a_raw_key)
{
    const uint8x16_t *l_enc = s_get_enc_schedule(a_aes, a_raw_key);
    uint8x16_t *l_dec = (uint8x16_t *)a_aes->hw_dec_schedule;
    if (!(a_aes->hw_schedule_ready & 2)) {
        s_aes256_derive_dec_keys(l_enc, l_dec);
        a_aes->hw_schedule_ready |= 2;
    }
    return (const uint8x16_t *)l_dec;
}

/* ========== CBC encrypt / decrypt ========== */

size_t dap_aes_armce_cbc_encrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                      size_t a_in_size, void *a_out, size_t a_out_size)
{
    size_t l_enc_size = dap_enc_iaes256_calc_encode_size(a_in_size);
    if (a_out_size < l_enc_size)
        return 0;

    dap_enc_aes_key_t *l_aes = DAP_ENC_AES_KEY(a_key);
    const uint8x16_t *l_enc_keys = s_get_enc_schedule(l_aes, a_key->priv_key_data);

    uint8x16_t l_feedback = vld1q_u8(l_aes->ivec);
    const uint8_t *l_in = (const uint8_t *)a_in;
    uint8_t *l_out = (uint8_t *)a_out;
    size_t l_full_blocks = a_in_size / IAES_BLOCK_SIZE;

    for (size_t i = 0; i < l_full_blocks; i++) {
        uint8x16_t l_block = vld1q_u8(l_in + i * IAES_BLOCK_SIZE);
        l_block = veorq_u8(l_block, l_feedback);
        l_feedback = s_aes256_encrypt_block(l_block, l_enc_keys);
        vst1q_u8(l_out + i * IAES_BLOCK_SIZE, l_feedback);
    }

    uint8_t l_last_block[IAES_BLOCK_SIZE];
    size_t l_tail = a_in_size % IAES_BLOCK_SIZE;
    memcpy(l_last_block, l_in + l_full_blocks * IAES_BLOCK_SIZE, l_tail);
    int l_pad = (int)(IAES_BLOCK_SIZE - l_tail);
    for (int i = 0; i < l_pad - 1; i++)
        l_last_block[l_tail + i] = 16;
    l_last_block[IAES_BLOCK_SIZE - 1] = (uint8_t)l_pad;

    uint8x16_t l_block = vld1q_u8(l_last_block);
    l_block = veorq_u8(l_block, l_feedback);
    l_feedback = s_aes256_encrypt_block(l_block, l_enc_keys);
    vst1q_u8(l_out + l_full_blocks * IAES_BLOCK_SIZE, l_feedback);

    return l_enc_size;
}

size_t dap_aes_armce_cbc_decrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                      size_t a_in_size, void *a_out, size_t a_out_size)
{
    if (a_in_size == 0 || a_in_size % IAES_BLOCK_SIZE != 0)
        return 0;

    dap_enc_aes_key_t *l_aes = DAP_ENC_AES_KEY(a_key);
    const uint8x16_t *l_dec_keys = s_get_dec_schedule(l_aes, a_key->priv_key_data);

    uint8x16_t l_feedback = vld1q_u8(l_aes->ivec);
    const uint8_t *l_in = (const uint8_t *)a_in;
    uint8_t *l_out = (uint8_t *)a_out;
    size_t l_blocks = a_in_size / IAES_BLOCK_SIZE;

    for (size_t i = 0; i < l_blocks; i++) {
        uint8x16_t l_cipher = vld1q_u8(l_in + i * IAES_BLOCK_SIZE);
        uint8x16_t l_plain = s_aes256_decrypt_block(l_cipher, l_dec_keys);
        l_plain = veorq_u8(l_plain, l_feedback);
        l_feedback = l_cipher;
        vst1q_u8(l_out + i * IAES_BLOCK_SIZE, l_plain);
    }

    uint8_t l_padding = l_out[a_in_size - 1];
    if (l_padding == 0 || l_padding > IAES_BLOCK_SIZE)
        return a_in_size;
    return a_in_size - l_padding;
}

#endif /* ARM CE */
