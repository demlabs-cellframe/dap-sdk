/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
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
 * @file dap_hash_shake_x4.h
 * @brief 4-way parallel SHAKE128/SHAKE256 for PQ crypto
 * @details All 4 inputs must have the same length (common in PQ sampling).
 *          Uses dap_keccak_x4_permute() for SIMD-accelerated permutation.
 *
 *          Convention (matching PQ reference implementations):
 *            absorb: XOR data, pad10*1 + domain sep (0x1F), permute
 *            squeeze: for each block: permute, extract
 */

#pragma once

#include "dap_hash_keccak_x4.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Internal helpers
// ============================================================================

/**
 * @brief Apply SHAKE padding to all 4 instances simultaneously.
 *
 * XORs SHAKE suffix (0x1F) at byte position a_pos and
 * end-of-rate marker (0x80) at byte position a_rate-1
 * into all 4 interleaved instances using word-level ops.
 */
static inline void s_shake_x4_pad(dap_keccak_x4_state_t *a_state,
                                   size_t a_pos, size_t a_rate)
{
    size_t l_lane_s = a_pos / 8;
    size_t l_byte_s = a_pos & 7;
    size_t l_lane_e = (a_rate - 1) / 8;
    size_t l_byte_e = (a_rate - 1) & 7;

    uint64_t l_suffix = (uint64_t)DAP_KECCAK_SHAKE_SUFFIX << (l_byte_s * 8);
    uint64_t l_end    = (uint64_t)0x80 << (l_byte_e * 8);

    uint64_t *l_s = &a_state->lanes[l_lane_s * 4];
    uint64_t *l_e = &a_state->lanes[l_lane_e * 4];
    l_s[0] ^= l_suffix; l_s[1] ^= l_suffix; l_s[2] ^= l_suffix; l_s[3] ^= l_suffix;
    l_e[0] ^= l_end;    l_e[1] ^= l_end;    l_e[2] ^= l_end;    l_e[3] ^= l_end;
}

/**
 * @brief Generic absorb + finalize for SHAKE x4 (same-length inputs)
 */
static inline void s_shake_x4_absorb(dap_keccak_x4_state_t *a_state,
                                      const uint8_t *a_in0,
                                      const uint8_t *a_in1,
                                      const uint8_t *a_in2,
                                      const uint8_t *a_in3,
                                      size_t a_inlen,
                                      size_t a_rate)
{
    dap_keccak_x4_init(a_state);

    while (a_inlen >= a_rate) {
        dap_keccak_x4_xor_bytes_all(a_state, a_in0, a_in1, a_in2, a_in3, a_rate);
        dap_keccak_x4_permute(a_state);
        a_in0   += a_rate;
        a_in1   += a_rate;
        a_in2   += a_rate;
        a_in3   += a_rate;
        a_inlen -= a_rate;
    }

    /* XOR remaining bytes */
    if (a_inlen > 0)
        dap_keccak_x4_xor_bytes_all(a_state, a_in0, a_in1, a_in2, a_in3, a_inlen);

    /* pad10*1 + SHAKE domain separation */
    s_shake_x4_pad(a_state, a_inlen, a_rate);

    /* Finalize permutation */
    dap_keccak_x4_permute(a_state);
}

/**
 * @brief Generic squeeze for SHAKE x4
 */
static inline void s_shake_x4_squeezeblocks(uint8_t *a_out0,
                                              uint8_t *a_out1,
                                              uint8_t *a_out2,
                                              uint8_t *a_out3,
                                              size_t a_nblocks,
                                              dap_keccak_x4_state_t *a_state,
                                              size_t a_rate)
{
    for (size_t i = 0; i < a_nblocks; i++) {
        dap_keccak_x4_permute(a_state);
        dap_keccak_x4_extract_bytes_all(a_state, a_out0, a_out1, a_out2, a_out3,
                                         a_rate);
        a_out0 += a_rate;
        a_out1 += a_rate;
        a_out2 += a_rate;
        a_out3 += a_rate;
    }
}

// ============================================================================
// SHAKE128 x4
// ============================================================================

static inline void dap_hash_shake128_x4_absorb(dap_keccak_x4_state_t *a_state,
                                                const uint8_t *a_in0,
                                                const uint8_t *a_in1,
                                                const uint8_t *a_in2,
                                                const uint8_t *a_in3,
                                                size_t a_inlen)
{
    s_shake_x4_absorb(a_state, a_in0, a_in1, a_in2, a_in3,
                       a_inlen, DAP_KECCAK_SHAKE128_RATE);
}

static inline void dap_hash_shake128_x4_squeezeblocks(uint8_t *a_out0,
                                                       uint8_t *a_out1,
                                                       uint8_t *a_out2,
                                                       uint8_t *a_out3,
                                                       size_t a_nblocks,
                                                       dap_keccak_x4_state_t *a_state)
{
    s_shake_x4_squeezeblocks(a_out0, a_out1, a_out2, a_out3,
                              a_nblocks, a_state, DAP_KECCAK_SHAKE128_RATE);
}

// ============================================================================
// SHAKE256 x4
// ============================================================================

static inline void dap_hash_shake256_x4_absorb(dap_keccak_x4_state_t *a_state,
                                                const uint8_t *a_in0,
                                                const uint8_t *a_in1,
                                                const uint8_t *a_in2,
                                                const uint8_t *a_in3,
                                                size_t a_inlen)
{
    s_shake_x4_absorb(a_state, a_in0, a_in1, a_in2, a_in3,
                       a_inlen, DAP_KECCAK_SHAKE256_RATE);
}

static inline void dap_hash_shake256_x4_squeezeblocks(uint8_t *a_out0,
                                                       uint8_t *a_out1,
                                                       uint8_t *a_out2,
                                                       uint8_t *a_out3,
                                                       size_t a_nblocks,
                                                       dap_keccak_x4_state_t *a_state)
{
    s_shake_x4_squeezeblocks(a_out0, a_out1, a_out2, a_out3,
                              a_nblocks, a_state, DAP_KECCAK_SHAKE256_RATE);
}

#ifdef __cplusplus
}
#endif
