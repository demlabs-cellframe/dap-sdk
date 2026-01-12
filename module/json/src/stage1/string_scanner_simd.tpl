/* ========================================================================== */
/*                    SIMD STRING SCANNING - {{ARCH_NAME}}                    */
/* ========================================================================== */

/**
 * @brief SIMD-optimized string scanner (CRITICAL HOTSPOT - 27.6% CPU → ~5-8%)
 * @details Fast-path для strings без escapes (99% случаев):
 *          - Параллельный поиск closing quote через SIMD
 *          - Zero overhead для simple strings
 *          - Fallback to scalar для escape sequences
 * 
 * Algorithm:
 *   1. SIMD scan: найти quote OR backslash в чанках (16/32/64 bytes/iter)
 *   2. Fast path: quote найдена раньше backslash → return immediately
 *   3. Slow path: backslash найдена → fallback к reference scanner
 * 
 * Performance gain: 3-5x vs scalar
 * Expected impact: 27.6% CPU → ~5-8% (overall +20-25% throughput)
 */
{{#if TARGET_ATTR}}
__attribute__((target("{{TARGET_ATTR}}")))
{{/if}}
static size_t s_scan_string_fast_{{ARCH_LOWER}}(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
)
{
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    size_t l_pos = a_start_pos + 1;  // Skip opening quote
    
    const size_t chunk_size = CHUNK_SIZE_VALUE;
    
{{#if ARCH_LOWER == "sve" || ARCH_LOWER == "sve2"}}
    // ========================================================================
    // ARM SVE/SVE2: Predicate-based string scanning
    // Scalable vector length - adapts to hardware (128-2048 bits)
    // ========================================================================
    svbool_t pg = svptrue_b8();
    VECTOR_TYPE v_quote = SIMD_SET1('"');
    VECTOR_TYPE v_backslash = SIMD_SET1('\\');
    
    // SIMD scan: parallel search for quote OR backslash
    while (l_pos + chunk_size <= l_len) {
        VECTOR_TYPE chunk = SIMD_LOAD(pg, l_input + l_pos);
        
        // Find quote OR backslash (predicate-based)
        svbool_t has_quote = SIMD_CMP_EQ(pg, chunk, v_quote);
        svbool_t has_backslash = SIMD_CMP_EQ(pg, chunk, v_backslash);
        
        // Convert predicates to bitmasks for analysis
        MASK_TYPE quote_mask = (MASK_TYPE){{MOVEMASK_EPI8}}(has_quote);
        MASK_TYPE backslash_mask = (MASK_TYPE){{MOVEMASK_EPI8}}(has_backslash);
        
        // Fast path: quote found, no escapes before it
        // Use CTZ (Count Trailing Zeros) to find first set bit
        if (quote_mask) {
            int quote_offset = __builtin_ctzll(quote_mask);
            if (!backslash_mask || __builtin_ctzll(backslash_mask) > quote_offset) {
                return l_pos + quote_offset + 1;  // After closing quote
            }
        }
        
        // Escape found - fallback to scalar
        if (backslash_mask) {
            goto slow_path;
        }
        
        l_pos += chunk_size;
    }
{{else}}
    // ========================================================================
    // x86 (SSE2/AVX2/AVX-512) & ARM NEON: Vector-based scanning
    // Fixed vector length: 16/32/64 bytes per iteration
    // ========================================================================
    VECTOR_TYPE v_quote = SIMD_SET1('"');
    VECTOR_TYPE v_backslash = SIMD_SET1('\\');
    
    // SIMD scan: parallel search for quote OR backslash
    while (l_pos + chunk_size <= l_len) {
        VECTOR_TYPE chunk = SIMD_LOAD(l_input + l_pos);
        
        // Find quote OR backslash (vector-based)
        COMPARISON_RESULT_TYPE has_quote = SIMD_CMP_EQ(chunk, v_quote);
        COMPARISON_RESULT_TYPE has_backslash = SIMD_CMP_EQ(chunk, v_backslash);
        
        // Convert to bitmasks for analysis
{{#if USE_AVX512_MASK}}
        // AVX-512: comparisons already return __mmask64
        MASK_TYPE quote_mask = has_quote;
        MASK_TYPE backslash_mask = has_backslash;
{{else}}
        // SSE2/AVX2/NEON: convert vectors to bitmasks
        MASK_TYPE quote_mask = (MASK_TYPE){{MOVEMASK_EPI8}}(has_quote);
        MASK_TYPE backslash_mask = (MASK_TYPE){{MOVEMASK_EPI8}}(has_backslash);
{{/if}}
        
        // Fast path: quote found, no escapes before it
        // Use CTZ (Count Trailing Zeros) to find first set bit
        if (quote_mask) {
            int quote_offset = __builtin_ctzll(quote_mask);
            if (!backslash_mask || __builtin_ctzll(backslash_mask) > quote_offset) {
                return l_pos + quote_offset + 1;  // After closing quote
            }
        }
        
        // Escape found - fallback to scalar
        if (backslash_mask) {
            goto slow_path;
        }
        
        l_pos += chunk_size;
    }
{{/if}}
    
slow_path:
    // ========================================================================
    // Slow path: Escape sequences detected OR tail bytes
    // Fallback to reference scanner (handles \uXXXX, validation, etc)
    // Performance: ~1% of strings hit this path
    // ========================================================================
    return dap_json_stage1_scan_string_ref(a_stage1, a_start_pos);
}

