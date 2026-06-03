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
