#ifndef DAP_JSON_STAGE1_SVE_ARCH_H
#define DAP_JSON_STAGE1_SVE_ARCH_H

// ============================================================================
// ARM SVE Predicate to Bitmask Conversion
// ============================================================================
//
// SVE predicates (svbool_t) represent boolean masks for variable-length vectors.
// We need to convert them to a compact bitmask (uint64_t) for bitmap processing.
//
// CHALLENGE: SVE vectors are runtime-sized (128-2048 bits), so we can't
// hardcode the conversion logic.
//
// SOLUTION: Use SVE's svptest_any() for bit extraction in a loop.
//           This is not as fast as x86 _mm_movemask_epi8, but it's portable
//           and works for any SVE vector length.
//
// OPTIMIZATION: For common SVE vector lengths (128, 256, 512 bits),
//               the compiler will likely unroll the loop.
// ============================================================================

static inline uint64_t dap_sve_movemask_u8(svbool_t a_pred)
{
    // Get SVE vector length in bytes (runtime value)
    size_t vl_bytes = svcntb();
    
    // Limit to 64 bits (we use uint64_t for bitmask)
    if (vl_bytes > 64) vl_bytes = 64;
    
    uint64_t result = 0;
    
    // Extract each bit from predicate
    // SVE approach: Create predicates for each lane and test
    for (size_t i = 0; i < vl_bytes; i++) {
        // Create a predicate that selects only lane 'i'
        svbool_t lane_pred = svcmpeq_n_u64(svptrue_b64(), svindex_u64(0, 1), (uint64_t)i);
        
        // Test if lane 'i' is active in our predicate
        // svcntp counts active lanes - will be 1 if lane is set, 0 otherwise
        if (svptest_any(lane_pred, a_pred)) {
            result |= (1ULL << i);
        }
    }
    
    return result;
}

static inline uint64_t dap_sve2_movemask_u8(svbool_t a_pred)
{
    // SVE2 has same predicate model as SVE, reuse the same implementation
    return dap_sve_movemask_u8(a_pred);
}

#endif // DAP_JSON_STAGE1_SVE_ARCH_H

