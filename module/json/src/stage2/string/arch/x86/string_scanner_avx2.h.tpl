/**
 * @file string_scanner_avx2.h.tpl
 * @brief AVX2-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

// AVX2 intrinsics
#include <immintrin.h>

#define SIMD_VEC_TYPE __m256i
#define SIMD_CHUNK_SIZE 32
#define SIMD_MASK_TYPE uint32_t  // AVX2: 32 bytes → 32-bit mask

// Load unaligned chunk
#define SIMD_LOAD(ptr) _mm256_loadu_si256((__m256i*)(ptr))

// Set all bytes to same value
#define SIMD_SET1(val) _mm256_set1_epi8(val)

// Compare bytes for equality
#define SIMD_CMP_EQ(a, b) _mm256_cmpeq_epi8((a), (b))

// Bitwise OR
#define SIMD_OR(a, b) _mm256_or_si256((a), (b))

// Convert comparison result to bitmask
#define SIMD_MOVEMASK(vec) ((SIMD_MASK_TYPE)_mm256_movemask_epi8(vec))
