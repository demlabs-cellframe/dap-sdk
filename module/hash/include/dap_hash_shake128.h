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
 * @file dap_hash_shake128.h
 * @brief SHAKE128 extendable output function (XOF)
 * @details Native DAP implementation based on Keccak-p[1600].
 *          SHAKE128 provides 128-bit security level.
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

#define DAP_SHAKE128_RATE           168   // (1600 - 2*128) / 8

// =============================================================================
// One-shot API
// =============================================================================

/**
 * @brief SHAKE128 extendable output function
 * @param a_output Output buffer
 * @param a_outlen Desired output length in bytes
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_shake128(uint8_t *a_output, size_t a_outlen,
                                     const uint8_t *a_input, size_t a_inlen)
{
    const dap_keccak_sponge_ops_t *ops = dap_keccak_sponge_get_ops();
    uint64_t l_st[25];
    ops->absorb_168(l_st, a_input, a_inlen, DAP_KECCAK_SHAKE_SUFFIX);
    size_t l_nblocks = a_outlen / DAP_SHAKE128_RATE;
    if (l_nblocks) {
        ops->squeeze_168(l_st, a_output, l_nblocks);
        a_output += l_nblocks * DAP_SHAKE128_RATE;
        a_outlen -= l_nblocks * DAP_SHAKE128_RATE;
    }
    if (a_outlen) {
        uint8_t l_tmp[DAP_SHAKE128_RATE];
        ops->squeeze_168(l_st, l_tmp, 1);
        memcpy(a_output, l_tmp, a_outlen);
    }
}

// =============================================================================
// Streaming API
// =============================================================================

/**
 * @brief Initialize SHAKE128 absorb phase
 * @param a_state Keccak state (25 uint64_t)
 * @param a_input Input data
 * @param a_inlen Input length in bytes
 */
DAP_STATIC_INLINE void dap_hash_shake128_absorb(uint64_t *a_state, const uint8_t *a_input, size_t a_inlen)
{
    dap_keccak_sponge_get_ops()->absorb_168(a_state, a_input, a_inlen, DAP_KECCAK_SHAKE_SUFFIX);
}

/**
 * @brief Squeeze blocks from SHAKE128
 * @param a_output Output buffer
 * @param a_nblocks Number of rate-sized blocks to squeeze
 * @param a_state Keccak state
 */
DAP_STATIC_INLINE void dap_hash_shake128_squeezeblocks(uint8_t *a_output, size_t a_nblocks, uint64_t *a_state)
{
    dap_keccak_sponge_get_ops()->squeeze_168(a_state, a_output, a_nblocks);
}

// =============================================================================
// cSHAKE128 (Customizable SHAKE - NIST SP 800-185)
// =============================================================================

/**
 * @brief cSHAKE128 with simple customization (16-bit custom string)
 * @param a_output Output buffer
 * @param a_outlen Desired output length
 * @param a_cstm 16-bit customization value
 * @param a_input Input data
 * @param a_inlen Input length
 */
void dap_hash_cshake128_simple(uint8_t *a_output, size_t a_outlen, uint16_t a_cstm,
                          const uint8_t *a_input, size_t a_inlen);

/**
 * @brief cSHAKE128 absorb with simple customization
 */
void dap_hash_cshake128_simple_absorb(uint64_t a_state[25], uint16_t a_cstm,
                                  const uint8_t *a_input, size_t a_inlen);

/**
 * @brief cSHAKE128 squeeze blocks
 */
DAP_STATIC_INLINE void dap_hash_cshake128_simple_squeezeblocks(uint8_t *a_output, size_t a_nblocks, uint64_t a_state[25])
{
    dap_hash_shake128_squeezeblocks(a_output, a_nblocks, a_state);
}

#ifdef __cplusplus
}
#endif
