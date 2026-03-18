/* ChaCha20 SIMD primitives — SSE2, 4 lanes (4 parallel blocks) */
#include <emmintrin.h>

typedef __m128i chacha_vec_t;
#define CHACHA_LANES 4
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_ADD32(a, b)   _mm_add_epi32((a), (b))
#define VEC_XOR(a, b)     _mm_xor_si128((a), (b))
#define VEC_SET1(x)       _mm_set1_epi32((int)(x))
#define VEC_LOADU(p)      _mm_loadu_si128((const __m128i *)(p))
#define VEC_STOREU(p, v)  _mm_storeu_si128((__m128i *)(p), (v))

#define VEC_ROTL16(v) \
    _mm_or_si128(_mm_slli_epi32((v), 16), _mm_srli_epi32((v), 16))
#define VEC_ROTL12(v) \
    _mm_or_si128(_mm_slli_epi32((v), 12), _mm_srli_epi32((v), 20))
#define VEC_ROTL8(v) \
    _mm_or_si128(_mm_slli_epi32((v), 8), _mm_srli_epi32((v), 24))
#define VEC_ROTL7(v) \
    _mm_or_si128(_mm_slli_epi32((v), 7), _mm_srli_epi32((v), 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    return _mm_set_epi32((int)(a_base + 3), (int)(a_base + 2),
                         (int)(a_base + 1), (int)(a_base));
}
