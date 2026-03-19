/**
 * @file dap_mlkem_symmetric.h
 * @brief SHAKE/SHA3 wrappers for ML-KEM via DAP hash API.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dap_mlkem_params.h"
#include "dap_hash_keccak.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

typedef struct { uint64_t s[25]; } dap_mlkem_xof_state;

#define MLKEM_XOF_BLOCKBYTES DAP_SHAKE128_RATE

static inline void dap_mlkem_hash_h(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    dap_hash_sha3_256_raw(a_out, a_in, a_len);
}

static inline void dap_mlkem_hash_g(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    dap_hash_sha3_512(a_out, a_in, a_len);
}

static inline void dap_mlkem_xof_absorb(dap_mlkem_xof_state *a_state,
                                         const uint8_t a_seed[MLKEM_SYMBYTES],
                                         uint8_t a_x, uint8_t a_y)
{
    uint8_t l_extseed[MLKEM_SYMBYTES + 2];
    memcpy(l_extseed, a_seed, MLKEM_SYMBYTES);
    l_extseed[MLKEM_SYMBYTES] = a_x;
    l_extseed[MLKEM_SYMBYTES + 1] = a_y;
    memset(a_state->s, 0, sizeof(a_state->s));
    dap_hash_shake128_absorb(a_state->s, l_extseed, sizeof(l_extseed));
}

static inline void dap_mlkem_xof_squeezeblocks(uint8_t *a_out, size_t a_nblocks,
                                                 dap_mlkem_xof_state *a_state)
{
    dap_hash_shake128_squeezeblocks(a_out, a_nblocks, a_state->s);
}

static inline void dap_mlkem_prf(uint8_t *a_out, size_t a_outlen,
                                  const uint8_t a_key[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint8_t l_extkey[MLKEM_SYMBYTES + 1];
    memcpy(l_extkey, a_key, MLKEM_SYMBYTES);
    l_extkey[MLKEM_SYMBYTES] = a_nonce;
    dap_hash_shake256(a_out, a_outlen, l_extkey, sizeof(l_extkey));
}

static inline void dap_mlkem_kdf(uint8_t *a_out, const uint8_t *a_in, size_t a_len)
{
    dap_hash_shake256(a_out, MLKEM_SSBYTES, a_in, a_len);
}
