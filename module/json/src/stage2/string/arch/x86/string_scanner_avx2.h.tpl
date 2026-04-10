/**
 * @file string_scanner_avx2.h.tpl
 * @brief AVX2-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

{{#include PRIM_LIB}}

#define SIMD_VEC_TYPE  VEC_T
#define SIMD_CHUNK_SIZE VEC_LANES_8
#define SIMD_MASK_TYPE uint32_t

#define SIMD_LOAD(ptr)      VEC_LOAD(ptr)
#define SIMD_SET1(val)      VEC_SET1_8(val)
#define SIMD_CMP_EQ(a, b)   VEC_CMPEQ_8(a, b)
#define SIMD_OR(a, b)        VEC_OR(a, b)
#define SIMD_MOVEMASK(vec)   ((SIMD_MASK_TYPE)VEC_MOVEMASK_8(vec))
