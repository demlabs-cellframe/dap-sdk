/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP JSON Native Implementation Team
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_json_string_avx2.c
 * @brief SIMD JSON String Scanner - AVX2 Implementation
 * 
 * AVX2 string scanner - processes 32 bytes at once.
 * 
 * Algorithm:
 * 1. Load 32 bytes into AVX2 register
 * 2. Compare all bytes with '"' in parallel
 * 3. Compare all bytes with '\' in parallel
 * 4. Combine results with OR
 * 5. Find first match with movemask + ctz
 * 
 * Performance: ~32 chars per cycle (vs 1 char/cycle reference)
 * Expected impact: +500-1000% for string scanning
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2026-01-12
 */

#include "dap_common.h"
#include "internal/dap_json_string.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __AVX2__
#include <immintrin.h>

#define LOG_TAG "dap_json_string_avx2"

/* ========================================================================== */
/*                        AVX2 STRING SCANNER                                 */
/* ========================================================================== */

/**
 * @brief AVX2 SIMD string scanner
 * @details Processes 32 bytes at once using AVX2 instructions.
 * 
 * Performance: ~32x faster than character-by-character scanning
 */
bool dap_json_string_scan_avx2(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
)
{
    if (!a_input || !a_out_string || !a_out_end_offset) {
        return false;
    }
    
    if (a_input_len == 0 || a_input[0] != '"') {
        log_it(L_ERROR, "Expected opening quote");
        return false;
    }
    
    const uint32_t l_string_start = 1; // after opening quote
    uint32_t l_pos = l_string_start;
    bool l_has_escape = false;
    
    // AVX2: 32-byte chunks
    const size_t l_chunk_size = 32;
    
    // Prepare comparison vectors
    const __m256i l_quote_vec = _mm256_set1_epi8('"');
    const __m256i l_backslash_vec = _mm256_set1_epi8('\\');
    
    // Process 32-byte chunks
    while (l_pos + l_chunk_size <= a_input_len) {
        // Load 32 bytes
        __m256i l_chunk = _mm256_loadu_si256((const __m256i*)(a_input + l_pos));
        
        // Find quotes: all bytes == '"'
        __m256i l_quote_mask = _mm256_cmpeq_epi8(l_chunk, l_quote_vec);
        
        // Find backslashes: all bytes == '\\'
        __m256i l_backslash_mask = _mm256_cmpeq_epi8(l_chunk, l_backslash_vec);
        
        // Combine: any quote or backslash
        __m256i l_combined = _mm256_or_si256(l_quote_mask, l_backslash_mask);
        
        // Convert to bitmask
        uint32_t l_mask = (uint32_t)_mm256_movemask_epi8(l_combined);
        
        if (l_mask != 0) {
            // Found quote or backslash
            uint32_t l_first_match = __builtin_ctz(l_mask);
            l_pos += l_first_match;
            
            if (a_input[l_pos] == '"') {
                // Found closing quote - done!
                const uint32_t l_string_len = l_pos - l_string_start;
                
                a_out_string->data = (const char*)(a_input + l_string_start);
                a_out_string->length = l_string_len;
                a_out_string->needs_unescape = l_has_escape;
                a_out_string->unescaped_valid = false;
                a_out_string->unescaped = NULL;
                a_out_string->unescaped_length = 0;
                a_out_string->reserved = 0;
                
                *a_out_end_offset = l_pos + 1; // after closing quote
                return true;
            } else if (a_input[l_pos] == '\\') {
                // Found escape sequence
                l_has_escape = true;
                l_pos++; // skip backslash
                if (l_pos >= a_input_len) {
                    log_it(L_ERROR, "Unterminated escape sequence");
                    return false;
                }
                l_pos++; // skip escaped character
            }
        } else {
            // No quote or backslash in this chunk
            l_pos += l_chunk_size;
        }
    }
    
    // Handle remaining bytes (< 32) character-by-character
    while (l_pos < a_input_len) {
        uint8_t l_byte = a_input[l_pos];
        
        if (l_byte == '"') {
            // Found closing quote
            const uint32_t l_string_len = l_pos - l_string_start;
            
            a_out_string->data = (const char*)(a_input + l_string_start);
            a_out_string->length = l_string_len;
            a_out_string->needs_unescape = l_has_escape;
            a_out_string->unescaped_valid = false;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            a_out_string->reserved = 0;
            
            *a_out_end_offset = l_pos + 1;
            return true;
        }
        
        if (l_byte == '\\') {
            l_has_escape = true;
            l_pos++; // skip backslash
            if (l_pos >= a_input_len) {
                log_it(L_ERROR, "Unterminated escape sequence");
                return false;
            }
            // Skip escaped character
        }
        
        l_pos++;
    }
    
    log_it(L_ERROR, "Unterminated string (missing closing quote)");
    return false;
}

#endif /* __AVX2__ */
