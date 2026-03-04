/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_hash_shake256.h
 * @brief SHAKE256 extendable output function (XOF)
 * @details Native DAP implementation based on Keccak-p[1600].
 *          SHAKE256 provides 256-bit security level.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_keccak.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants
// =============================================================================

#define DAP_SHAKE256_RATE           136   // (1600 - 2*256) / 8

// =============================================================================
// One-shot API
// =============================================================================

/**
 * @brief SHAKE256 extendable output function
 * @param a_output Output buffer
 * @param a_outlen Desired output length in bytes
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_shake256(uint8_t *a_output, size_t a_outlen,
                                     const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHAKE256_RATE, DAP_KECCAK_SHAKE_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_squeeze(&l_ctx, a_output, a_outlen);
}

// =============================================================================
// Streaming API
// =============================================================================

/**
 * @brief Initialize SHAKE256 absorb phase
 * @param a_state Keccak state (25 uint64_t)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_shake256_absorb(uint64_t *a_state, const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHAKE256_RATE, DAP_KECCAK_SHAKE_SUFFIX);
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    // Apply padding WITHOUT permuting: squeezeblocks permutes before each extraction,
    // matching the original fips202.c keccak_absorb() contract
    uint8_t *l_state_bytes = (uint8_t *)l_ctx.state.lanes;
    l_state_bytes[l_ctx.block_idx] ^= l_ctx.suffix;
    l_state_bytes[l_ctx.rate - 1] ^= 0x80;
    memcpy(a_state, l_ctx.state.lanes, DAP_KECCAK_STATE_BYTES);
}

/**
 * @brief Squeeze blocks from SHAKE256
 * @param a_output Output buffer
 * @param a_nblocks Number of rate-sized blocks to squeeze
 * @param a_state Keccak state
 */
DAP_STATIC_INLINE void dap_hash_shake256_squeezeblocks(uint8_t *a_output, size_t a_nblocks, uint64_t *a_state)
{
    dap_hash_keccak_state_t *l_state = (dap_hash_keccak_state_t *)a_state;
    for (size_t i = 0; i < a_nblocks; i++) {
        dap_hash_keccak_permute(l_state);
        dap_hash_keccak_extract_bytes(l_state, a_output, DAP_KECCAK_SHAKE256_RATE);
        a_output += DAP_KECCAK_SHAKE256_RATE;
    }
}

// =============================================================================
// cSHAKE256 (Customizable SHAKE - NIST SP 800-185)
// =============================================================================

/**
 * @brief cSHAKE256 with simple customization (16-bit custom string)
 * @param a_output Output buffer
 * @param a_outlen Desired output length
 * @param a_cstm 16-bit customization value
 * @param a_input Input data
 * @param a_inlen Input length
 */
void dap_hash_cshake256_simple(uint8_t *a_output, size_t a_outlen, uint16_t a_cstm,
                          const uint8_t *a_input, size_t a_inlen);

/**
 * @brief cSHAKE256 absorb with simple customization
 */
void dap_hash_cshake256_simple_absorb(uint64_t a_state[25], uint16_t a_cstm,
                                  const uint8_t *a_input, size_t a_inlen);

/**
 * @brief cSHAKE256 squeeze blocks
 */
DAP_STATIC_INLINE void dap_hash_cshake256_simple_squeezeblocks(uint8_t *a_output, size_t a_nblocks, uint64_t a_state[25])
{
    dap_hash_shake256_squeezeblocks(a_output, a_nblocks, a_state);
}

#ifdef __cplusplus
}
#endif
