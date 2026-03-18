// AVX-512BW primitives for 16-bit NTT (512-bit = 32 × int16_t)

typedef __m512i VEC_T;
#define VEC_LANES 32

#define VEC_LOAD(p)        _mm512_loadu_si512((const void *)(p))
#define VEC_STORE(p, v)    _mm512_storeu_si512((void *)(p), (v))
#define VEC_SET1_16(x)     _mm512_set1_epi16(x)
#define VEC_ADD16(a, b)    _mm512_add_epi16(a, b)
#define VEC_SUB16(a, b)    _mm512_sub_epi16(a, b)
#define VEC_MULLO16(a, b)  _mm512_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)  _mm512_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)   _mm512_srai_epi16(a, n)

// Half-width (256-bit) primitives for len=VEC_LANES/2 layer
typedef __m256i HVEC_T;
#define HVEC_LANES 16

#define HVEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define HVEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))
#define HVEC_SET1_16(x)     _mm256_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm256_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm256_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm256_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm256_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm256_srai_epi16(a, n)

#define VEC_LO_HALF(v)            _mm512_castsi512_si256(v)
#define VEC_HI_HALF(v)            _mm512_extracti64x4_epi64(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm512_inserti64x4(_mm512_castsi256_si512(lo), (hi), 1)
