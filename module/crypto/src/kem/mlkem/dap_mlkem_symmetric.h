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
 */
static inline void s_keccak_absorb(uint64_t *a_state, unsigned a_rate,
                                    const uint8_t *a_data, size_t a_len,
                                    uint8_t a_suffix)
{
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
    for (size_t i = 0; i < a_nblocks; i++) {
        dap_hash_keccak_permute((dap_hash_keccak_state_t *)a_state);
        memcpy(a_out, a_state, a_rate);
        a_out += a_rate;
    }
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

    size_t l_nblocks = a_outlen / DAP_KECCAK_SHAKE256_RATE;
    if (l_nblocks)
        dap_hash_shake256_x4_squeezeblocks(a_out0, a_out1, a_out2, a_out3,
                                            l_nblocks, &l_state);
    size_t l_done = l_nblocks * DAP_KECCAK_SHAKE256_RATE;
    if (l_done < a_outlen) {
        uint8_t l_tail[4][DAP_KECCAK_SHAKE256_RATE];
        dap_hash_shake256_x4_squeezeblocks(l_tail[0], l_tail[1], l_tail[2], l_tail[3],
                                            1, &l_state);
        size_t l_rem = a_outlen - l_done;
        memcpy(a_out0 + l_done, l_tail[0], l_rem);
        memcpy(a_out1 + l_done, l_tail[1], l_rem);
        memcpy(a_out2 + l_done, l_tail[2], l_rem);
        memcpy(a_out3 + l_done, l_tail[3], l_rem);
    }
}
