/**
 * @file string_scanner_sve.h.tpl
 * @brief ARM SVE-specific helpers for string scanning
 * 
 * SVE features:
 * - Variable vector length (128-2048 bits at runtime)
 * - Predicate-based operations (no fixed bitmask!)
 * - No fixed SIMD_CHUNK_SIZE (determined at runtime)
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

{{#include PRIM_LIB}}

#define SIMD_VEC_TYPE svuint8_t
#define SIMD_PRED_TYPE svbool_t
#define SIMD_CHUNK_SIZE (svcntb())

#define SIMD_LOAD(ptr, pred)     VEC_LOAD_U8_Z(pred, ptr)
#define SIMD_SET1(val)           VEC_SET1_U8(val)
#define SIMD_CMP_EQ(pred, a, b) VEC_CMPEQ_U8(pred, a, b)
#define SIMD_OR_PRED(a, b)       VEC_OR_PRED(a, b)
#define SIMD_PTRUE()             VEC_PTRUE_8()
#define SIMD_PRED_ANY(pred)      VEC_PRED_ANY(VEC_PTRUE_8(), pred)

static inline uint32_t sve_find_first_true(svbool_t pred) {
    svbool_t l_first = svbrka_b_z(svptrue_b8(), pred);
    return (uint32_t)svcntp_b8(svptrue_b8(), svbic_b_z(svptrue_b8(), l_first, pred));
}

#define SIMD_PRED_FIRST_TRUE(pred) sve_find_first_true(pred)
