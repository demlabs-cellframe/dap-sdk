/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * MD5 hash implementation (RFC 1321)
 */
#include "dap_hash_md5.h"
#include <string.h>
#include <stdio.h>

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))
#define MD5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) do { \
    (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTATE_LEFT((a), (s)); \
    (a) += (b); \
} while (0)

#define MD5_GG(a, b, c, d, x, s, ac) do { \
    (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTATE_LEFT((a), (s)); \
    (a) += (b); \
} while (0)

#define MD5_HH(a, b, c, d, x, s, ac) do { \
    (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTATE_LEFT((a), (s)); \
    (a) += (b); \
} while (0)

#define MD5_II(a, b, c, d, x, s, ac) do { \
    (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTATE_LEFT((a), (s)); \
    (a) += (b); \
} while (0)

static void s_md5_transform(uint32_t a_state[4], const uint8_t a_block[64])
{
    uint32_t l_a = a_state[0], l_b = a_state[1], l_c = a_state[2], l_d = a_state[3];
    uint32_t l_x[16];
    for (int l_i = 0; l_i < 16; l_i++)
        l_x[l_i] = (uint32_t)a_block[l_i * 4] | ((uint32_t)a_block[l_i * 4 + 1] << 8) |
               ((uint32_t)a_block[l_i * 4 + 2] << 16) | ((uint32_t)a_block[l_i * 4 + 3] << 24);

    MD5_FF(l_a, l_b, l_c, l_d, l_x[0],  7, 0xd76aa478); MD5_FF(l_d, l_a, l_b, l_c, l_x[1],  12, 0xe8c7b756);
    MD5_FF(l_c, l_d, l_a, l_b, l_x[2],  17, 0x242070db); MD5_FF(l_b, l_c, l_d, l_a, l_x[3],  22, 0xc1bdceee);
    MD5_FF(l_a, l_b, l_c, l_d, l_x[4],  7, 0xf57c0faf); MD5_FF(l_d, l_a, l_b, l_c, l_x[5],  12, 0x4787c62a);
    MD5_FF(l_c, l_d, l_a, l_b, l_x[6],  17, 0xa8304613); MD5_FF(l_b, l_c, l_d, l_a, l_x[7],  22, 0xfd469501);
    MD5_FF(l_a, l_b, l_c, l_d, l_x[8],  7, 0x698098d8); MD5_FF(l_d, l_a, l_b, l_c, l_x[9],  12, 0x8b44f7af);
    MD5_FF(l_c, l_d, l_a, l_b, l_x[10], 17, 0xffff5bb1); MD5_FF(l_b, l_c, l_d, l_a, l_x[11], 22, 0x895cd7be);
    MD5_FF(l_a, l_b, l_c, l_d, l_x[12], 7, 0x6b901122); MD5_FF(l_d, l_a, l_b, l_c, l_x[13], 12, 0xfd987193);
    MD5_FF(l_c, l_d, l_a, l_b, l_x[14], 17, 0xa679438e); MD5_FF(l_b, l_c, l_d, l_a, l_x[15], 22, 0x49b40821);

    MD5_GG(l_a, l_b, l_c, l_d, l_x[1],  5, 0xf61e2562); MD5_GG(l_d, l_a, l_b, l_c, l_x[6],  9, 0xc040b340);
    MD5_GG(l_c, l_d, l_a, l_b, l_x[11], 14, 0x265e5a51); MD5_GG(l_b, l_c, l_d, l_a, l_x[0],  20, 0xe9b6c7aa);
    MD5_GG(l_a, l_b, l_c, l_d, l_x[5],  5, 0xd62f105d); MD5_GG(l_d, l_a, l_b, l_c, l_x[10], 9, 0x02441453);
    MD5_GG(l_c, l_d, l_a, l_b, l_x[15], 14, 0xd8a1e681); MD5_GG(l_b, l_c, l_d, l_a, l_x[4],  20, 0xe7d3fbc8);
    MD5_GG(l_a, l_b, l_c, l_d, l_x[9],  5, 0x21e1cde6); MD5_GG(l_d, l_a, l_b, l_c, l_x[14], 9, 0xc33707d6);
    MD5_GG(l_c, l_d, l_a, l_b, l_x[3],  14, 0xf4d50d87); MD5_GG(l_b, l_c, l_d, l_a, l_x[8],  20, 0x455a14ed);
    MD5_GG(l_a, l_b, l_c, l_d, l_x[13], 5, 0xa9e3e905); MD5_GG(l_d, l_a, l_b, l_c, l_x[2],  9, 0xfcefa3f8);
    MD5_GG(l_c, l_d, l_a, l_b, l_x[7],  14, 0x676f02d9); MD5_GG(l_b, l_c, l_d, l_a, l_x[12], 20, 0x8d2a4c8a);

    MD5_HH(l_a, l_b, l_c, l_d, l_x[5],  4, 0xfffa3942); MD5_HH(l_d, l_a, l_b, l_c, l_x[8],  11, 0x8771f681);
    MD5_HH(l_c, l_d, l_a, l_b, l_x[11], 16, 0x6d9d6122); MD5_HH(l_b, l_c, l_d, l_a, l_x[14], 23, 0xfde5380c);
    MD5_HH(l_a, l_b, l_c, l_d, l_x[1],  4, 0xa4beea44); MD5_HH(l_d, l_a, l_b, l_c, l_x[4],  11, 0x4bdecfa9);
    MD5_HH(l_c, l_d, l_a, l_b, l_x[7],  16, 0xf6bb4b60); MD5_HH(l_b, l_c, l_d, l_a, l_x[10], 23, 0xbebfbc70);
    MD5_HH(l_a, l_b, l_c, l_d, l_x[13], 4, 0x289b7ec6); MD5_HH(l_d, l_a, l_b, l_c, l_x[0],  11, 0xeaa127fa);
    MD5_HH(l_c, l_d, l_a, l_b, l_x[3],  16, 0xd4ef3085); MD5_HH(l_b, l_c, l_d, l_a, l_x[6],  23, 0x04881d05);
    MD5_HH(l_a, l_b, l_c, l_d, l_x[9],  4, 0xd9d4d039); MD5_HH(l_d, l_a, l_b, l_c, l_x[12], 11, 0xe6db99e5);
    MD5_HH(l_c, l_d, l_a, l_b, l_x[15], 16, 0x1fa27cf8); MD5_HH(l_b, l_c, l_d, l_a, l_x[2],  23, 0xc4ac5665);

    MD5_II(l_a, l_b, l_c, l_d, l_x[0],  6, 0xf4292244); MD5_II(l_d, l_a, l_b, l_c, l_x[7],  10, 0x432aff97);
    MD5_II(l_c, l_d, l_a, l_b, l_x[14], 15, 0xab9423a7); MD5_II(l_b, l_c, l_d, l_a, l_x[5],  21, 0xfc93a039);
    MD5_II(l_a, l_b, l_c, l_d, l_x[12], 6, 0x655b59c3); MD5_II(l_d, l_a, l_b, l_c, l_x[3],  10, 0x8f0ccc92);
    MD5_II(l_c, l_d, l_a, l_b, l_x[10], 15, 0xffeff47d); MD5_II(l_b, l_c, l_d, l_a, l_x[1],  21, 0x85845dd1);
    MD5_II(l_a, l_b, l_c, l_d, l_x[8],  6, 0x6fa87e4f); MD5_II(l_d, l_a, l_b, l_c, l_x[15], 10, 0xfe2ce6e0);
    MD5_II(l_c, l_d, l_a, l_b, l_x[6],  15, 0xa3014314); MD5_II(l_b, l_c, l_d, l_a, l_x[13], 21, 0x4e0811a1);
    MD5_II(l_a, l_b, l_c, l_d, l_x[4],  6, 0xf7537e82); MD5_II(l_d, l_a, l_b, l_c, l_x[11], 10, 0xbd3af235);
    MD5_II(l_c, l_d, l_a, l_b, l_x[2],  15, 0x2ad7d2bb); MD5_II(l_b, l_c, l_d, l_a, l_x[9],  21, 0xeb86d391);

    a_state[0] += l_a; a_state[1] += l_b; a_state[2] += l_c; a_state[3] += l_d;
}

void dap_hash_md5_init(dap_hash_md5_ctx_t *a_ctx)
{
    if (!a_ctx)
        return;
    a_ctx->state[0] = 0x67452301u;
    a_ctx->state[1] = 0xefcdab89u;
    a_ctx->state[2] = 0x98badcfeu;
    a_ctx->state[3] = 0x10325476u;
    a_ctx->count = 0;
}

void dap_hash_md5_update(dap_hash_md5_ctx_t *a_ctx, const uint8_t *a_data, size_t a_len)
{
    if (!a_ctx || (!a_data && a_len))
        return;

    size_t l_i = 0;
    size_t l_idx = (size_t)((a_ctx->count >> 3) & 0x3F);
    a_ctx->count += (uint64_t)a_len << 3;
    size_t l_part = 64 - l_idx;
    if (a_len >= l_part) {
        memcpy(&a_ctx->buffer[l_idx], a_data, l_part);
        s_md5_transform(a_ctx->state, a_ctx->buffer);
        for (l_i = l_part; l_i + 63 < a_len; l_i += 64) {
            uint8_t l_block[64];
            memcpy(l_block, &a_data[l_i], 64);
            s_md5_transform(a_ctx->state, l_block);
        }
        l_idx = 0;
    } else {
        l_i = 0;
    }
    memcpy(&a_ctx->buffer[l_idx], &a_data[l_i], a_len - l_i);
}

void dap_hash_md5_final(dap_hash_md5_ctx_t *a_ctx, uint8_t a_digest[DAP_HASH_MD5_DIGEST_SIZE])
{
    if (!a_ctx || !a_digest)
        return;

    uint8_t l_bits[8];
    for (int l_i = 0; l_i < 8; l_i++)
        l_bits[l_i] = (uint8_t)(a_ctx->count >> (l_i * 8));

    uint8_t l_pad[64];
    memset(l_pad, 0, sizeof(l_pad));
    l_pad[0] = 0x80;
    size_t l_idx = (size_t)((a_ctx->count >> 3) & 0x3F);
    size_t l_pad_len = (l_idx < 56) ? (56 - l_idx) : (120 - l_idx);
    dap_hash_md5_update(a_ctx, l_pad, l_pad_len);
    dap_hash_md5_update(a_ctx, l_bits, 8);
    for (int l_i = 0; l_i < 4; l_i++) {
        a_digest[l_i * 4]     = (uint8_t)(a_ctx->state[l_i]);
        a_digest[l_i * 4 + 1] = (uint8_t)(a_ctx->state[l_i] >> 8);
        a_digest[l_i * 4 + 2] = (uint8_t)(a_ctx->state[l_i] >> 16);
        a_digest[l_i * 4 + 3] = (uint8_t)(a_ctx->state[l_i] >> 24);
    }
}

void dap_hash_md5(const void *a_data, size_t a_data_len, uint8_t a_digest[DAP_HASH_MD5_DIGEST_SIZE])
{
    if (!a_data || !a_digest)
        return;
    dap_hash_md5_ctx_t l_ctx;
    dap_hash_md5_init(&l_ctx);
    dap_hash_md5_update(&l_ctx, (const uint8_t *)a_data, a_data_len);
    dap_hash_md5_final(&l_ctx, a_digest);
}

int dap_hash_md5_hex(const void *a_data, size_t a_data_len, char *a_hash_hex, size_t a_hash_hex_size)
{
    if (!a_data || !a_hash_hex || a_hash_hex_size < DAP_HASH_MD5_DIGEST_SIZE * 2 + 1)
        return -1;

    uint8_t l_digest[DAP_HASH_MD5_DIGEST_SIZE];
    dap_hash_md5(a_data, a_data_len, l_digest);

    for (int l_i = 0; l_i < DAP_HASH_MD5_DIGEST_SIZE; l_i++)
        snprintf(a_hash_hex + l_i * 2, 3, "%02x", l_digest[l_i]);
    a_hash_hex[DAP_HASH_MD5_DIGEST_SIZE * 2] = '\0';
    return 0;
}
