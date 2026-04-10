/**
 * @file string_scanner_sve2.h.tpl
 * @brief ARM SVE2-specific helpers for string scanning
 * 
 * SVE2 features:
 * - All SVE features (variable vector length, predicates)
 * - Additional instructions for string processing
 * - svmatch instruction for character search
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
#define SIMD_MATCH(pred, vec, val) VEC_MATCH_U8(pred, vec, svdup_n_u8((uint8_t)(val)))
#define SIMD_PRED_ANY(pred)      VEC_PRED_ANY(VEC_PTRUE_8(), pred)

static inline uint32_t sve2_find_first_true(svbool_t pred) {
    svbool_t l_first = svbrka_b_z(svptrue_b8(), pred);
    return (uint32_t)svcntp_b8(svptrue_b8(), svbic_b_z(svptrue_b8(), l_first, pred));
}

#define SIMD_PRED_FIRST_TRUE(pred) sve2_find_first_true(pred)
