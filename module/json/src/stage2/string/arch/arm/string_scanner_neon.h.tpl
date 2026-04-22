/**
 * @file string_scanner_neon.h.tpl
 * @brief ARM NEON-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

{{#include PRIM_LIB}}

#define SIMD_VEC_TYPE uint8x16_t
#define SIMD_CHUNK_SIZE VEC_LANES_8
#define SIMD_MASK_TYPE uint16_t

#define SIMD_LOAD(ptr)      VEC_LOAD_U8(ptr)
#define SIMD_SET1(val)      VEC_SET1_U8(val)
#define SIMD_CMP_EQ(a, b)   VEC_CMPEQ_U8(a, b)
#define SIMD_OR(a, b)        VEC_OR_U8(a, b)
#define SIMD_MOVEMASK(vec)   VEC_MOVEMASK_U8(vec)
