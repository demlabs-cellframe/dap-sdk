#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/*
 * ChaCha20 multi-block SIMD encrypt — generated from dap_chacha20_simd.c.tpl
 * Architecture: SSE2
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

/* ChaCha20 SIMD primitives — SSE2, 4 lanes (4 parallel blocks) */

// ============================================================================
// SSE2 Shared SIMD Primitives (128-bit)
// Provides unified macro names for all modules using SSE2 arch optimizations.
// ============================================================================

#include <emmintrin.h>

typedef __m128i VEC_T;
#define VEC_BITS     128
#define VEC_LANES_8  16
#define VEC_LANES_16 8
#define VEC_LANES_32 4
#define VEC_LANES_64 2

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm_loadu_si128((const __m128i *)(p))
#define VEC_STORE(p, v)   _mm_storeu_si128((__m128i *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm_xor_si128(a, b)
#define VEC_AND(a, b)     _mm_and_si128(a, b)
#define VEC_OR(a, b)      _mm_or_si128(a, b)
#define VEC_ANDNOT(a, b)  _mm_andnot_si128(a, b)

// === Zero / Blend ===========================================================

#define VEC_ZERO()        _mm_setzero_si128()

// === 8-bit element ops ======================================================

#define VEC_SET1_8(x)      _mm_set1_epi8(x)
#define VEC_CMPEQ_8(a, b)  _mm_cmpeq_epi8(a, b)
#define VEC_ADD8(a, b)     _mm_add_epi8(a, b)
#define VEC_SUB8(a, b)     _mm_sub_epi8(a, b)
#define VEC_MOVEMASK_8(v)  _mm_movemask_epi8(v)

// === 16-bit element ops =====================================================

#define VEC_SET1_16(x)      _mm_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm_srli_epi16(a, n)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)          _mm_set1_epi32((int)(x))
#define VEC_ADD32(a, b)         _mm_add_epi32(a, b)
#define VEC_SUB32(a, b)         _mm_sub_epi32(a, b)
#define VEC_SLLI32(a, n)        _mm_slli_epi32(a, n)
#define VEC_SRLI32(a, n)        _mm_srli_epi32(a, n)
#define VEC_SRAI32(a, n)        _mm_srai_epi32(a, n)
#define VEC_SET_32(d,c,b,a)     _mm_set_epi32(d,c,b,a)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)          _mm_set1_epi64x(x)
#define VEC_ADD64(a, b)         _mm_add_epi64(a, b)
#define VEC_SET_64(b, a)        _mm_set_epi64x(b, a)

typedef VEC_T chacha_vec_t;
#define CHACHA_LANES VEC_LANES_32
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_SET1(x)       VEC_SET1_32(x)
#define VEC_LOADU(p)      VEC_LOAD(p)
#define VEC_STOREU(p, v)  VEC_STORE(p, v)

#define VEC_ROTL16(v) VEC_OR(VEC_SLLI32(v, 16), VEC_SRLI32(v, 16))
#define VEC_ROTL12(v) VEC_OR(VEC_SLLI32(v, 12), VEC_SRLI32(v, 20))
#define VEC_ROTL8(v)  VEC_OR(VEC_SLLI32(v, 8),  VEC_SRLI32(v, 24))
#define VEC_ROTL7(v)  VEC_OR(VEC_SLLI32(v, 7),  VEC_SRLI32(v, 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    return VEC_SET_32((int)(a_base + 3), (int)(a_base + 2),
                      (int)(a_base + 1), (int)(a_base));
}

/*
 * Store keystream to stack (word-interleaved layout), then scalar
 * XOR per lane. Works for any lane count.
 */
#define CHACHA_XOR_STORE_FULL(W0,W1,W2,W3,W4,W5,W6,W7,W8,W9,W10,W11,W12,W13,W14,W15, OUTP, INP) \
do { \
    uint32_t _ks[16 * CHACHA_LANES] __attribute__((aligned(64))); \
    VEC_STOREU(_ks + 0  * CHACHA_LANES, W0);  VEC_STOREU(_ks + 1  * CHACHA_LANES, W1); \
    VEC_STOREU(_ks + 2  * CHACHA_LANES, W2);  VEC_STOREU(_ks + 3  * CHACHA_LANES, W3); \
    VEC_STOREU(_ks + 4  * CHACHA_LANES, W4);  VEC_STOREU(_ks + 5  * CHACHA_LANES, W5); \
    VEC_STOREU(_ks + 6  * CHACHA_LANES, W6);  VEC_STOREU(_ks + 7  * CHACHA_LANES, W7); \
    VEC_STOREU(_ks + 8  * CHACHA_LANES, W8);  VEC_STOREU(_ks + 9  * CHACHA_LANES, W9); \
    VEC_STOREU(_ks + 10 * CHACHA_LANES, W10); VEC_STOREU(_ks + 11 * CHACHA_LANES, W11); \
    VEC_STOREU(_ks + 12 * CHACHA_LANES, W12); VEC_STOREU(_ks + 13 * CHACHA_LANES, W13); \
    VEC_STOREU(_ks + 14 * CHACHA_LANES, W14); VEC_STOREU(_ks + 15 * CHACHA_LANES, W15); \
    for (int _lane = 0; _lane < CHACHA_LANES; _lane++) { \
        const uint32_t *_li = (const uint32_t *)((INP) + _lane * 64); \
        uint32_t *_lo = (uint32_t *)((OUTP) + _lane * 64); \
        for (int _w = 0; _w < 16; _w++) \
            _lo[_w] = _li[_w] ^ _ks[_w * CHACHA_LANES + _lane]; \
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

__attribute__((target("sse2")))
void dap_chacha20_encrypt_sse2(
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
