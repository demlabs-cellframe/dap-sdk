/*
 * ChaCha20 multi-block SIMD encrypt — generated from dap_chacha20_simd.c.tpl
 * Architecture: {{ARCH_NAME}}
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "dap_chacha20_poly1305.h"

{{PRIMITIVES}}

/* Quarter-round on vectorized state */
#define QR_VEC(a, b, c, d) do {   \
    a = VEC_ADD32(a, b); d = VEC_XOR(d, a); d = VEC_ROTL16(d); \
    c = VEC_ADD32(c, d); b = VEC_XOR(b, c); b = VEC_ROTL12(b); \
    a = VEC_ADD32(a, b); d = VEC_XOR(d, a); d = VEC_ROTL8(d);  \
    c = VEC_ADD32(c, d); b = VEC_XOR(b, c); b = VEC_ROTL7(b);  \
} while (0)

static inline uint32_t s_load32_le_tpl(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8
         | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void s_store32_le_tpl(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v); p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

{{TARGET_ATTR}}
void dap_chacha20_encrypt_{{ARCH_LOWER}}(
        uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter)
{
    uint32_t s[16];
    s[0] = 0x61707865; s[1] = 0x3320646e;
    s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        s[4 + i] = s_load32_le_tpl(a_key + 4 * i);
    s[13] = s_load32_le_tpl(a_nonce);
    s[14] = s_load32_le_tpl(a_nonce + 4);
    s[15] = s_load32_le_tpl(a_nonce + 8);

    while (a_len >= CHACHA_BLOCK_BYTES) {
        chacha_vec_t v0  = VEC_SET1(s[0]),  v1  = VEC_SET1(s[1]);
        chacha_vec_t v2  = VEC_SET1(s[2]),  v3  = VEC_SET1(s[3]);
        chacha_vec_t v4  = VEC_SET1(s[4]),  v5  = VEC_SET1(s[5]);
        chacha_vec_t v6  = VEC_SET1(s[6]),  v7  = VEC_SET1(s[7]);
        chacha_vec_t v8  = VEC_SET1(s[8]),  v9  = VEC_SET1(s[9]);
        chacha_vec_t v10 = VEC_SET1(s[10]), v11 = VEC_SET1(s[11]);
        chacha_vec_t v12 = s_vec_counter_init(a_counter);
        chacha_vec_t v13 = VEC_SET1(s[13]), v14 = VEC_SET1(s[14]);
        chacha_vec_t v15 = VEC_SET1(s[15]);

        chacha_vec_t s12 = v12;

        for (int r = 0; r < 10; r++) {
            QR_VEC(v0, v4, v8,  v12);
            QR_VEC(v1, v5, v9,  v13);
            QR_VEC(v2, v6, v10, v14);
            QR_VEC(v3, v7, v11, v15);
            QR_VEC(v0, v5, v10, v15);
            QR_VEC(v1, v6, v11, v12);
            QR_VEC(v2, v7, v8,  v13);
            QR_VEC(v3, v4, v9,  v14);
        }

        v0  = VEC_ADD32(v0,  VEC_SET1(s[0]));
        v1  = VEC_ADD32(v1,  VEC_SET1(s[1]));
        v2  = VEC_ADD32(v2,  VEC_SET1(s[2]));
        v3  = VEC_ADD32(v3,  VEC_SET1(s[3]));
        v4  = VEC_ADD32(v4,  VEC_SET1(s[4]));
        v5  = VEC_ADD32(v5,  VEC_SET1(s[5]));
        v6  = VEC_ADD32(v6,  VEC_SET1(s[6]));
        v7  = VEC_ADD32(v7,  VEC_SET1(s[7]));
        v8  = VEC_ADD32(v8,  VEC_SET1(s[8]));
        v9  = VEC_ADD32(v9,  VEC_SET1(s[9]));
        v10 = VEC_ADD32(v10, VEC_SET1(s[10]));
        v11 = VEC_ADD32(v11, VEC_SET1(s[11]));
        v12 = VEC_ADD32(v12, s12);
        v13 = VEC_ADD32(v13, VEC_SET1(s[13]));
        v14 = VEC_ADD32(v14, VEC_SET1(s[14]));
        v15 = VEC_ADD32(v15, VEC_SET1(s[15]));

        /* Store keystream vectors to flat buffer, XOR with plaintext */
        uint32_t ks[16 * CHACHA_LANES] __attribute__((aligned(64)));
        VEC_STOREU(ks + 0  * CHACHA_LANES, v0);
        VEC_STOREU(ks + 1  * CHACHA_LANES, v1);
        VEC_STOREU(ks + 2  * CHACHA_LANES, v2);
        VEC_STOREU(ks + 3  * CHACHA_LANES, v3);
        VEC_STOREU(ks + 4  * CHACHA_LANES, v4);
        VEC_STOREU(ks + 5  * CHACHA_LANES, v5);
        VEC_STOREU(ks + 6  * CHACHA_LANES, v6);
        VEC_STOREU(ks + 7  * CHACHA_LANES, v7);
        VEC_STOREU(ks + 8  * CHACHA_LANES, v8);
        VEC_STOREU(ks + 9  * CHACHA_LANES, v9);
        VEC_STOREU(ks + 10 * CHACHA_LANES, v10);
        VEC_STOREU(ks + 11 * CHACHA_LANES, v11);
        VEC_STOREU(ks + 12 * CHACHA_LANES, v12);
        VEC_STOREU(ks + 13 * CHACHA_LANES, v13);
        VEC_STOREU(ks + 14 * CHACHA_LANES, v14);
        VEC_STOREU(ks + 15 * CHACHA_LANES, v15);

        for (int lane = 0; lane < CHACHA_LANES; lane++) {
            const uint32_t *l_in_w = (const uint32_t *)(a_in + lane * 64);
            uint32_t *l_out_w = (uint32_t *)(a_out + lane * 64);
            for (int w = 0; w < 16; w++)
                l_out_w[w] = l_in_w[w] ^ ks[w * CHACHA_LANES + lane];
        }

        a_counter += CHACHA_LANES;
        a_out += CHACHA_BLOCK_BYTES;
        a_in  += CHACHA_BLOCK_BYTES;
        a_len -= CHACHA_BLOCK_BYTES;
    }

    if (a_len > 0) {
        dap_chacha20_encrypt(a_out, a_in, a_len, a_key, a_nonce, a_counter);
    }
}
