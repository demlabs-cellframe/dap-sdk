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

#define VEC_SET1_32(x)      _mm_set1_epi32((int)(x))
#define VEC_ADD32(a, b)     _mm_add_epi32(a, b)
#define VEC_SUB32(a, b)     _mm_sub_epi32(a, b)
#define VEC_SLLI32(a, n)    _mm_slli_epi32(a, n)
#define VEC_SRLI32(a, n)    _mm_srli_epi32(a, n)
#define VEC_SET_32(d,c,b,a) _mm_set_epi32(d, c, b, a)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)      _mm_set1_epi64x(x)
#define VEC_ADD64(a, b)     _mm_add_epi64(a, b)
#define VEC_SET_64(hi, lo)  _mm_set_epi64x(hi, lo)
