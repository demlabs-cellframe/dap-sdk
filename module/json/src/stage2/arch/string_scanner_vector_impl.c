/**
 * @file string_scanner_vector_impl.c.tpl
 * @brief Vector-based SIMD implementation fragment
 * 
 * This fragment is used for SSE2, AVX2, and NEON architectures
 * that use vector comparison API (not mask-based like AVX-512).
 */

// SIMD main loop - vector-based implementation
while (l_pos + SIMD_CHUNK_SIZE <= a_input_len) {
    // Load chunk
    SIMD_VEC_TYPE l_chunk = SIMD_LOAD(a_input + l_pos);
    
    // Compare for quotes and backslashes in parallel
    SIMD_VEC_TYPE l_quotes = SIMD_CMP_EQ(l_chunk, l_quote_vec);
    SIMD_VEC_TYPE l_backslashes = SIMD_CMP_EQ(l_chunk, l_backslash_vec);
    
    // Combine results
    SIMD_VEC_TYPE l_combined = SIMD_OR(l_quotes, l_backslashes);
    
    // Convert to bitmask
    uint32_t l_mask = SIMD_MOVEMASK(l_combined);
    
    if (l_mask != 0) {
        // Found quote or backslash - find first match
        uint32_t l_first_idx = __builtin_ctz(l_mask);
        
        // Check if it's a quote (re-check with scalar to distinguish)
        if (a_input[l_pos + l_first_idx] == '"') {
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
    
    // No matches - advance by chunk size
    l_pos += SIMD_CHUNK_SIZE;
}
