/**
 * @file string_scanner_avx512.h.tpl
 * @brief AVX-512-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

{{#include PRIM_LIB}}

#define SIMD_VEC_TYPE  VEC_T
#define SIMD_CHUNK_SIZE VEC_LANES_8
#define SIMD_MASK_TYPE __mmask64

#define SIMD_LOAD(ptr)         VEC_LOAD(ptr)
#define SIMD_SET1(val)         VEC_SET1_8(val)
#define SIMD_CMP_EQ_MASK(a, b) VEC_CMPEQ_8_MASK(a, b)
#define SIMD_OR_MASK(a, b)     ((a) | (b))
#define SIMD_GET_MASK(mask)    (mask)
