#include <stdint.h>
#include <string.h>

#include "dap_enc_base64.h"
#include "dap_net_trans_websocket_handshake.h"

#define DAP_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define DAP_WS_SHA1_BLOCK_SIZE 64U
#define DAP_WS_SHA1_DIGEST_SIZE 20U
#define DAP_WS_SHA1_ROUNDS 80U

typedef struct dap_ws_sha1_ctx {
    uint32_t state[5];
    uint64_t bit_len;
    uint8_t block[DAP_WS_SHA1_BLOCK_SIZE];
    size_t block_used;
} dap_ws_sha1_ctx_t;

static inline uint32_t s_rotl32(uint32_t a_value, unsigned a_shift)
{
    return (a_value << a_shift) | (a_value >> (32U - a_shift));
}

static inline uint32_t s_read_u32_be(const uint8_t *a_ptr)
{
    return ((uint32_t)a_ptr[0] << 24U) |
           ((uint32_t)a_ptr[1] << 16U) |
           ((uint32_t)a_ptr[2] << 8U) |
           (uint32_t)a_ptr[3];
}

static inline void s_write_u64_be(uint8_t *a_ptr, uint64_t a_value)
{
    for (int i = 7; i >= 0; --i) {
        a_ptr[i] = (uint8_t)(a_value & 0xFFU);
        a_value >>= 8U;
    }
}

static void s_sha1_process_block(dap_ws_sha1_ctx_t *a_ctx, const uint8_t a_block[DAP_WS_SHA1_BLOCK_SIZE])
{
    uint32_t w[DAP_WS_SHA1_ROUNDS];
    for (size_t i = 0; i < 16U; ++i) {
        w[i] = s_read_u32_be(a_block + i * 4U);
    }
    for (size_t i = 16U; i < DAP_WS_SHA1_ROUNDS; ++i) {
        w[i] = s_rotl32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    uint32_t a = a_ctx->state[0];
    uint32_t b = a_ctx->state[1];
    uint32_t c = a_ctx->state[2];
    uint32_t d = a_ctx->state[3];
    uint32_t e = a_ctx->state[4];

    for (size_t i = 0; i < DAP_WS_SHA1_ROUNDS; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }

        uint32_t temp = s_rotl32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = s_rotl32(b, 30U);
        b = a;
        a = temp;
    }

    a_ctx->state[0] += a;
    a_ctx->state[1] += b;
    a_ctx->state[2] += c;
    a_ctx->state[3] += d;
    a_ctx->state[4] += e;
}

static void s_sha1_init(dap_ws_sha1_ctx_t *a_ctx)
{
    a_ctx->state[0] = 0x67452301U;
    a_ctx->state[1] = 0xEFCDAB89U;
    a_ctx->state[2] = 0x98BADCFEU;
    a_ctx->state[3] = 0x10325476U;
    a_ctx->state[4] = 0xC3D2E1F0U;
    a_ctx->bit_len = 0U;
    a_ctx->block_used = 0U;
}

static void s_sha1_update(dap_ws_sha1_ctx_t *a_ctx, const uint8_t *a_data, size_t a_size)
{
    if (!a_size) {
        return;
    }

    a_ctx->bit_len += ((uint64_t)a_size << 3U);

    while (a_size > 0U) {
        size_t l_to_copy = DAP_WS_SHA1_BLOCK_SIZE - a_ctx->block_used;
        if (l_to_copy > a_size) {
            l_to_copy = a_size;
        }
        memcpy(a_ctx->block + a_ctx->block_used, a_data, l_to_copy);
        a_ctx->block_used += l_to_copy;
        a_data += l_to_copy;
        a_size -= l_to_copy;

        if (a_ctx->block_used == DAP_WS_SHA1_BLOCK_SIZE) {
            s_sha1_process_block(a_ctx, a_ctx->block);
            a_ctx->block_used = 0U;
        }
    }
}

static void s_sha1_final(dap_ws_sha1_ctx_t *a_ctx, uint8_t a_digest[DAP_WS_SHA1_DIGEST_SIZE])
{
    a_ctx->block[a_ctx->block_used++] = 0x80U;

    if (a_ctx->block_used > (DAP_WS_SHA1_BLOCK_SIZE - 8U)) {
        memset(a_ctx->block + a_ctx->block_used, 0, DAP_WS_SHA1_BLOCK_SIZE - a_ctx->block_used);
        s_sha1_process_block(a_ctx, a_ctx->block);
        a_ctx->block_used = 0U;
    }

    memset(a_ctx->block + a_ctx->block_used, 0, DAP_WS_SHA1_BLOCK_SIZE - 8U - a_ctx->block_used);
    s_write_u64_be(a_ctx->block + DAP_WS_SHA1_BLOCK_SIZE - 8U, a_ctx->bit_len);
    s_sha1_process_block(a_ctx, a_ctx->block);

    for (size_t i = 0; i < 5U; ++i) {
        a_digest[i * 4U + 0U] = (uint8_t)(a_ctx->state[i] >> 24U);
        a_digest[i * 4U + 1U] = (uint8_t)(a_ctx->state[i] >> 16U);
        a_digest[i * 4U + 2U] = (uint8_t)(a_ctx->state[i] >> 8U);
        a_digest[i * 4U + 3U] = (uint8_t)a_ctx->state[i];
    }
}

int dap_net_trans_websocket_build_accept_key(const char *a_client_key,
                                             char *a_accept_key,
                                             size_t a_accept_key_size)
{
    if (!a_client_key || !a_accept_key || a_accept_key_size < 29U) {
        return -1;
    }

    size_t l_key_len = strlen(a_client_key);
    if (l_key_len > 64U) {
        l_key_len = 64U;
    }

    char l_concat[128] = {0};
    memcpy(l_concat, a_client_key, l_key_len);
    strcat(l_concat, DAP_WS_GUID);

    uint8_t l_sha1[DAP_WS_SHA1_DIGEST_SIZE] = {0};
    dap_ws_sha1_ctx_t l_ctx;
    s_sha1_init(&l_ctx);
    s_sha1_update(&l_ctx, (const uint8_t *)l_concat, strlen(l_concat));
    s_sha1_final(&l_ctx, l_sha1);

    size_t l_encoded_size = dap_enc_base64_encode(l_sha1, DAP_WS_SHA1_DIGEST_SIZE,
                                                  a_accept_key, DAP_ENC_DATA_TYPE_B64);
    if (l_encoded_size == 0U || l_encoded_size >= a_accept_key_size) {
        return -2;
    }

    a_accept_key[l_encoded_size] = '\0';
    return 0;
}
