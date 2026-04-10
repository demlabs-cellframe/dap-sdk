// ============================================================================
// ARM SVE2 Shared SIMD Primitives (scalable vectors, 128-2048 bits)
//
// SVE2 extends SVE with: EOR3 (3-way XOR), BCAX (bit clear and xor),
// MATCH/NMATCH for byte-level search, and more.
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
#define VEC_MATCH_U8(pg, a, b)     svmatch_u8(pg, a, b)

// === 64-bit (svuint64_t) operations =========================================

#define VEC_LOAD_U64_Z(pg, p)      svld1_u64(pg, (const uint64_t *)(p))
#define VEC_STORE_U64(pg, p, v)    svst1_u64(pg, (uint64_t *)(p), v)
#define VEC_SET1_U64(x)            svdup_u64(x)
#define VEC_XOR_U64_Z(pg, a, b)    sveor_u64_z(pg, a, b)
#define VEC_ADD_U64_Z(pg, a, b)    svadd_u64_z(pg, a, b)
#define VEC_SUB_U64_Z(pg, a, b)    svsub_u64_z(pg, a, b)
#define VEC_SHL_U64_Z(pg, a, n)    svlsl_n_u64_z(pg, a, n)
#define VEC_SHR_U64_Z(pg, a, n)    svlsr_n_u64_z(pg, a, n)

// === SVE2-specific multi-operand ops ========================================

#define VEC_XOR3_U64(a, b, c)      sveor3_u64(a, b, c)
#define VEC_BCAX_U64(a, b, c)      svbcax_u64(a, c, b)

// === Permutation (table lookup) =============================================

#define VEC_TBL_U64(a, idx)        svtbl_u64(a, idx)
#define VEC_ZIP1_U64(a, b)         svzip1_u64(a, b)
#define VEC_ZIP2_U64(a, b)         svzip2_u64(a, b)
#define VEC_UZP1_U64(a, b)         svuzp1_u64(a, b)
#define VEC_UZP2_U64(a, b)         svuzp2_u64(a, b)
#define VEC_EXT_U64(a, b, n)       svext_u64(a, b, n)
