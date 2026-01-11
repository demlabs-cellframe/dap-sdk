// ARM SVE movemask equivalent
// SVE provides native predicate operations, making this much simpler than NEON

static inline uint64_t dap_sve_movemask_u8(svuint8_t a_input)
{
    // SVE approach: Create a predicate from comparison with 0x80
    // This gives us a boolean mask of MSB bits
    
    // Get active predicate for all lanes
    svbool_t pg = svptrue_b8();
    
    // Compare MSB (>= 0x80 means MSB is set)
    svbool_t msb_pred = svcmpge_u8(pg, a_input, svdup_u8(0x80));
    
    // Convert predicate to uint64_t bitmask
    // Each bit in the result corresponds to one byte lane
    // NOTE: This is platform-dependent - SVE vector length varies
    // We need to handle variable-length vectors properly
    
    // SVE provides direct conversion: svptest returns bits
    // But we need actual bit extraction for compatibility
    // Use ptest to convert predicate to scalar bitmask
    
    // ALTERNATIVE: Use byte extraction in a loop
    // This is guaranteed correct but not optimal
    uint64_t result = 0;
    svuint8_t shifted = svlsr_n_u8_z(pg, a_input, 7); // Extract MSB to LSB
    
    // Get vector length in bytes
    size_t vl_bytes = svcntb();
    
    // Extract each byte's LSB into result
    for (size_t i = 0; i < vl_bytes && i < 64; i++) {
        uint8_t bit = svlasta_u8(svcmpne_u8(pg, svindex_u8(0, 1), i), shifted);
        result |= ((uint64_t)bit) << i;
    }
    
    return result;
}

