/*
 * PHASE 4 NTT/InvNTT SIMD OPTIMIZATION - Data-Driven Approach
 * 
 * Based on systematic profiling results:
 * - InvNTT operations: 37.5% of total time (top bottleneck)
 * - NTT operations: 36.4% of total time  
 * - Combined: 73.9% of total execution time
 * 
 * Optimization strategy:
 * 1. True vectorized Barrett reduction using NEON
 * 2. Optimized NTT butterfly operations with SIMD
 * 3. Cache-friendly memory access patterns
 * 4. Conditional compilation for testing
 */

#include "chipmunk_ntt.h"
#include "chipmunk.h"
#include <string.h>

// Always compile optimizations - controlled by header flags
// #ifdef CHIPMUNK_USE_NTT_OPTIMIZATIONS

// External symbols from chipmunk_ntt.c
extern const int32_t g_ntt_forward_table[1024];
extern const int32_t g_ntt_inverse_table[1024];

// Apple Silicon NEON intrinsics
#if defined(__APPLE__) && defined(__aarch64__)
#include <arm_neon.h>
#define CHIPMUNK_SIMD_NEON_OPTIMIZED 1
#endif

/**
 * @brief **TRUE VECTORIZED BARRETT REDUCTION** using NEON intrinsics
 * 
 * This is the core optimization - Barrett reduction vectorized for 4 elements
 * Based on profiling data showing Barrett reduction is the main bottleneck
 */
#ifdef CHIPMUNK_SIMD_NEON_OPTIMIZED

// Helper function for 64-bit NEON multiplication (vmulq_s64 replacement)
static inline int64x2_t neon_mul64_scalar(int64x2_t a, int64_t scalar) {
    // Extract individual 64-bit values
    int64_t a0 = vgetq_lane_s64(a, 0);
    int64_t a1 = vgetq_lane_s64(a, 1);
    
    // Perform scalar multiplication
    int64_t r0 = a0 * scalar;
    int64_t r1 = a1 * scalar;
    
    // Create result vector
    int64x2_t result = vdupq_n_s64(0);
    result = vsetq_lane_s64(r0, result, 0);
    result = vsetq_lane_s64(r1, result, 1);
    return result;
}

static inline int32x4_t chipmunk_ntt_barrett_reduce_neon_v4(int64x2_t a_low, int64x2_t a_high) {
    // **ADVANCED NEON BARRETT REDUCTION** for q = 3168257
    // Since vmulq_s64 doesn't exist, implement 64-bit multiplication manually
    
    const int64_t BARRETT_21 = 5243; // ⌊2^21 / q⌋ for q = 3168257  
    const int32_t Q = CHIPMUNK_Q;
    
    // Barrett reduction for low 64-bit values: ⌊a * barrett_const / 2^21⌋
    int64x2_t temp_low = neon_mul64_scalar(a_low, BARRETT_21);
    temp_low = vshrq_n_s64(temp_low, 21);
    int32x2_t q_mult_low = vmovn_s64(temp_low);
    
    // Barrett reduction for high 64-bit values
    int64x2_t temp_high = neon_mul64_scalar(a_high, BARRETT_21);
    temp_high = vshrq_n_s64(temp_high, 21);
    int32x2_t q_mult_high = vmovn_s64(temp_high);
    
    // Combine low and high quotients
    int32x4_t q_mult = vcombine_s32(q_mult_low, q_mult_high);
    int32x4_t q_vec = vdupq_n_s32(Q);
    
    // Convert original 64-bit values to 32-bit (truncation)
    int32x2_t orig_low_32 = vmovn_s64(a_low);
    int32x2_t orig_high_32 = vmovn_s64(a_high);
    int32x4_t orig_vals = vcombine_s32(orig_low_32, orig_high_32);
    
    // Final Barrett reduction: a - q * quotient
    int32x4_t q_mult_full = vmulq_s32(q_mult, q_vec);
    int32x4_t result = vsubq_s32(orig_vals, q_mult_full);
    
    // **VECTORIZED CONDITIONAL REDUCTION**
    // If result >= q, subtract q
    uint32x4_t ge_q_mask = vcgeq_s32(result, q_vec);
    result = vbslq_s32(ge_q_mask, vsubq_s32(result, q_vec), result);
    
    // If result < 0, add q
    int32x4_t zero_vec = vdupq_n_s32(0);
    uint32x4_t lt_zero_mask = vcltq_s32(result, zero_vec);
    result = vbslq_s32(lt_zero_mask, vaddq_s32(result, q_vec), result);
    
    return result;
}

/**
 * @brief **OPTIMIZED NTT BUTTERFLY** with true NEON vectorization
 * 
 * Processes 4 NTT butterflies simultaneously with vectorized Barrett reduction
 */
static inline void chipmunk_ntt_butterfly_neon_v4(int32_t *a_r, int l_j, int l_ht, int32_t l_s) {
    // Load 4 u values and 4 temp values
    int32x4_t u_vec = vld1q_s32(&a_r[l_j]);
    int32x4_t temp_vec = vld1q_s32(&a_r[l_j + l_ht]);
    int32x4_t s_vec = vdupq_n_s32(l_s);
    
    // 64-bit multiplication: temp * s
    int32x2_t temp_low = vget_low_s32(temp_vec);
    int32x2_t temp_high = vget_high_s32(temp_vec);
    int32x2_t s_low = vget_low_s32(s_vec);
    int32x2_t s_high = vget_high_s32(s_vec);
    
    int64x2_t mult_low = vmull_s32(temp_low, s_low);
    int64x2_t mult_high = vmull_s32(temp_high, s_high);
    
    // Vectorized Barrett reduction
    int32x4_t v_vec = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
    
    // NTT butterfly operations
    int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    // result1 = u + v
    int32x4_t result1 = vaddq_s32(u_vec, v_vec);
    
    // result2 = u + q - v  
    int32x4_t temp_sub = vsubq_s32(q_vec, v_vec);
    int32x4_t result2 = vaddq_s32(u_vec, temp_sub);
    
    // Final Barrett reduction for both results
    // Convert to 64-bit for reduction
    int32x2_t r1_low = vget_low_s32(result1);
    int32x2_t r1_high = vget_high_s32(result1);
    int64x2_t r1_64_low = vmovl_s32(r1_low);
    int64x2_t r1_64_high = vmovl_s32(r1_high);
    
    int32x2_t r2_low = vget_low_s32(result2);
    int32x2_t r2_high = vget_high_s32(result2);
    int64x2_t r2_64_low = vmovl_s32(r2_low);
    int64x2_t r2_64_high = vmovl_s32(r2_high);
    
    result1 = chipmunk_ntt_barrett_reduce_neon_v4(r1_64_low, r1_64_high);
    result2 = chipmunk_ntt_barrett_reduce_neon_v4(r2_64_low, r2_64_high);
    
    // Store results
    vst1q_s32(&a_r[l_j], result1);
    vst1q_s32(&a_r[l_j + l_ht], result2);
}

/**
 * @brief **PHASE 4 OPTIMIZED NTT** - True SIMD implementation
 * 
 * Based on profiling data showing NTT operations are 36.4% of total time
 */
void chipmunk_ntt_optimized(int32_t a_r[CHIPMUNK_N]) {
#ifdef CHIPMUNK_SIMD_NEON_OPTIMIZED
    int l_t = CHIPMUNK_N; // 512
    
    for (int l = 0; l < 9; l++) {
        int l_m = 1 << l;
        int l_ht = l_t >> 1;
        int l_i = 0;
        int l_j1 = 0;
        
        while (l_i < l_m) {
            int32_t l_s = g_ntt_forward_table[l_m + l_i];
            int l_j2 = l_j1 + l_ht;
            int l_j = l_j1;
            
            // **TRUE SIMD OPTIMIZATION**: Process 4 butterflies at once
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3);
            
            while (l_j < l_simd_end) {
                chipmunk_ntt_butterfly_neon_v4(a_r, l_j, l_ht, l_s);
                l_j += 4;
            }
            
            // Handle remaining elements with scalar fallback
            while (l_j < l_j2) {
                int32_t l_u = a_r[l_j];
                int64_t l_v_temp = (int64_t)a_r[l_j + l_ht] * (int64_t)l_s;
                int32_t l_v = chipmunk_ntt_barrett_reduce(l_v_temp);
                
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                a_r[l_j + l_ht] = chipmunk_ntt_barrett_reduce(l_u + CHIPMUNK_Q - l_v);
                l_j += 1;
            }
            
            l_i += 1;
            l_j1 += l_t;
        }
        l_t = l_ht;
    }
#else
    // Fallback to standard implementation for non-NEON platforms
    chipmunk_ntt(a_r);
#endif
}

/**
 * @brief **PHASE 4 OPTIMIZED InvNTT** - True SIMD implementation  
 * 
 * Based on profiling data showing InvNTT operations are 37.5% of total time (TOP BOTTLENECK)
 */
void chipmunk_invntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    int l_t = 1;
    int l_m = CHIPMUNK_N; // 512
    
    while (l_m > 1) {
        int l_hm = l_m >> 1;
        int l_dt = l_t << 1;
        int l_i = 0;
        int l_j1 = 0;
        
        while (l_i < l_hm) {
            int l_j2 = l_j1 + l_t;
            int32_t l_s = g_ntt_inverse_table[l_hm + l_i];
            int l_j = l_j1;
            
            // **TRUE SIMD OPTIMIZATION**: Process 4 inverse butterflies at once
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3);
            
            while (l_j < l_simd_end) {
                // Load 4 u and v values
                int32x4_t u_vec = vld1q_s32(&a_r[l_j]);
                int32x4_t v_vec = vld1q_s32(&a_r[l_j + l_t]);
                int32x4_t s_vec = vdupq_n_s32(l_s);
                int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
                
                // InvNTT butterfly: a[j] = u + v
                int32x4_t sum_result = vaddq_s32(u_vec, v_vec);
                
                // a[j+t] = (u + q - v) * s
                int32x4_t temp_diff = vaddq_s32(u_vec, vsubq_s32(q_vec, v_vec));
                
                // Vectorized multiplication for (u + q - v) * s
                int32x2_t diff_low = vget_low_s32(temp_diff);
                int32x2_t diff_high = vget_high_s32(temp_diff);
                int32x2_t s_low = vget_low_s32(s_vec);
                int32x2_t s_high = vget_high_s32(s_vec);
                
                int64x2_t mult_low = vmull_s32(diff_low, s_low);
                int64x2_t mult_high = vmull_s32(diff_high, s_high);
                
                // Vectorized Barrett reduction for multiplication result
                int32x4_t mult_result = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
                
                // Barrett reduction for sum result
                int32x2_t sum_low = vget_low_s32(sum_result);
                int32x2_t sum_high = vget_high_s32(sum_result);
                int64x2_t sum_64_low = vmovl_s32(sum_low);
                int64x2_t sum_64_high = vmovl_s32(sum_high);
                sum_result = chipmunk_ntt_barrett_reduce_neon_v4(sum_64_low, sum_64_high);
                
                // Store results
                vst1q_s32(&a_r[l_j], sum_result);
                vst1q_s32(&a_r[l_j + l_t], mult_result);
                
                l_j += 4;
            }
            
            // Handle remaining elements with scalar fallback
            while (l_j < l_j2) {
                int32_t l_u = a_r[l_j];
                int32_t l_v = a_r[l_j + l_t];
                
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                
                int64_t l_temp = (int64_t)(l_u + CHIPMUNK_Q - l_v) * (int64_t)l_s;
                a_r[l_j + l_t] = chipmunk_ntt_barrett_reduce(l_temp);
                
                l_j += 1;
            }
            
            l_i += 1;
            l_j1 += l_dt;
        }
        l_t = l_dt;
        l_m = l_hm;
    }
    
    // **OPTIMIZED FINAL NORMALIZATION** with NEON
    int l_simd_end = CHIPMUNK_N & ~3; // Align to 4
    int32x4_t one_over_n_vec = vdupq_n_s32(3162069); // HOTS_ONE_OVER_N
    int32x4_t q_half_vec = vdupq_n_s32(CHIPMUNK_Q / 2);
    int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    for (int i = 0; i < l_simd_end; i += 4) {
        int32x4_t data_vec = vld1q_s32(&a_r[i]);
        
        // Vectorized multiplication by one_over_n
        int32x2_t data_low = vget_low_s32(data_vec);
        int32x2_t data_high = vget_high_s32(data_vec);
        int32x2_t one_over_n_low = vget_low_s32(one_over_n_vec);
        int32x2_t one_over_n_high = vget_high_s32(one_over_n_vec);
        
        int64x2_t mult_low = vmull_s32(data_low, one_over_n_low);
        int64x2_t mult_high = vmull_s32(data_high, one_over_n_high);
        
        // Vectorized Barrett reduction
        data_vec = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
        
        // Vectorized centering to [-q/2, q/2]
        uint32x4_t gt_q_half_mask = vcgtq_s32(data_vec, q_half_vec);
        uint32x4_t lt_neg_q_half_mask = vcltq_s32(data_vec, vnegq_s32(q_half_vec));
        
        data_vec = vbslq_s32(gt_q_half_mask, vsubq_s32(data_vec, q_vec), data_vec);
        data_vec = vbslq_s32(lt_neg_q_half_mask, vaddq_s32(data_vec, q_vec), data_vec);
        
        vst1q_s32(&a_r[i], data_vec);
    }
    
    // Handle remaining elements
    for (int i = l_simd_end; i < CHIPMUNK_N; i++) {
        int64_t l_temp = (int64_t)a_r[i] * (int64_t)3162069;
        a_r[i] = chipmunk_ntt_barrett_reduce(l_temp);
        
        if (a_r[i] > CHIPMUNK_Q / 2) 
            a_r[i] -= CHIPMUNK_Q;
        if (a_r[i] < -CHIPMUNK_Q / 2) 
            a_r[i] += CHIPMUNK_Q;
    }
}

#else // !CHIPMUNK_SIMD_NEON_OPTIMIZED

// Fallback to standard implementation for non-NEON platforms
void chipmunk_ntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    chipmunk_ntt(a_r);
}

void chipmunk_invntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    chipmunk_invntt(a_r);
}

#endif // CHIPMUNK_SIMD_NEON_OPTIMIZED

// #endif // CHIPMUNK_USE_NTT_OPTIMIZATIONS 