// ============================================================================
// AVX-512 Shared SIMD Primitives (512-bit)
// Requires AVX-512F + AVX-512BW for full 8/16-bit element support.
// ============================================================================

#include <immintrin.h>

typedef __m512i VEC_T;
#define VEC_BITS     512
#define VEC_LANES_8  64
#define VEC_LANES_16 32
#define VEC_LANES_32 16
#define VEC_LANES_64 8

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm512_loadu_si512((const void *)(p))
#define VEC_STORE(p, v)   _mm512_storeu_si512((void *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm512_xor_si512(a, b)
#define VEC_AND(a, b)     _mm512_and_si512(a, b)
#define VEC_OR(a, b)      _mm512_or_si512(a, b)
#define VEC_ANDNOT(a, b)  _mm512_andnot_si512(a, b)

// === Ternary logic (single-instruction boolean combinations) ================

#define VEC_XOR3(a, b, c)     _mm512_ternarylogic_epi64(a, b, c, 0x96)
#define VEC_CHI(a, b, c)      _mm512_ternarylogic_epi64(a, b, c, 0xD2)

// === Zero ===================================================================

#define VEC_ZERO()        _mm512_setzero_si512()

// === 8-bit element ops (requires AVX-512BW) =================================

#define VEC_SET1_8(x)              _mm512_set1_epi8(x)
#define VEC_ADD8(a, b)             _mm512_add_epi8(a, b)
#define VEC_SUB8(a, b)             _mm512_sub_epi8(a, b)
#define VEC_CMPEQ_8_MASK(a, b)     _mm512_cmpeq_epi8_mask(a, b)
#define VEC_MOVEMASK_8_TYPE        __mmask64

// === 16-bit element ops (requires AVX-512BW) ================================

#define VEC_SET1_16(x)      _mm512_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm512_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm512_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm512_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm512_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm512_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm512_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm512_srli_epi16(a, n)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)      _mm512_set1_epi32((int)(x))
#define VEC_ADD32(a, b)     _mm512_add_epi32(a, b)
#define VEC_SUB32(a, b)     _mm512_sub_epi32(a, b)
#define VEC_MULLO32(a, b)   _mm512_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)    _mm512_slli_epi32(a, n)
#define VEC_SRLI32(a, n)    _mm512_srli_epi32(a, n)
#define VEC_SRAI32(a, n)    _mm512_srai_epi32(a, n)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)      _mm512_set1_epi64(x)
#define VEC_ADD64(a, b)     _mm512_add_epi64(a, b)
#define VEC_ROL64(a, n)     _mm512_rol_epi64(a, n)
#define VEC_ROLV64(a, v)    _mm512_rolv_epi64(a, v)

// === Mask load / store (5-lane for Keccak plane layout etc.) ================

#define VEC_MASKZ_LOAD_64(mask, p)     _mm512_maskz_loadu_epi64(mask, p)
#define VEC_MASK_STORE_64(p, mask, v)  _mm512_mask_storeu_epi64(p, mask, v)
#define VEC_MASK_BLEND_64(mask, a, b)  _mm512_mask_blend_epi64(mask, a, b)

// === Permutation ============================================================

#define VEC_PERMUTEXVAR_64(idx, v)          _mm512_permutexvar_epi64(idx, v)
#define VEC_PERMUTEX2VAR_64(a, idx, b)      _mm512_permutex2var_epi64(a, idx, b)
#define VEC_UNPACKLO_64(a, b)               _mm512_unpacklo_epi64(a, b)
#define VEC_UNPACKHI_64(a, b)               _mm512_unpackhi_epi64(a, b)

// === Half-width (256-bit) operations ========================================

typedef __m256i HVEC_T;
#define HVEC_BITS    256
#define HVEC_LANES_16 16

#define HVEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define HVEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))

#define HVEC_SET1_16(x)     _mm256_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm256_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm256_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm256_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm256_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm256_srai_epi16(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm512_castsi512_si256(v)
#define VEC_HI_HALF(v)            _mm512_extracti64x4_epi64(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm512_inserti64x4(_mm512_castsi256_si512(lo), (hi), 1)
