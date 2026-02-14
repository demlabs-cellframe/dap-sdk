/*! @file hash.c
 *  @brief Hash implementation using native DAP Keccak.
 *
 *  This file is part of the reference implementation of the Picnic signature scheme.
 *  See the accompanying documentation for complete details.
 *
 *  The code is provided under the MIT license, see LICENSE for
 *  more details.
 *  SPDX-License-Identifier: MIT
 */

#include "hash.h"
#include <string.h>

void HashUpdate(HashInstance* ctx, const uint8_t* data, size_t byteLen)
{
    dap_hash_keccak_sponge_absorb(&ctx->ctx, data, byteLen);
}

void HashInit(HashInstance* ctx, paramset_t* params, uint8_t hashPrefix)
{
    if (params->stateSizeBits == 128) {
        /* L1 - SHAKE128 */
        ctx->is_128 = 1;
        dap_hash_keccak_sponge_init(&ctx->ctx, DAP_KECCAK_SHAKE128_RATE, DAP_KECCAK_SHAKE_SUFFIX);
    }
    else {
        /* L3, L5 - SHAKE256 */
        ctx->is_128 = 0;
        dap_hash_keccak_sponge_init(&ctx->ctx, DAP_KECCAK_SHAKE256_RATE, DAP_KECCAK_SHAKE_SUFFIX);
    }

    if (hashPrefix != HASH_PREFIX_NONE) {
        HashUpdate(ctx, &hashPrefix, 1);
    }
}

void HashFinal(HashInstance* ctx)
{
    dap_hash_keccak_sponge_finalize(&ctx->ctx);
}

void HashSqueeze(HashInstance* ctx, uint8_t* digest, size_t byteLen)
{
    size_t l_rate = ctx->is_128 ? DAP_KECCAK_SHAKE128_RATE : DAP_KECCAK_SHAKE256_RATE;
    
    while (byteLen > 0) {
        dap_hash_keccak_permute(&ctx->ctx.state);
        
        size_t l_to_copy = (byteLen < l_rate) ? byteLen : l_rate;
        dap_hash_keccak_extract_bytes(&ctx->ctx.state, digest, l_to_copy);
        
        digest += l_to_copy;
        byteLen -= l_to_copy;
    }
}
