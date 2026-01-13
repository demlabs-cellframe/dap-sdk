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

// ARM SVE intrinsics
#include <arm_sve.h>

// SVE uses variable vector length
#define SIMD_VEC_TYPE svuint8_t
#define SIMD_PRED_TYPE svbool_t
#define SIMD_CHUNK_SIZE (svcntb())  // Runtime vector length in bytes

// Load unaligned chunk with predicate
#define SIMD_LOAD(ptr, pred) svld1_u8(pred, (const uint8_t*)(ptr))

// Set all bytes to same value
#define SIMD_SET1(val) svdup_n_u8(val)

// Compare bytes for equality (predicated)
#define SIMD_CMP_EQ(pred, a, b) svcmpeq_u8(pred, (a), (b))

// Bitwise OR for predicates
#define SIMD_OR_PRED(a, b) svorr_b_z(svptrue_b8(), (a), (b))

// Get all-true predicate for current vector length
#define SIMD_PTRUE() svptrue_b8()

// Check if any predicate bit is true
#define SIMD_PRED_ANY(pred) svptest_any(svptrue_b8(), (pred))

// Find first true bit in predicate (returns byte index)
static inline uint32_t sve_find_first_true(svbool_t pred) {
    // svbrka_b_z creates mask with first true bit and all before it false
    svbool_t l_first = svbrka_b_z(svptrue_b8(), pred);
    
    // Count false bits before first true
    return (uint32_t)svcntp_b8(svptrue_b8(), svbic_b_z(svptrue_b8(), l_first, pred));
}

#define SIMD_PRED_FIRST_TRUE(pred) sve_find_first_true(pred)
