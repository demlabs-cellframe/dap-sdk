/**
 * @file string_scanner_sve2.h.tpl
 * @brief ARM SVE2-specific helpers for string scanning
 * 
 * SVE2 features:
 * - All SVE features
 * - Additional instructions for string processing
 * - Histogram operations
 * - Match/search instructions
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

// ARM SVE2 intrinsics (includes SVE)
#include <arm_sve.h>

// SVE2 uses variable vector length (same as SVE)
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

// SVE2 specific: Use match instruction for faster character search
// svmatch_u8 returns predicate where bytes match
#define SIMD_MATCH(pred, vec, val) svmatch_u8(pred, vec, val)

// Convert predicate to bitmask
static inline uint64_t sve2_predicate_to_mask(svbool_t pred) {
    uint64_t mask = 0;
    uint8_t buf[64] = {0};
    
    svst1_u8(pred, buf, svdup_n_u8(1));
    
    for (int i = 0; i < 64 && i < svcntb(); i++) {
        if (buf[i]) mask |= (1ULL << i);
    }
    
    return mask;
}

#define SIMD_PRED_TO_MASK(pred) sve2_predicate_to_mask(pred)
