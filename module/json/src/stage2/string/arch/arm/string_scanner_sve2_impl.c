/**
 * @file string_scanner_sve2_impl.c
 * @brief ARM SVE2 specific implementation fragment (with svmatch optimization)
 * 
 * SVE2 adds the svmatch instruction for faster character matching.
 * This is more efficient than separate compare + OR operations.
 * Work with predicates directly - no bitmask conversion!
 */

// Get runtime vector length
uint32_t l_vlen = svcntb(); // Vector length in bytes (runtime)

// SIMD main loop - SVE2 optimized implementation with svmatch
while (l_pos + l_vlen <= a_input_len) {
    // Create all-true predicate for this iteration
    SIMD_PRED_TYPE l_pred = SIMD_PTRUE();
    
    // Load chunk with predicate
    SIMD_VEC_TYPE l_chunk = SIMD_LOAD(a_input + l_pos, l_pred);
    
    // SVE2: Use match instructions for faster search (returns predicates)
    SIMD_PRED_TYPE l_quotes_pred = SIMD_MATCH(l_pred, l_chunk, '"');
    SIMD_PRED_TYPE l_backslashes_pred = SIMD_MATCH(l_pred, l_chunk, '\\');
    
    // Combine predicates with OR
    SIMD_PRED_TYPE l_combined_pred = SIMD_OR_PRED(l_quotes_pred, l_backslashes_pred);
    
    // Check if any match found (no bitmask conversion!)
    if (SIMD_PRED_ANY(l_combined_pred)) {
        // Find first true bit directly from predicate
        uint32_t l_first_idx = SIMD_PRED_FIRST_TRUE(l_combined_pred);
        
        // Check if it's a quote by testing the quote predicate
        if (svptest_first(svptrue_b8(), l_quotes_pred)) {
            // Found closing quote!
            a_out_string->data = (const char*)a_input;
            a_out_string->length = l_pos + l_first_idx;
            a_out_string->needs_unescape = l_has_escapes;
            a_out_string->unescaped_valid = 0;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            *a_out_end_offset = l_pos + l_first_idx;
            return true;
        } else {
            // It's a backslash - escape sequence
            l_has_escapes = true;
            l_pos += l_first_idx + 2; // Skip backslash and escaped char
            
            if (l_pos > a_input_len) {
                log_it(L_ERROR, "Unexpected end after escape at offset %u", l_pos - 2);
                return false;
            }
            continue;
        }
    }
    
    // No matches - advance by vector length
    l_pos += l_vlen;
}
