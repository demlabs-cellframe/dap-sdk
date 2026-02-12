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
 * @file dap_hash_shake256.c
 * @brief SHAKE256 and cSHAKE256 implementation
 */

#include <string.h>
#include "dap_hash_shake256.h"
#include "dap_hash_keccak.h"

// =============================================================================
// cSHAKE256 Implementation (NIST SP 800-185)
// =============================================================================

// Left-encode a value per NIST SP 800-185
static size_t s_left_encode(uint8_t *a_out, size_t a_value)
{
    size_t l_n = 1;
    size_t l_v = a_value;
    
    while (l_v > 255) {
        l_n++;
        l_v >>= 8;
    }
    
    a_out[0] = (uint8_t)l_n;
    for (size_t i = l_n; i > 0; i--) {
        a_out[i] = (uint8_t)(a_value & 0xFF);
        a_value >>= 8;
    }
    
    return l_n + 1;
}

// Encode and absorb bytepad for cSHAKE
static void s_cshake256_absorb_customization(dap_hash_keccak_ctx_t *a_ctx, uint16_t a_cstm)
{
    uint8_t l_buf[DAP_KECCAK_SHAKE256_RATE];
    size_t l_pos = 0;
    
    // bytepad(encode_string(N) || encode_string(S), rate)
    // For simple cSHAKE: N = "" (empty), S = 2-byte custom value
    
    // left_encode(rate)
    l_pos += s_left_encode(l_buf + l_pos, DAP_KECCAK_SHAKE256_RATE);
    
    // encode_string("") = left_encode(0) = 0x01 0x00
    l_buf[l_pos++] = 0x01;
    l_buf[l_pos++] = 0x00;
    
    // encode_string(S) where S is 2 bytes
    l_buf[l_pos++] = 0x01;  // left_encode(16) for 16 bits
    l_buf[l_pos++] = 0x10;
    l_buf[l_pos++] = (uint8_t)(a_cstm >> 8);
    l_buf[l_pos++] = (uint8_t)(a_cstm & 0xFF);
    
    // Pad to rate boundary
    while (l_pos % DAP_KECCAK_SHAKE256_RATE != 0) {
        l_buf[l_pos++] = 0x00;
    }
    
    // Absorb the customization block
    dap_hash_keccak_sponge_absorb(a_ctx, l_buf, l_pos);
}

void dap_hash_cshake256_simple_absorb(uint64_t a_state[25], uint16_t a_cstm,
                                  const uint8_t *a_input, size_t a_inlen)
{
    dap_hash_keccak_ctx_t l_ctx;
    
    // cSHAKE uses 0x04 suffix instead of 0x1F
    dap_hash_keccak_sponge_init(&l_ctx, DAP_KECCAK_SHAKE256_RATE, 0x04);
    
    // Absorb customization
    s_cshake256_absorb_customization(&l_ctx, a_cstm);
    
    // Absorb input
    dap_hash_keccak_sponge_absorb(&l_ctx, a_input, a_inlen);
    dap_hash_keccak_sponge_finalize(&l_ctx);
    
    memcpy(a_state, l_ctx.state.lanes, DAP_KECCAK_STATE_BYTES);
}

void dap_hash_cshake256_simple(uint8_t *a_output, size_t a_outlen, uint16_t a_cstm,
                          const uint8_t *a_input, size_t a_inlen)
{
    uint64_t l_state[25];
    dap_hash_cshake256_simple_absorb(l_state, a_cstm, a_input, a_inlen);
    
    // Squeeze output
    size_t l_nblocks = a_outlen / DAP_KECCAK_SHAKE256_RATE;
    dap_hash_cshake256_simple_squeezeblocks(a_output, l_nblocks, l_state);
    
    a_output += l_nblocks * DAP_KECCAK_SHAKE256_RATE;
    a_outlen -= l_nblocks * DAP_KECCAK_SHAKE256_RATE;
    
    if (a_outlen > 0) {
        uint8_t l_tmp[DAP_KECCAK_SHAKE256_RATE];
        dap_hash_cshake256_simple_squeezeblocks(l_tmp, 1, l_state);
        memcpy(a_output, l_tmp, a_outlen);
    }
}
