// AVX2 primitives for 16-bit NTT (256-bit = 16 × int16_t)

typedef __m256i VEC_T;
#define VEC_LANES 16

#define VEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define VEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))
#define VEC_SET1_16(x)     _mm256_set1_epi16(x)
#define VEC_ADD16(a, b)    _mm256_add_epi16(a, b)
#define VEC_SUB16(a, b)    _mm256_sub_epi16(a, b)
#define VEC_MULLO16(a, b)  _mm256_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)  _mm256_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)   _mm256_srai_epi16(a, n)

// Half-width (128-bit) primitives for len=VEC_LANES/2 layer
typedef __m128i HVEC_T;
#define HVEC_LANES 8

#define HVEC_LOAD(p)        _mm_loadu_si128((const __m128i *)(p))
#define HVEC_STORE(p, v)    _mm_storeu_si128((__m128i *)(p), (v))
#define HVEC_SET1_16(x)     _mm_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm_srai_epi16(a, n)

#define VEC_LO_HALF(v)            _mm256_castsi256_si128(v)
#define VEC_HI_HALF(v)            _mm256_extracti128_si256(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm256_setr_m128i(lo, hi)
