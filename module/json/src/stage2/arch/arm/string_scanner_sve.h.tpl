/**
 * @file string_scanner_sve.h.tpl
 * @brief ARM SVE-specific helpers for string scanning
 * 
 * SVE features:
 * - Variable vector length (128-2048 bits at runtime)
 * - Predicate-based operations
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

// Convert predicate to bitmask (first N bits)
// For SVE we need to extract active lanes
static inline uint64_t sve_predicate_to_mask(svbool_t pred) {
    // Convert predicate to uint64 bitmask (max 64 bytes per vector)
    uint64_t mask = 0;
    uint8_t buf[64] = {0};
    
    // Store predicate as bytes (1 where true, 0 where false)
    svst1_u8(pred, buf, svdup_n_u8(1));
    
    // Pack into bitmask
    for (int i = 0; i < 64 && i < svcntb(); i++) {
        if (buf[i]) mask |= (1ULL << i);
    }
    
    return mask;
}

#define SIMD_PRED_TO_MASK(pred) sve_predicate_to_mask(pred)
