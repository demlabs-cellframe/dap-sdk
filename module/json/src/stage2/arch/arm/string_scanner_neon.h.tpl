/**
 * @file string_scanner_neon.h.tpl
 * @brief ARM NEON-specific helpers for string scanning
 * 
 * TEMPLATE FRAGMENT - included by parent template
 */

// ARM NEON intrinsics
#include <arm_neon.h>

#define SIMD_VEC_TYPE uint8x16_t
#define SIMD_CHUNK_SIZE 16

// Load unaligned chunk
#define SIMD_LOAD(ptr) vld1q_u8((const uint8_t*)(ptr))

// Set all bytes to same value
#define SIMD_SET1(val) vmovq_n_u8(val)

// Compare bytes for equality
#define SIMD_CMP_EQ(a, b) vceqq_u8((a), (b))

// Bitwise OR
#define SIMD_OR(a, b) vorrq_u8((a), (b))

// ARM NEON movemask emulation (optimized)
static inline uint16_t neon_movemask_u8(uint8x16_t a_vec) {
    // Shift MSB to position
    uint8x16_t l_shifted = vshrq_n_u8(a_vec, 7);
    
    // Horizontal reduction using pairwise operations
    static const uint8_t l_weights[16] = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t l_weight_vec = vld1q_u8(l_weights);
    uint8x16_t l_weighted = vmulq_u8(l_shifted, l_weight_vec);
    
    // Sum each 8-byte lane
    uint8x8_t l_low = vget_low_u8(l_weighted);
    uint8x8_t l_high = vget_high_u8(l_weighted);
    
    uint8_t l_mask_low = vaddv_u8(l_low);
    uint8_t l_mask_high = vaddv_u8(l_high);
    
    return (uint16_t)l_mask_low | ((uint16_t)l_mask_high << 8);
}

#define SIMD_MOVEMASK(vec) neon_movemask_u8(vec)
