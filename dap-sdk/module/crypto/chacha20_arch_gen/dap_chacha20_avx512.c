#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/*
 * ChaCha20 multi-block SIMD encrypt — generated from dap_chacha20_simd.c.tpl
 * Architecture: AVX-512
 *
 * Pure algorithmic template: ALL architecture-specific code lives in
 * PRIMITIVES_FILE (following the Keccak pattern). This file contains
 * ZERO intrinsics and ZERO #if CHACHA_LANES branches.
 *
 * Primitives contract — PRIMITIVES_FILE must provide:
 *   Types:   chacha_vec_t
 *   Macros:  CHACHA_LANES, CHACHA_BLOCK_BYTES
 *            VEC_SET1, VEC_ADD32, VEC_XOR, VEC_ROTL{16,12,8,7}
 *            CHACHA_XOR_STORE_FULL(W0..W15, OUT, IN)
 *   Funcs:   s_vec_counter_init(uint32_t base) -> chacha_vec_t
 *   Optional: CHACHA_HAS_DUAL_BLOCK — enables 2× block pipeline
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "dap_chacha20_poly1305.h"

/* ChaCha20 SIMD primitives — AVX-512F, 16 lanes (16 parallel blocks, 1024 bytes/iter) */

#include <immintrin.h>

typedef __m512i chacha_vec_t;
#define CHACHA_LANES 16
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_SET1(x)       _mm512_set1_epi32((int)(x))
#define VEC_LOADU(p)      _mm512_loadu_si512((const void *)(p))
#define VEC_STOREU(p, v)  _mm512_storeu_si512((void *)(p), (v))
#define VEC_ADD32(a, b)   _mm512_add_epi32(a, b)
#define VEC_XOR(a, b)     _mm512_xor_si512(a, b)
#define VEC_OR(a, b)      _mm512_or_si512(a, b)
#define VEC_SLLI32(a, n)  _mm512_slli_epi32(a, n)
#define VEC_SRLI32(a, n)  _mm512_srli_epi32(a, n)

#define VEC_ROTL16(v) _mm512_rol_epi32(v, 16)
#define VEC_ROTL12(v) _mm512_rol_epi32(v, 12)
#define VEC_ROTL8(v)  _mm512_rol_epi32(v, 8)
#define VEC_ROTL7(v)  _mm512_rol_epi32(v, 7)

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    return _mm512_setr_epi32(
        (int)(a_base),      (int)(a_base + 1),  (int)(a_base + 2),  (int)(a_base + 3),
        (int)(a_base + 4),  (int)(a_base + 5),  (int)(a_base + 6),  (int)(a_base + 7),
        (int)(a_base + 8),  (int)(a_base + 9),  (int)(a_base + 10), (int)(a_base + 11),
        (int)(a_base + 12), (int)(a_base + 13), (int)(a_base + 14), (int)(a_base + 15));
}

/*
 * 16-lane gather + XOR + store.
 * Store keystream to stack (word-interleaved layout), then use vgatherdps
 * with stride-16 index to deinterleave per-block, XOR plaintext, store.
 */
#define CHACHA_XOR_STORE_FULL(W0,W1,W2,W3,W4,W5,W6,W7,W8,W9,W10,W11,W12,W13,W14,W15, OUTP, INP) \
do { \
    uint32_t _ks[16 * 16] __attribute__((aligned(64))); \
    _mm512_storeu_si512(_ks + 0*16,  W0);  _mm512_storeu_si512(_ks + 1*16,  W1); \
    _mm512_storeu_si512(_ks + 2*16,  W2);  _mm512_storeu_si512(_ks + 3*16,  W3); \
    _mm512_storeu_si512(_ks + 4*16,  W4);  _mm512_storeu_si512(_ks + 5*16,  W5); \
    _mm512_storeu_si512(_ks + 6*16,  W6);  _mm512_storeu_si512(_ks + 7*16,  W7); \
    _mm512_storeu_si512(_ks + 8*16,  W8);  _mm512_storeu_si512(_ks + 9*16,  W9); \
    _mm512_storeu_si512(_ks + 10*16, W10); _mm512_storeu_si512(_ks + 11*16, W11); \
    _mm512_storeu_si512(_ks + 12*16, W12); _mm512_storeu_si512(_ks + 13*16, W13); \
    _mm512_storeu_si512(_ks + 14*16, W14); _mm512_storeu_si512(_ks + 15*16, W15); \
    __m512i _gidx = _mm512_setr_epi32( \
        0,16,32,48,64,80,96,112,128,144,160,176,192,208,224,240); \
    for (int _lane = 0; _lane < 16; _lane++) { \
        __m512i _pt = _mm512_loadu_si512((INP) + _lane * 64); \
        __m512i _bks = _mm512_i32gather_epi32(_gidx, &_ks[_lane], 4); \
        _mm512_storeu_si512((OUTP) + _lane * 64, _mm512_xor_si512(_pt, _bks)); \
    } \
} while (0)

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

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_chacha20_encrypt_avx512(
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

#ifdef CHACHA_HAS_DUAL_BLOCK
    while (a_len >= 2 * CHACHA_BLOCK_BYTES) {
        chacha_vec_t a0  = VEC_SET1(s[0]),  a1  = VEC_SET1(s[1]);
        chacha_vec_t a2  = VEC_SET1(s[2]),  a3  = VEC_SET1(s[3]);
        chacha_vec_t a4  = VEC_SET1(s[4]),  a5  = VEC_SET1(s[5]);
        chacha_vec_t a6  = VEC_SET1(s[6]),  a7  = VEC_SET1(s[7]);
        chacha_vec_t a8  = VEC_SET1(s[8]),  a9  = VEC_SET1(s[9]);
        chacha_vec_t a10 = VEC_SET1(s[10]), a11 = VEC_SET1(s[11]);
        chacha_vec_t a12 = s_vec_counter_init(a_counter);
        chacha_vec_t a13 = VEC_SET1(s[13]), a14 = VEC_SET1(s[14]);
        chacha_vec_t a15 = VEC_SET1(s[15]);
        chacha_vec_t sa12 = a12;

        chacha_vec_t b0  = VEC_SET1(s[0]),  b1  = VEC_SET1(s[1]);
        chacha_vec_t b2  = VEC_SET1(s[2]),  b3  = VEC_SET1(s[3]);
        chacha_vec_t b4  = VEC_SET1(s[4]),  b5  = VEC_SET1(s[5]);
        chacha_vec_t b6  = VEC_SET1(s[6]),  b7  = VEC_SET1(s[7]);
        chacha_vec_t b8  = VEC_SET1(s[8]),  b9  = VEC_SET1(s[9]);
        chacha_vec_t b10 = VEC_SET1(s[10]), b11 = VEC_SET1(s[11]);
        chacha_vec_t b12 = s_vec_counter_init(a_counter + CHACHA_LANES);
        chacha_vec_t b13 = VEC_SET1(s[13]), b14 = VEC_SET1(s[14]);
        chacha_vec_t b15 = VEC_SET1(s[15]);
        chacha_vec_t sb12 = b12;

        for (int r = 0; r < 10; r++) {
            QR_VEC(a0, a4, a8,  a12); QR_VEC(b0, b4, b8,  b12);
            QR_VEC(a1, a5, a9,  a13); QR_VEC(b1, b5, b9,  b13);
            QR_VEC(a2, a6, a10, a14); QR_VEC(b2, b6, b10, b14);
            QR_VEC(a3, a7, a11, a15); QR_VEC(b3, b7, b11, b15);
            QR_VEC(a0, a5, a10, a15); QR_VEC(b0, b5, b10, b15);
            QR_VEC(a1, a6, a11, a12); QR_VEC(b1, b6, b11, b12);
            QR_VEC(a2, a7, a8,  a13); QR_VEC(b2, b7, b8,  b13);
            QR_VEC(a3, a4, a9,  a14); QR_VEC(b3, b4, b9,  b14);
        }

        a0  = VEC_ADD32(a0,  VEC_SET1(s[0]));  b0  = VEC_ADD32(b0,  VEC_SET1(s[0]));
        a1  = VEC_ADD32(a1,  VEC_SET1(s[1]));  b1  = VEC_ADD32(b1,  VEC_SET1(s[1]));
        a2  = VEC_ADD32(a2,  VEC_SET1(s[2]));  b2  = VEC_ADD32(b2,  VEC_SET1(s[2]));
        a3  = VEC_ADD32(a3,  VEC_SET1(s[3]));  b3  = VEC_ADD32(b3,  VEC_SET1(s[3]));
        a4  = VEC_ADD32(a4,  VEC_SET1(s[4]));  b4  = VEC_ADD32(b4,  VEC_SET1(s[4]));
        a5  = VEC_ADD32(a5,  VEC_SET1(s[5]));  b5  = VEC_ADD32(b5,  VEC_SET1(s[5]));
        a6  = VEC_ADD32(a6,  VEC_SET1(s[6]));  b6  = VEC_ADD32(b6,  VEC_SET1(s[6]));
        a7  = VEC_ADD32(a7,  VEC_SET1(s[7]));  b7  = VEC_ADD32(b7,  VEC_SET1(s[7]));
        a8  = VEC_ADD32(a8,  VEC_SET1(s[8]));  b8  = VEC_ADD32(b8,  VEC_SET1(s[8]));
        a9  = VEC_ADD32(a9,  VEC_SET1(s[9]));  b9  = VEC_ADD32(b9,  VEC_SET1(s[9]));
        a10 = VEC_ADD32(a10, VEC_SET1(s[10])); b10 = VEC_ADD32(b10, VEC_SET1(s[10]));
        a11 = VEC_ADD32(a11, VEC_SET1(s[11])); b11 = VEC_ADD32(b11, VEC_SET1(s[11]));
        a12 = VEC_ADD32(a12, sa12);            b12 = VEC_ADD32(b12, sb12);
        a13 = VEC_ADD32(a13, VEC_SET1(s[13])); b13 = VEC_ADD32(b13, VEC_SET1(s[13]));
        a14 = VEC_ADD32(a14, VEC_SET1(s[14])); b14 = VEC_ADD32(b14, VEC_SET1(s[14]));
        a15 = VEC_ADD32(a15, VEC_SET1(s[15])); b15 = VEC_ADD32(b15, VEC_SET1(s[15]));

        CHACHA_XOR_STORE_FULL(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15, a_out, a_in);
        CHACHA_XOR_STORE_FULL(b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15, a_out + CHACHA_BLOCK_BYTES, a_in + CHACHA_BLOCK_BYTES);

        a_counter += 2 * CHACHA_LANES;
        a_out += 2 * CHACHA_BLOCK_BYTES;
        a_in  += 2 * CHACHA_BLOCK_BYTES;
        a_len -= 2 * CHACHA_BLOCK_BYTES;
    }
#endif

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

        CHACHA_XOR_STORE_FULL(v0,v1,v2,v3,v4,v5,v6,v7,v8,v9,v10,v11,v12,v13,v14,v15, a_out, a_in);

        a_counter += CHACHA_LANES;
        a_out += CHACHA_BLOCK_BYTES;
        a_in  += CHACHA_BLOCK_BYTES;
        a_len -= CHACHA_BLOCK_BYTES;
    }

    if (a_len > 0) {
        dap_chacha20_encrypt(a_out, a_in, a_len, a_key, a_nonce, a_counter);
    }
}
#endif
