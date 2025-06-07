/*
 * PHASE 4 NTT/InvNTT SIMD OPTIMIZATION - Header File
 * 
 * Based on systematic profiling showing NTT/InvNTT operations 
 * consume 73.9% of total execution time
 */

#ifndef _CHIPMUNK_NTT_OPTIMIZED_H_
#define _CHIPMUNK_NTT_OPTIMIZED_H_

#include "chipmunk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// **PHASE 4 OPTIMIZATION FLAGS**
// Enable this flag to use optimized NTT/InvNTT implementations
#ifndef CHIPMUNK_USE_NTT_OPTIMIZATIONS
#define CHIPMUNK_USE_NTT_OPTIMIZATIONS 1  // Enable for performance comparison
#endif

#ifdef CHIPMUNK_USE_NTT_OPTIMIZATIONS

/**
 * @brief **PHASE 4 OPTIMIZED NTT** - True SIMD implementation
 * 
 * Based on profiling data showing NTT operations are 36.4% of total time
 * Uses NEON intrinsics for vectorized Barrett reduction and butterfly operations
 * 
 * @param a_r Polynomial coefficients array to transform to NTT domain
 */
void chipmunk_ntt_optimized(int32_t a_r[CHIPMUNK_N]);

/**
 * @brief **PHASE 4 OPTIMIZED InvNTT** - True SIMD implementation  
 * 
 * Based on profiling data showing InvNTT operations are 37.5% of total time (TOP BOTTLENECK)
 * Uses NEON intrinsics for vectorized Barrett reduction and inverse butterfly operations
 * 
 * @param a_r Polynomial coefficients array to transform from NTT domain
 */
void chipmunk_invntt_optimized(int32_t a_r[CHIPMUNK_N]);

// **WRAPPER FUNCTIONS** - Use optimized versions when enabled
#define chipmunk_ntt_phase4(a_r)    chipmunk_ntt_optimized(a_r)
#define chipmunk_invntt_phase4(a_r) chipmunk_invntt_optimized(a_r)

#else

// Fallback to standard implementations when optimizations disabled
#define chipmunk_ntt_phase4(a_r)    chipmunk_ntt(a_r)
#define chipmunk_invntt_phase4(a_r) chipmunk_invntt(a_r)

#endif // CHIPMUNK_USE_NTT_OPTIMIZATIONS

// **PERFORMANCE TESTING FUNCTIONS**

/**
 * @brief Test Phase 4 NTT optimizations performance
 * 
 * Compares standard vs optimized NTT/InvNTT implementations
 * Measures execution time and validates correctness
 * 
 * @return 0 on success with performance improvement, negative on failure
 */
int test_phase4_ntt_optimization_performance(void);

/**
 * @brief Validate Phase 4 NTT optimizations correctness
 * 
 * Ensures optimized implementations produce identical results to standard
 * 
 * @return 0 if results are identical, negative if different
 */
int test_phase4_ntt_optimization_correctness(void);

#ifdef __cplusplus
}
#endif

#endif // _CHIPMUNK_NTT_OPTIMIZED_H_ 