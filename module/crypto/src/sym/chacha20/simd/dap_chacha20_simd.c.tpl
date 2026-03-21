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

{{#include PRIMITIVES_FILE}}

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

#if defined(CHACHA_HAS_TRANSPOSE_XOR) && CHACHA_LANES == 8
        /* In-register 8×8 transpose + SIMD XOR — no stack spill.
         * Two passes: lo (words 0-7) and hi (words 8-15) of each block.
         * After transpose, row k = 32-byte half of block k. Stride = 64. */
        {
#define _XOR_ROW(row_reg, blk, off) \
    _mm256_storeu_si256((__m256i *)(a_out + (blk)*64 + (off)), \
        _mm256_xor_si256(row_reg, _mm256_loadu_si256((const __m256i *)(a_in + (blk)*64 + (off)))))

#define TRANSPOSE8_XOR(V0,V1,V2,V3,V4,V5,V6,V7, byte_off) \
do { \
    __m256i _t0 = _mm256_unpacklo_epi32(V0, V1); \
    __m256i _t1 = _mm256_unpackhi_epi32(V0, V1); \
    __m256i _t2 = _mm256_unpacklo_epi32(V2, V3); \
    __m256i _t3 = _mm256_unpackhi_epi32(V2, V3); \
    __m256i _t4 = _mm256_unpacklo_epi32(V4, V5); \
    __m256i _t5 = _mm256_unpackhi_epi32(V4, V5); \
    __m256i _t6 = _mm256_unpacklo_epi32(V6, V7); \
    __m256i _t7 = _mm256_unpackhi_epi32(V6, V7); \
    __m256i _u0 = _mm256_unpacklo_epi64(_t0, _t2); \
    __m256i _u1 = _mm256_unpackhi_epi64(_t0, _t2); \
    __m256i _u2 = _mm256_unpacklo_epi64(_t1, _t3); \
    __m256i _u3 = _mm256_unpackhi_epi64(_t1, _t3); \
    __m256i _u4 = _mm256_unpacklo_epi64(_t4, _t6); \
    __m256i _u5 = _mm256_unpackhi_epi64(_t4, _t6); \
    __m256i _u6 = _mm256_unpacklo_epi64(_t5, _t7); \
    __m256i _u7 = _mm256_unpackhi_epi64(_t5, _t7); \
    _XOR_ROW(_mm256_permute2x128_si256(_u0, _u4, 0x20), 0, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u1, _u5, 0x20), 1, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u2, _u6, 0x20), 2, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u3, _u7, 0x20), 3, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u0, _u4, 0x31), 4, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u1, _u5, 0x31), 5, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u2, _u6, 0x31), 6, byte_off); \
    _XOR_ROW(_mm256_permute2x128_si256(_u3, _u7, 0x31), 7, byte_off); \
} while (0)

            TRANSPOSE8_XOR(v0,v1,v2,v3,v4,v5,v6,v7, 0);
            TRANSPOSE8_XOR(v8,v9,v10,v11,v12,v13,v14,v15, 32);

#undef _XOR_ROW
#undef TRANSPOSE8_XOR
        }
#else
        /* Fallback: store keystream to stack, scalar XOR */
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
#endif

        a_counter += CHACHA_LANES;
        a_out += CHACHA_BLOCK_BYTES;
        a_in  += CHACHA_BLOCK_BYTES;
        a_len -= CHACHA_BLOCK_BYTES;
    }

    if (a_len > 0) {
        dap_chacha20_encrypt(a_out, a_in, a_len, a_key, a_nonce, a_counter);
    }
}
