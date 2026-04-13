/* ChaCha20 SIMD primitives — NEON, 4 lanes (4 parallel blocks) */

{{#include PRIM_LIB}}

typedef uint32x4_t chacha_vec_t;
#define CHACHA_LANES VEC_LANES_32
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_ADD32(a, b)   VEC_ADD_U32(a, b)
#define VEC_XOR(a, b)     VEC_XOR_U32(a, b)
#define VEC_SET1(x)       VEC_SET1_U32(x)
#define VEC_LOADU(p)      VEC_LOAD_U32(p)
#define VEC_STOREU(p, v)  VEC_STORE_U32(p, v)

#define VEC_ROTL16(v) \
    vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32((v))))
#define VEC_ROTL12(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 12), VEC_SHR_U32(v, 20))
#define VEC_ROTL8(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 8), VEC_SHR_U32(v, 24))
#define VEC_ROTL7(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 7), VEC_SHR_U32(v, 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    uint32_t c[4] = {a_base, a_base + 1, a_base + 2, a_base + 3};
    return vld1q_u32(c);
}

/*
 * Store keystream to stack (word-interleaved layout), then scalar
 * XOR per lane. Same algorithm as SSE2 fallback.
 */
#define CHACHA_XOR_STORE_FULL(W0,W1,W2,W3,W4,W5,W6,W7,W8,W9,W10,W11,W12,W13,W14,W15, OUTP, INP) \
do { \
    uint32_t _ks[16 * CHACHA_LANES] __attribute__((aligned(16))); \
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
