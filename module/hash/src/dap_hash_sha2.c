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
 * @file dap_hash_sha2.c
 * @brief SHA-2 implementation (SHA-256)
 * @details FIPS 180-4 compliant implementation.
 */

#include "dap_hash_sha2.h"
#include <string.h>

// =============================================================================
// SHA-256 Constants
// =============================================================================

// Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes)
static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// Round constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// =============================================================================
// Helper Macros
// =============================================================================

#define ROTR32(x, n)    (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)     (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)          (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define EP1(x)          (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SIG0(x)         (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SIG1(x)         (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

// Big-endian load/store
static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline void store_be32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24);
    p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8);
    p[3] = (uint8_t)x;
}

static inline void store_be64(uint8_t *p, uint64_t x) {
    p[0] = (uint8_t)(x >> 56);
    p[1] = (uint8_t)(x >> 48);
    p[2] = (uint8_t)(x >> 40);
    p[3] = (uint8_t)(x >> 32);
    p[4] = (uint8_t)(x >> 24);
    p[5] = (uint8_t)(x >> 16);
    p[6] = (uint8_t)(x >> 8);
    p[7] = (uint8_t)x;
}

// =============================================================================
// SHA-256 Transform (process one 64-byte block)
// =============================================================================

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    // Prepare message schedule
    for (i = 0; i < 16; i++) {
        W[i] = load_be32(block + i * 4);
    }
    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];
    }

    // Initialize working variables
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    // 64 rounds
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + SHA256_K[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Add to state
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// =============================================================================
// Public API: Incremental
// =============================================================================

void dap_hash_sha2_256_init(dap_hash_sha2_256_ctx_t *a_ctx) {
    memcpy(a_ctx->state, SHA256_H0, sizeof(SHA256_H0));
    a_ctx->count = 0;
}

void dap_hash_sha2_256_update(dap_hash_sha2_256_ctx_t *a_ctx, const uint8_t *a_data, size_t a_len) {
    size_t pos = (size_t)(a_ctx->count & 63);  // Position in buffer
    
    a_ctx->count += a_len;
    
    // If we have buffered data, fill and process
    if (pos > 0) {
        size_t space = 64 - pos;
        if (a_len < space) {
            memcpy(a_ctx->buffer + pos, a_data, a_len);
            return;
        }
        memcpy(a_ctx->buffer + pos, a_data, space);
        sha256_transform(a_ctx->state, a_ctx->buffer);
        a_data += space;
        a_len -= space;
    }
    
    // Process full blocks
    while (a_len >= 64) {
        sha256_transform(a_ctx->state, a_data);
        a_data += 64;
        a_len -= 64;
    }
    
    // Buffer remaining
    if (a_len > 0) {
        memcpy(a_ctx->buffer, a_data, a_len);
    }
}

void dap_hash_sha2_256_final(dap_hash_sha2_256_ctx_t *a_ctx, uint8_t a_output[32]) {
    uint8_t pad[72];  // Max padding: 64 + 8 bytes
    size_t pos = (size_t)(a_ctx->count & 63);
    size_t padlen;
    
    // Padding: 0x80, then zeros, then 64-bit bit count (big-endian)
    pad[0] = 0x80;
    
    if (pos < 56) {
        padlen = 56 - pos;
    } else {
        padlen = 120 - pos;  // Need extra block
    }
    
    memset(pad + 1, 0, padlen - 1);
    store_be64(pad + padlen, a_ctx->count * 8);  // Bit count
    
    dap_hash_sha2_256_update(a_ctx, pad, padlen + 8);
    
    // Output hash
    for (int i = 0; i < 8; i++) {
        store_be32(a_output + i * 4, a_ctx->state[i]);
    }
    
    // Clear context
    memset(a_ctx, 0, sizeof(*a_ctx));
}

// =============================================================================
// Public API: One-shot
// =============================================================================

void dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    dap_hash_sha2_256_ctx_t ctx;
    dap_hash_sha2_256_init(&ctx);
    if (a_input && a_inlen > 0) {
        dap_hash_sha2_256_update(&ctx, a_input, a_inlen);
    }
    dap_hash_sha2_256_final(&ctx, a_output);
}

// =============================================================================
// HMAC-SHA256
// =============================================================================

void dap_hash_hmac_sha2_256(
    uint8_t a_output[32],
    const uint8_t *a_key, size_t a_keylen,
    const uint8_t *a_data, size_t a_datalen)
{
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[32];
    dap_hash_sha2_256_ctx_t ctx;
    
    // If key > 64 bytes, hash it first
    if (a_keylen > 64) {
        dap_hash_sha2_256(tk, a_key, a_keylen);
        a_key = tk;
        a_keylen = 32;
    }
    
    // Prepare pads
    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5c, 64);
    for (size_t i = 0; i < a_keylen; i++) {
        k_ipad[i] ^= a_key[i];
        k_opad[i] ^= a_key[i];
    }
    
    // Inner hash: H(k_ipad || data)
    dap_hash_sha2_256_init(&ctx);
    dap_hash_sha2_256_update(&ctx, k_ipad, 64);
    dap_hash_sha2_256_update(&ctx, a_data, a_datalen);
    dap_hash_sha2_256_final(&ctx, tk);
    
    // Outer hash: H(k_opad || inner_hash)
    dap_hash_sha2_256_init(&ctx);
    dap_hash_sha2_256_update(&ctx, k_opad, 64);
    dap_hash_sha2_256_update(&ctx, tk, 32);
    dap_hash_sha2_256_final(&ctx, a_output);
    
    // Clear sensitive data
    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memset(tk, 0, 32);
}
