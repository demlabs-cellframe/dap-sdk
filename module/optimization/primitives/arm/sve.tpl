// ============================================================================
// ARM SVE Shared SIMD Primitives (scalable vectors, 128-2048 bits)
//
// SVE uses predicated operations. All ops require a governing predicate.
// Unlike NEON/x86, vector length is determined at runtime via svcntb() etc.
// ============================================================================

#include <arm_sve.h>

// === Predicate helpers ======================================================

#define VEC_PTRUE_8()    svptrue_b8()
#define VEC_PTRUE_16()   svptrue_b16()
#define VEC_PTRUE_32()   svptrue_b32()
#define VEC_PTRUE_64()   svptrue_b64()

#define VEC_WHILELT_8(a, b)   svwhilelt_b8(a, b)
#define VEC_WHILELT_64(a, b)  svwhilelt_b64(a, b)

// === 8-bit (svuint8_t) operations ===========================================

#define VEC_LOAD_U8_Z(pg, p)       svld1_u8(pg, (const uint8_t *)(p))
#define VEC_STORE_U8(pg, p, v)     svst1_u8(pg, (uint8_t *)(p), v)
#define VEC_SET1_U8(x)             svdup_n_u8(x)
#define VEC_CMPEQ_U8(pg, a, b)     svcmpeq_u8(pg, a, b)
#define VEC_OR_PRED(a, b)          svorr_b_z(svptrue_b8(), a, b)
#define VEC_PRED_ANY(pg, pred)     svptest_any(pg, pred)

// === 64-bit (svuint64_t) operations =========================================

#define VEC_LOAD_U64_Z(pg, p)      svld1_u64(pg, (const uint64_t *)(p))
#define VEC_STORE_U64(pg, p, v)    svst1_u64(pg, (uint64_t *)(p), v)
#define VEC_SET1_U64(x)            svdup_u64(x)
#define VEC_XOR_U64_Z(pg, a, b)    sveor_u64_z(pg, a, b)
#define VEC_ADD_U64_Z(pg, a, b)    svadd_u64_z(pg, a, b)
#define VEC_SUB_U64_Z(pg, a, b)    svsub_u64_z(pg, a, b)
#define VEC_SHL_U64_Z(pg, a, n)    svlsl_n_u64_z(pg, a, n)
#define VEC_SHR_U64_Z(pg, a, n)    svlsr_n_u64_z(pg, a, n)
