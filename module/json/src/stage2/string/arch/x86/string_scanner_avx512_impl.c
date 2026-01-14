/**
 * @file string_scanner_avx512_impl.c.tpl
 * @brief AVX-512 specific implementation fragment (mask-based API)
 * 
 * This fragment replaces the SIMD loop in the main template for AVX-512
 * to use the mask-based comparison API instead of vector-based.
 */

// SIMD main loop - AVX-512 mask-based implementation
while (l_pos + SIMD_CHUNK_SIZE <= a_input_len) {
    // Load chunk
    SIMD_VEC_TYPE l_chunk = SIMD_LOAD(a_input + l_pos);
    
    // Compare for quotes and backslashes (returns masks, not vectors!)
    SIMD_MASK_TYPE l_quotes_mask = SIMD_CMP_EQ_MASK(l_chunk, l_quote_vec);
    SIMD_MASK_TYPE l_backslashes_mask = SIMD_CMP_EQ_MASK(l_chunk, l_backslash_vec);
    
    // Combine masks with bitwise OR
    SIMD_MASK_TYPE l_combined_mask = SIMD_OR_MASK(l_quotes_mask, l_backslashes_mask);
    
    if (l_combined_mask != 0) {
        // Found quote or backslash - find first match
        uint32_t l_first_idx = __builtin_ctzll(l_combined_mask);
        
        // Check if it's a quote (check mask bit)
        if ((l_quotes_mask >> l_first_idx) & 1) {
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
