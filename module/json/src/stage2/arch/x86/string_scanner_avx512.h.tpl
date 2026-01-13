/**
 * @file string_scanner_avx512.h.tpl
 * @brief AVX-512-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

// AVX-512 intrinsics
#include <immintrin.h>

#define SIMD_VEC_TYPE __m512i
#define SIMD_CHUNK_SIZE 64
#define SIMD_MASK_TYPE __mmask64

// Load unaligned chunk
#define SIMD_LOAD(ptr) _mm512_loadu_si512((__m512i*)(ptr))

// Set all bytes to same value
#define SIMD_SET1(val) _mm512_set1_epi8(val)

// Compare bytes for equality (returns mask, not vector!)
#define SIMD_CMP_EQ_MASK(a, b) _mm512_cmpeq_epi8_mask((a), (b))

// Bitwise OR for masks
#define SIMD_OR_MASK(a, b) ((a) | (b))

// Mask is already the result (no movemask needed)
#define SIMD_GET_MASK(mask) (mask)
