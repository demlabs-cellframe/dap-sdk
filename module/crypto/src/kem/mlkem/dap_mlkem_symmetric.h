/**
 * @file dap_mlkem_symmetric.h
 * @brief Specialized SHAKE/SHA3 for ML-KEM — bypasses sponge API for speed.
 *
 * All ML-KEM hash inputs fit in one rate block, so we absorb + pad + permute
 * in a single pass with word-at-a-time XORs, avoiding the generic sponge
 * machinery (memset/memcpy/loop overhead).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dap_mlkem_params.h"
#include "dap_hash_keccak.h"
#include "dap_hash_shake_x4.h"

typedef struct { uint64_t s[25]; } dap_mlkem_xof_state;

#define MLKEM_XOF_BLOCKBYTES DAP_KECCAK_SHAKE128_RATE

/**
 * Absorb + pad + permute in one go.  Handles arbitrary input length.
 * Full blocks are XOR'd word-at-a-time; the last partial block gets
 * the domain suffix and pad10*1 appended before the final permute.
 *
 * When AVX-512VL is available, dispatches to register-resident assembly
 * that keeps state in xmm0-24 across all blocks (no intermediate load/store).
 */

#if defined(__x86_64__) || defined(_M_X64)
#define DAP_KECCAK_HAS_SPONGE_ASM 1
#endif

static inline void s_keccak_absorb(uint64_t *a_state, unsigned a_rate,
                                    const uint8_t *a_data, size_t a_len,
                                    uint8_t a_suffix)
{
#if DAP_KECCAK_HAS_SPONGE_ASM
    static int s_avx512 = -1;
    if (__builtin_expect(s_avx512 < 0, 0))
        s_avx512 = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512);
    if (__builtin_expect(s_avx512, 1)) {
        switch (a_rate) {
        case 136: dap_keccak_absorb_136_avx512vl_asm(a_state, a_data, a_len, a_suffix); return;
        case 168: dap_keccak_absorb_168_avx512vl_asm(a_state, a_data, a_len, a_suffix); return;
        case 72:  dap_keccak_absorb_72_avx512vl_asm(a_state, a_data, a_len, a_suffix); return;
        }
    }
#endif
    memset(a_state, 0, 200);
    unsigned rate_words = a_rate / 8;

    while (a_len >= (size_t)a_rate) {
        for (unsigned j = 0; j < rate_words; j++) {
            uint64_t w;
            memcpy(&w, a_data + j * 8, 8);
            a_state[j] ^= w;
        }
        dap_hash_keccak_permute((dap_hash_keccak_state_t *)a_state);
        a_data += a_rate;
        a_len  -= a_rate;
    }

    size_t i = 0;
    for (; i + 8 <= a_len; i += 8) {
        uint64_t w;
        memcpy(&w, a_data + i, 8);
        a_state[i / 8] ^= w;
    }
    {
        uint64_t tail = 0;
        size_t rem = a_len - i;
        if (rem)
            memcpy(&tail, a_data + i, rem);
        tail |= (uint64_t)a_suffix << (rem * 8);
        a_state[i / 8] ^= tail;
    }
    a_state[(a_rate - 1) / 8] ^= (uint64_t)0x80 << (((a_rate - 1) % 8) * 8);
    dap_hash_keccak_permute((dap_hash_keccak_state_t *)a_state);
}

static inline void s_keccak_squeezeblocks(uint8_t *a_out, size_t a_nblocks,
                                           uint64_t *a_state, unsigned a_rate)
{
#if DAP_KECCAK_HAS_SPONGE_ASM
    static int s_avx512_sq = -1;
    if (__builtin_expect(s_avx512_sq < 0, 0))
        s_avx512_sq = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512);
    if (__builtin_expect(s_avx512_sq, 1)) {
        switch (a_rate) {
        case 136: dap_keccak_squeeze_136_avx512vl_asm(a_state, a_out, a_nblocks); return;
        case 168: dap_keccak_squeeze_168_avx512vl_asm(a_state, a_out, a_nblocks); return;
        case 72:  dap_keccak_squeeze_72_avx512vl_asm(a_state, a_out, a_nblocks); return;
        }
    }
#endif
    for (size_t i = 0; i < a_nblocks; i++) {
        dap_hash_keccak_permute((dap_hash_keccak_state_t *)a_state);
        memcpy(a_out, a_state, a_rate);
        a_out += a_rate;
    }
}

static inline void s_keccak_absorb_squeeze(uint8_t *a_out, size_t a_nblocks,
                                            uint64_t *a_state, unsigned a_rate,
                                            const uint8_t *a_data, size_t a_len,
                                            uint8_t a_suffix)
{
    s_keccak_absorb(a_state, a_rate, a_data, a_len, a_suffix);
    if (a_nblocks > 0) {
        memcpy(a_out, a_state, a_rate);
        a_out += a_rate;
        a_nblocks--;
    }
    s_keccak_squeezeblocks(a_out, a_nblocks, a_state, a_rate);
}

/* ---------- SHA3-256 (hash_h): rate=136, suffix=0x06, output=32 ---------- */
static inline void dap_mlkem_hash_h(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    uint64_t l_st[25];
    s_keccak_absorb(l_st, DAP_KECCAK_SHA3_256_RATE, a_in, a_len,
                    DAP_KECCAK_SHA3_SUFFIX);
    memcpy(a_out, l_st, 32);
}

/* ---------- SHA3-512 (hash_g): rate=72, suffix=0x06, output=64 ----------- */
static inline void dap_mlkem_hash_g(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    uint64_t l_st[25];
    s_keccak_absorb(l_st, DAP_KECCAK_SHA3_512_RATE, a_in, a_len,
                    DAP_KECCAK_SHA3_SUFFIX);
    memcpy(a_out, l_st, 64);
}

/* ---------- XOF absorb (SHAKE128): rate=168, input=34 bytes -------------- */
static inline void dap_mlkem_xof_absorb(dap_mlkem_xof_state *a_state,
                                         const uint8_t a_seed[MLKEM_SYMBYTES],
                                         uint8_t a_x, uint8_t a_y)
{
    uint8_t l_extseed[MLKEM_SYMBYTES + 2];
    memcpy(l_extseed, a_seed, MLKEM_SYMBYTES);
    l_extseed[MLKEM_SYMBYTES]     = a_x;
    l_extseed[MLKEM_SYMBYTES + 1] = a_y;
    s_keccak_absorb(a_state->s, DAP_KECCAK_SHAKE128_RATE,
                    l_extseed, sizeof(l_extseed), DAP_KECCAK_SHAKE_SUFFIX);
}

/* ---------- XOF squeeze (SHAKE128): permute + memcpy per block ----------- */
static inline void dap_mlkem_xof_squeezeblocks(uint8_t *a_out, size_t a_nblocks,
                                                 dap_mlkem_xof_state *a_state)
{
    s_keccak_squeezeblocks(a_out, a_nblocks, a_state->s, DAP_KECCAK_SHAKE128_RATE);
}

/* ---------- XOF absorb + squeeze first block free ----------------------- */
static inline void dap_mlkem_xof_absorb_squeeze(dap_mlkem_xof_state *a_state,
                                                  uint8_t *a_out, size_t a_nblocks,
                                                  const uint8_t a_seed[MLKEM_SYMBYTES],
                                                  uint8_t a_x, uint8_t a_y)
{
    uint8_t l_extseed[MLKEM_SYMBYTES + 2];
    memcpy(l_extseed, a_seed, MLKEM_SYMBYTES);
    l_extseed[MLKEM_SYMBYTES]     = a_x;
    l_extseed[MLKEM_SYMBYTES + 1] = a_y;
    s_keccak_absorb_squeeze(a_out, a_nblocks, a_state->s, DAP_KECCAK_SHAKE128_RATE,
                            l_extseed, sizeof(l_extseed), DAP_KECCAK_SHAKE_SUFFIX);
}

/* ---------- PRF (SHAKE256): rate=136, input=33 bytes --------------------- */
static inline void dap_mlkem_prf(uint8_t *a_out, size_t a_outlen,
                                  const uint8_t a_key[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint64_t l_st[25];
    uint8_t l_extkey[MLKEM_SYMBYTES + 1];
    memcpy(l_extkey, a_key, MLKEM_SYMBYTES);
    l_extkey[MLKEM_SYMBYTES] = a_nonce;
    s_keccak_absorb(l_st, DAP_KECCAK_SHAKE256_RATE,
                    l_extkey, sizeof(l_extkey), DAP_KECCAK_SHAKE_SUFFIX);
    size_t pos = 0;
    unsigned rate = DAP_KECCAK_SHAKE256_RATE;
    size_t to_copy = (a_outlen < rate) ? a_outlen : rate;
    memcpy(a_out, l_st, to_copy);
    pos += to_copy;
    while (pos < a_outlen) {
        dap_hash_keccak_permute((dap_hash_keccak_state_t *)l_st);
        to_copy = (a_outlen - pos < rate) ? a_outlen - pos : rate;
        memcpy(a_out + pos, l_st, to_copy);
        pos += to_copy;
    }
}

/* ---------- KDF (SHAKE256): rate=136, output=32 bytes -------------------- */
static inline void dap_mlkem_kdf(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    uint64_t l_st[25];
    s_keccak_absorb(l_st, DAP_KECCAK_SHAKE256_RATE, a_in, a_len,
                    DAP_KECCAK_SHAKE_SUFFIX);
    memcpy(a_out, l_st, MLKEM_SSBYTES);
}

/* ---- x4 parallel wrappers ---- */

static inline void dap_mlkem_xof_absorb_x4(dap_keccak_x4_state_t *a_state,
                                             const uint8_t a_seed[MLKEM_SYMBYTES],
                                             uint8_t a_x0, uint8_t a_y0,
                                             uint8_t a_x1, uint8_t a_y1,
                                             uint8_t a_x2, uint8_t a_y2,
                                             uint8_t a_x3, uint8_t a_y3)
{
    uint8_t l_ext[4][MLKEM_SYMBYTES + 2];
    for (int k = 0; k < 4; k++)
        memcpy(l_ext[k], a_seed, MLKEM_SYMBYTES);
    l_ext[0][MLKEM_SYMBYTES] = a_x0; l_ext[0][MLKEM_SYMBYTES + 1] = a_y0;
    l_ext[1][MLKEM_SYMBYTES] = a_x1; l_ext[1][MLKEM_SYMBYTES + 1] = a_y1;
    l_ext[2][MLKEM_SYMBYTES] = a_x2; l_ext[2][MLKEM_SYMBYTES + 1] = a_y2;
    l_ext[3][MLKEM_SYMBYTES] = a_x3; l_ext[3][MLKEM_SYMBYTES + 1] = a_y3;
    dap_hash_shake128_x4_absorb(a_state, l_ext[0], l_ext[1], l_ext[2], l_ext[3],
                                 MLKEM_SYMBYTES + 2);
}

static inline void dap_mlkem_xof_squeezeblocks_x4(uint8_t *a_out0, uint8_t *a_out1,
                                                     uint8_t *a_out2, uint8_t *a_out3,
                                                     size_t a_nblocks,
                                                     dap_keccak_x4_state_t *a_state)
{
    dap_hash_shake128_x4_squeezeblocks(a_out0, a_out1, a_out2, a_out3,
                                        a_nblocks, a_state);
}

static inline void dap_mlkem_xof_absorb_squeeze_x4(
    dap_keccak_x4_state_t *a_state,
    uint8_t *a_out0, uint8_t *a_out1, uint8_t *a_out2, uint8_t *a_out3,
    size_t a_nblocks,
    const uint8_t a_seed[MLKEM_SYMBYTES],
    uint8_t a_x0, uint8_t a_y0, uint8_t a_x1, uint8_t a_y1,
    uint8_t a_x2, uint8_t a_y2, uint8_t a_x3, uint8_t a_y3)
{
    dap_mlkem_xof_absorb_x4(a_state, a_seed, a_x0, a_y0, a_x1, a_y1,
                              a_x2, a_y2, a_x3, a_y3);
    if (a_nblocks > 0) {
        dap_keccak_x4_extract_bytes_all(a_state, a_out0, a_out1, a_out2, a_out3,
                                         DAP_KECCAK_SHAKE128_RATE);
        a_out0 += DAP_KECCAK_SHAKE128_RATE;
        a_out1 += DAP_KECCAK_SHAKE128_RATE;
        a_out2 += DAP_KECCAK_SHAKE128_RATE;
        a_out3 += DAP_KECCAK_SHAKE128_RATE;
        a_nblocks--;
    }
    if (a_nblocks > 0)
        dap_mlkem_xof_squeezeblocks_x4(a_out0, a_out1, a_out2, a_out3,
                                         a_nblocks, a_state);
}

static inline void dap_mlkem_prf_x4(uint8_t *a_out0, uint8_t *a_out1,
                                      uint8_t *a_out2, uint8_t *a_out3,
                                      size_t a_outlen,
                                      const uint8_t a_key[MLKEM_SYMBYTES],
                                      uint8_t a_n0, uint8_t a_n1,
                                      uint8_t a_n2, uint8_t a_n3)
{
    uint8_t l_ext[4][MLKEM_SYMBYTES + 1];
    for (int k = 0; k < 4; k++)
        memcpy(l_ext[k], a_key, MLKEM_SYMBYTES);
    l_ext[0][MLKEM_SYMBYTES] = a_n0;
    l_ext[1][MLKEM_SYMBYTES] = a_n1;
    l_ext[2][MLKEM_SYMBYTES] = a_n2;
    l_ext[3][MLKEM_SYMBYTES] = a_n3;

    dap_keccak_x4_state_t l_state;
    dap_hash_shake256_x4_absorb(&l_state, l_ext[0], l_ext[1], l_ext[2], l_ext[3],
                                 MLKEM_SYMBYTES + 1);

    unsigned l_rate = DAP_KECCAK_SHAKE256_RATE;
    size_t l_pos = 0;
    size_t l_first = (a_outlen < l_rate) ? a_outlen : l_rate;
    if (l_first <= l_rate) {
        uint8_t l_tmp[4][DAP_KECCAK_SHAKE256_RATE];
        dap_keccak_x4_extract_bytes_all(&l_state, l_tmp[0], l_tmp[1], l_tmp[2], l_tmp[3], l_rate);
        memcpy(a_out0, l_tmp[0], l_first);
        memcpy(a_out1, l_tmp[1], l_first);
        memcpy(a_out2, l_tmp[2], l_first);
        memcpy(a_out3, l_tmp[3], l_first);
        l_pos = l_first;
    }
    while (l_pos < a_outlen) {
        dap_keccak_x4_permute(&l_state);
        size_t l_rem = a_outlen - l_pos;
        size_t l_copy = (l_rem < l_rate) ? l_rem : l_rate;
        if (l_copy == l_rate) {
            dap_keccak_x4_extract_bytes_all(&l_state, a_out0 + l_pos, a_out1 + l_pos,
                                             a_out2 + l_pos, a_out3 + l_pos, l_rate);
        } else {
            uint8_t l_tmp[4][DAP_KECCAK_SHAKE256_RATE];
            dap_keccak_x4_extract_bytes_all(&l_state, l_tmp[0], l_tmp[1], l_tmp[2], l_tmp[3], l_rate);
            memcpy(a_out0 + l_pos, l_tmp[0], l_copy);
            memcpy(a_out1 + l_pos, l_tmp[1], l_copy);
            memcpy(a_out2 + l_pos, l_tmp[2], l_copy);
            memcpy(a_out3 + l_pos, l_tmp[3], l_copy);
        }
        l_pos += l_copy;
    }
}
