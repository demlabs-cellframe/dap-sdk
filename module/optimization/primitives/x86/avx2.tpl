// ============================================================================
// AVX2 Shared SIMD Primitives (256-bit)
// Provides unified macro names for all modules using AVX2 arch optimizations.
// ============================================================================

#include <immintrin.h>

typedef __m256i VEC_T;
#define VEC_BITS     256
#define VEC_LANES_8  32
#define VEC_LANES_16 16
#define VEC_LANES_32 8
#define VEC_LANES_64 4

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm256_loadu_si256((const __m256i *)(p))
#define VEC_STORE(p, v)   _mm256_storeu_si256((__m256i *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm256_xor_si256(a, b)
#define VEC_AND(a, b)     _mm256_and_si256(a, b)
#define VEC_OR(a, b)      _mm256_or_si256(a, b)
#define VEC_ANDNOT(a, b)  _mm256_andnot_si256(a, b)

// === Zero / Blend ===========================================================

#define VEC_ZERO()        _mm256_setzero_si256()

// === 8-bit element ops ======================================================

#define VEC_SET1_8(x)      _mm256_set1_epi8(x)
#define VEC_CMPEQ_8(a, b)  _mm256_cmpeq_epi8(a, b)
#define VEC_ADD8(a, b)     _mm256_add_epi8(a, b)
#define VEC_SUB8(a, b)     _mm256_sub_epi8(a, b)
#define VEC_MOVEMASK_8(v)  _mm256_movemask_epi8(v)

// === 16-bit element ops =====================================================

#define VEC_SET1_16(x)      _mm256_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm256_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm256_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm256_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm256_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm256_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm256_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm256_srli_epi16(a, n)

// === 16-bit advanced ops ====================================================

#define VEC_MULHRS16(a, b)      _mm256_mulhrs_epi16(a, b)
#define VEC_BLEND16(a, b, imm)  _mm256_blend_epi16(a, b, imm)
#define VEC_SHUFFLELO16(a, imm) _mm256_shufflelo_epi16(a, imm)
#define VEC_SHUFFLEHI16(a, imm) _mm256_shufflehi_epi16(a, imm)
#define VEC_SETR_16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15) \
    _mm256_setr_epi16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)          _mm256_set1_epi32((int)(x))
#define VEC_ADD32(a, b)         _mm256_add_epi32(a, b)
#define VEC_SUB32(a, b)         _mm256_sub_epi32(a, b)
#define VEC_MULLO32(a, b)       _mm256_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)        _mm256_slli_epi32(a, n)
#define VEC_SRLI32(a, n)        _mm256_srli_epi32(a, n)
#define VEC_SRAI32(a, n)        _mm256_srai_epi32(a, n)
#define VEC_SET_32(h,g,f,e,d,c,b,a)  _mm256_set_epi32(h,g,f,e,d,c,b,a)
#define VEC_CMPEQ_32(a, b)         _mm256_cmpeq_epi32(a, b)
#define VEC_CMPGT_32(a, b)         _mm256_cmpgt_epi32(a, b)
#define VEC_BLENDV_32(mask, t, f)  _mm256_blendv_epi8(f, t, mask)
#define VEC_ANY_TRUE_32(v)         (_mm256_movemask_epi8(v) != 0)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)          _mm256_set1_epi64x(x)
#define VEC_ADD64(a, b)         _mm256_add_epi64(a, b)
#define VEC_SET_64(d, c, b, a)  _mm256_set_epi64x(d, c, b, a)

// === Half-width (128-bit) operations ========================================

typedef __m128i HVEC_T;
#define HVEC_BITS    128
#define HVEC_LANES_8  16
#define HVEC_LANES_16 8
#define HVEC_LANES_32 4
#define HVEC_LANES_64 2

#define HVEC_LOAD(p)       _mm_loadu_si128((const __m128i *)(p))
#define HVEC_STORE(p, v)   _mm_storeu_si128((__m128i *)(p), (v))

#define HVEC_XOR(a, b)     _mm_xor_si128(a, b)
#define HVEC_AND(a, b)     _mm_and_si128(a, b)
#define HVEC_OR(a, b)      _mm_or_si128(a, b)
#define HVEC_ANDNOT(a, b)  _mm_andnot_si128(a, b)

#define HVEC_SET1_16(x)     _mm_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm_srai_epi16(a, n)

#define HVEC_SET1_32(x)     _mm_set1_epi32((int)(x))
#define HVEC_ADD32(a, b)    _mm_add_epi32(a, b)
#define HVEC_SUB32(a, b)    _mm_sub_epi32(a, b)
#define HVEC_SLLI32(a, n)   _mm_slli_epi32(a, n)
#define HVEC_SRLI32(a, n)   _mm_srli_epi32(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm256_castsi256_si128(v)
#define VEC_HI_HALF(v)            _mm256_extracti128_si256(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm256_setr_m128i(lo, hi)
