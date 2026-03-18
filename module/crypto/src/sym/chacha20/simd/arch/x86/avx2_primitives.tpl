/* ChaCha20 SIMD primitives — AVX2, 8 lanes (8 parallel blocks) */
#include <immintrin.h>

typedef __m256i chacha_vec_t;
#define CHACHA_LANES 8
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_ADD32(a, b)   _mm256_add_epi32((a), (b))
#define VEC_XOR(a, b)     _mm256_xor_si256((a), (b))
#define VEC_SET1(x)       _mm256_set1_epi32((int)(x))
#define VEC_LOADU(p)      _mm256_loadu_si256((const __m256i *)(p))
#define VEC_STOREU(p, v)  _mm256_storeu_si256((__m256i *)(p), (v))

#define VEC_ROTL16(v) \
    _mm256_or_si256(_mm256_slli_epi32((v), 16), _mm256_srli_epi32((v), 16))
#define VEC_ROTL12(v) \
    _mm256_or_si256(_mm256_slli_epi32((v), 12), _mm256_srli_epi32((v), 20))
#define VEC_ROTL8(v) \
    _mm256_or_si256(_mm256_slli_epi32((v), 8), _mm256_srli_epi32((v), 24))
#define VEC_ROTL7(v) \
    _mm256_or_si256(_mm256_slli_epi32((v), 7), _mm256_srli_epi32((v), 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    return _mm256_set_epi32((int)(a_base + 7), (int)(a_base + 6),
                            (int)(a_base + 5), (int)(a_base + 4),
                            (int)(a_base + 3), (int)(a_base + 2),
                            (int)(a_base + 1), (int)(a_base));
}
