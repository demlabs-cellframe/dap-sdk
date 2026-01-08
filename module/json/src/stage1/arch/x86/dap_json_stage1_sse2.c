/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_json_stage1_sse2.c
 * @brief Stage 1 JSON tokenization - SSE2 (x86/x64) implementation
 * @details SIMD-optimized structural + value tokenization using SSE2 (16 bytes/iteration)
 * 
 * Architecture: Hybrid SIMD+scalar approach
 * - SIMD: Fast detection of token boundaries (structural chars, strings, numbers, literals)
 * - Scalar: Precise validation (UTF-8, number parsing, literal matching)
 * 
 * SSE2 specific:
 * - Chunk size: 16 bytes
 * - Intrinsics: _mm_* (128-bit)
 * - Available on: All x86-64 CPUs (baseline)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <emmintrin.h>  // SSE2

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_sse2"

// SSE2 chunk size
#define SSE2_CHUNK_SIZE 16

/* ========================================================================== */
/*                        SIMD PRIMITIVES - SSE2                              */
/* ========================================================================== */

/**
 * @brief Classify bytes as structural characters using SSE2
 * @details Finds {, }, [, ], :, , in 16 bytes
 */
static inline __m128i s_classify_structural_sse2(const __m128i a_chunk)
{
    const __m128i l_open_brace   = _mm_set1_epi8('{');
    const __m128i l_close_brace  = _mm_set1_epi8('}');
    const __m128i l_open_bracket = _mm_set1_epi8('[');
    const __m128i l_close_bracket= _mm_set1_epi8(']');
    const __m128i l_colon        = _mm_set1_epi8(':');
    const __m128i l_comma        = _mm_set1_epi8(',');
    
    __m128i l_result = _mm_cmpeq_epi8(a_chunk, l_open_brace);
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_close_brace));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_open_bracket));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_close_bracket));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_colon));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_comma));
    
    return l_result;
}

/**
 * @brief Classify bytes as whitespace using SSE2
 * @details Finds space, tab, \\r, \\n in 16 bytes
 */
static inline __m128i s_classify_whitespace_sse2(const __m128i a_chunk)
{
    const __m128i l_space = _mm_set1_epi8(' ');
    const __m128i l_tab   = _mm_set1_epi8('\t');
    const __m128i l_cr    = _mm_set1_epi8('\r');
    const __m128i l_lf    = _mm_set1_epi8('\n');
    
    __m128i l_result = _mm_cmpeq_epi8(a_chunk, l_space);
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_tab));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_cr));
    l_result = _mm_or_si128(l_result, _mm_cmpeq_epi8(a_chunk, l_lf));
    
    return l_result;
}

/**
 * @brief Classify bytes as quotes using SSE2
 */
static inline __m128i s_classify_quotes_sse2(const __m128i a_chunk)
{
    const __m128i l_quote = _mm_set1_epi8('"');
    return _mm_cmpeq_epi8(a_chunk, l_quote);
}

/**
 * @brief Classify bytes as backslashes using SSE2
 */
static inline __m128i s_classify_backslashes_sse2(const __m128i a_chunk)
{
    const __m128i l_backslash = _mm_set1_epi8('\\');
    return _mm_cmpeq_epi8(a_chunk, l_backslash);
}

/**
 * @brief Compute escaped quotes (quotes that are preceded by odd number of backslashes)
 * @details This is critical for accurate string boundary detection
 * 
 * Algorithm (adapted from simdjson for SSE2):
 * 1. Find all backslashes in chunk
 * 2. Compute "backslash runs" - sequences of consecutive backslashes
 * 3. Carry odd/even state from previous chunk
 * 4. XOR quotes with "escaped" mask to find real string boundaries
 */
static inline uint16_t s_compute_escaped_quotes(
    uint16_t a_quotes,
    uint16_t a_backslashes,
    uint64_t *a_prev_backslash_run
)
{
    // Backslash sequences: find starts and handle carries from previous chunk
    uint64_t l_bs_start = a_backslashes & ~((a_backslashes << 1) | *a_prev_backslash_run);
    uint64_t l_bs_end   = a_backslashes ^ (a_backslashes << 1);
    
    // Compute which backslashes affect quotes (odd-length sequences)
    uint64_t l_bs_even = l_bs_start;
    l_bs_even ^= (l_bs_even << 1);
    l_bs_even ^= (l_bs_even << 2);
    l_bs_even ^= (l_bs_even << 4);
    l_bs_even ^= (l_bs_even << 8);
    
    uint64_t l_escaped = (l_bs_even & l_bs_end) >> 1;  // Escaped positions
    
    // Update carry for next chunk
    *a_prev_backslash_run = (a_backslashes >> 15) & 1;
    
    // Real quotes = all quotes XOR escaped quotes
    return a_quotes & ~((uint16_t)l_escaped);
}

/* ========================================================================== */
/*                        MAIN SSE2 IMPLEMENTATION                            */
/* ========================================================================== */

/**
 * @brief SSE2 implementation of Stage 1 tokenization
 * @param a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    log_it(L_DEBUG, "Starting SSE2 Stage 1 tokenization (%zu bytes)", l_input_len);
    
    // Process in 16-byte chunks
    size_t l_pos = 0;
    bool l_in_string = false;
    uint64_t l_prev_backslash_run = 0;
    
    // Main SIMD loop - process 16 bytes at a time
    while (l_pos + SSE2_CHUNK_SIZE <= l_input_len) {
        // Load 16 bytes
        __m128i l_chunk = _mm_loadu_si128((const __m128i*)(l_input + l_pos));
        
        // Classify all bytes in parallel
        __m128i l_structural = s_classify_structural_sse2(l_chunk);
        __m128i l_whitespace = s_classify_whitespace_sse2(l_chunk);
        __m128i l_quotes     = s_classify_quotes_sse2(l_chunk);
        __m128i l_backslashes= s_classify_backslashes_sse2(l_chunk);
        
        // Convert to bitmasks
        uint16_t l_struct_mask = (uint16_t)_mm_movemask_epi8(l_structural);
        uint16_t l_ws_mask     = (uint16_t)_mm_movemask_epi8(l_whitespace);
        uint16_t l_quote_mask  = (uint16_t)_mm_movemask_epi8(l_quotes);
        uint16_t l_bs_mask     = (uint16_t)_mm_movemask_epi8(l_backslashes);
        
        // Compute real quotes (not escaped)
        uint16_t l_real_quotes = s_compute_escaped_quotes(l_quote_mask, l_bs_mask, &l_prev_backslash_run);
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcount(l_struct_mask);
        a_stage1->whitespace_chars += __builtin_popcount(l_ws_mask);
        
        // Process quotes to track string state
        if (l_real_quotes) {
            int l_bit_idx = __builtin_ctz(l_real_quotes);
            while (l_bit_idx < 16) {
                size_t l_abs_pos = l_pos + l_bit_idx;
                
                if (!l_in_string) {
                    // String start - use reference function for full parsing and validation
                    size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                    if (l_str_end == l_abs_pos) {
                        // Error already set by scan_string
                        return a_stage1->error_code;
                    }
                    
                    size_t l_str_len = l_str_end - l_abs_pos;
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, (uint32_t)l_str_len,
                                              TOKEN_TYPE_STRING, 0);
                    
                    // If string extends beyond current chunk, skip to end of string
                    if (l_str_end >= l_pos + SSE2_CHUNK_SIZE) {
                        l_pos = l_str_end;
                        goto next_chunk;
                    }
                    // Otherwise continue processing rest of chunk
                }
                
                l_in_string = !l_in_string;
                
                // Find next quote
                l_real_quotes &= ~(1 << l_bit_idx);
                if (!l_real_quotes) break;
                l_bit_idx = __builtin_ctz(l_real_quotes);
            }
        }
        
        // Process structural characters (outside strings)
        if (!l_in_string && l_struct_mask) {
            for (int i = 0; i < 16; i++) {
                if (l_struct_mask & (1 << i)) {
                    uint8_t l_char = l_input[l_pos + i];
                    dap_json_stage1_add_token(a_stage1, (uint32_t)(l_pos + i), 0,
                                              TOKEN_TYPE_STRUCTURAL, l_char);
                }
            }
        }
        
        // Process numbers and literals (outside strings, non-structural, non-whitespace)
        if (!l_in_string) {
            uint16_t l_value_mask = ~(l_struct_mask | l_ws_mask | l_quote_mask);
            
            if (l_value_mask) {
                for (int i = 0; i < 16; i++) {
                    if (l_value_mask & (1 << i)) {
                        size_t l_abs_pos = l_pos + i;
                        uint8_t l_char = l_input[l_abs_pos];
                        
                        // Number detection
                        if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                            size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_abs_pos);
                            if (l_num_end > l_abs_pos) {
                                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                          (uint32_t)(l_num_end - l_abs_pos),
                                                          TOKEN_TYPE_NUMBER, 0);
                                // Skip processed bytes within current chunk
                                size_t l_skip = l_num_end - l_abs_pos - 1;
                                if (l_num_end >= l_pos + SSE2_CHUNK_SIZE) {
                                    // Number extends beyond chunk - skip to end of number
                                    l_pos = l_num_end;
                                    goto next_chunk;
                                }
                                i += l_skip;
                            }
                        }
                        // Literal detection
                        else if (l_char == 't' || l_char == 'f' || l_char == 'n') {
                            size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, l_abs_pos);
                            if (l_lit_end > l_abs_pos) {
                                // Determine literal type
                                uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                                if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                                else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                                else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                                
                                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                          (uint32_t)(l_lit_end - l_abs_pos),
                                                          TOKEN_TYPE_LITERAL, l_lit_type);
                                // Skip processed bytes within current chunk
                                size_t l_skip = l_lit_end - l_abs_pos - 1;
                                if (l_lit_end >= l_pos + SSE2_CHUNK_SIZE) {
                                    // Literal extends beyond chunk - skip to end
                                    l_pos = l_lit_end;
                                    goto next_chunk;
                                }
                                i += l_skip;
                            }
                        }
                    }
                }
            }
        }
        
next_chunk:
        l_pos += SSE2_CHUNK_SIZE;
    }
    
    // Process remaining bytes (tail < 16 bytes) using reference implementation
    if (l_pos < l_input_len) {
        log_it(L_DEBUG, "Processing tail: %zu bytes from position %zu", l_input_len - l_pos, l_pos);
        
        for (size_t i = l_pos; i < l_input_len; i++) {
            uint8_t l_char = l_input[i];
            
            // String
            if (l_char == '"' && !l_in_string) {
                size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, i);
                if (l_str_end == i) return a_stage1->error_code;
                
                dap_json_stage1_add_token(a_stage1, (uint32_t)i, (uint32_t)(l_str_end - i),
                                          TOKEN_TYPE_STRING, 0);
                i = l_str_end - 1;
                continue;
            }
            
            // Structural
            if (dap_json_classify_char(l_char) == CHAR_CLASS_STRUCTURAL) {
                dap_json_stage1_add_token(a_stage1, (uint32_t)i, 0, TOKEN_TYPE_STRUCTURAL, l_char);
                continue;
            }
            
            // Whitespace - skip
            if (dap_json_classify_char(l_char) == CHAR_CLASS_WHITESPACE) {
                a_stage1->whitespace_chars++;
                continue;
            }
            
            // Number
            if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, i);
                if (l_num_end > i) {
                    dap_json_stage1_add_token(a_stage1, (uint32_t)i, (uint32_t)(l_num_end - i),
                                              TOKEN_TYPE_NUMBER, 0);
                    i = l_num_end - 1;
                }
                continue;
            }
            
            // Literal
            if (l_char == 't' || l_char == 'f' || l_char == 'n') {
                size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, i);
                if (l_lit_end > i) {
                    uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                    if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                    else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                    else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                    
                    dap_json_stage1_add_token(a_stage1, (uint32_t)i, (uint32_t)(l_lit_end - i),
                                              TOKEN_TYPE_LITERAL, l_lit_type);
                    i = l_lit_end - 1;
                }
            }
        }
    }
    
    // Count tokens by type for statistics
    size_t l_str_count = 0, l_num_count = 0, l_lit_count = 0;
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        switch (a_stage1->indices[i].type) {
            case TOKEN_TYPE_STRING: l_str_count++; break;
            case TOKEN_TYPE_NUMBER: l_num_count++; break;
            case TOKEN_TYPE_LITERAL: l_lit_count++; break;
            default: break;
        }
    }
    
    log_it(L_INFO, "SSE2 Stage 1 complete: %zu tokens (%zu structural, %zu strings, %zu numbers, %zu literals)",
           a_stage1->indices_count,
           a_stage1->indices_count - l_str_count - l_num_count - l_lit_count,
           l_str_count, l_num_count, l_lit_count);
    
    return STAGE1_SUCCESS;
}
