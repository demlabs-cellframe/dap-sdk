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

#define CHACHA_HAS_GATHER_XOR 1
