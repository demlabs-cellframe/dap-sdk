/**
 * @file string_scanner_sse2.h.tpl
 * @brief SSE2-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

// SSE2 intrinsics
#include <emmintrin.h>

#define SIMD_VEC_TYPE __m128i
#define SIMD_CHUNK_SIZE 16

// Load unaligned chunk
#define SIMD_LOAD(ptr) _mm_loadu_si128((__m128i*)(ptr))

// Set all bytes to same value
#define SIMD_SET1(val) _mm_set1_epi8(val)

// Compare bytes for equality
#define SIMD_CMP_EQ(a, b) _mm_cmpeq_epi8((a), (b))

// Bitwise OR
#define SIMD_OR(a, b) _mm_or_si128((a), (b))

// Convert comparison result to bitmask
#define SIMD_MOVEMASK(vec) _mm_movemask_epi8(vec)
