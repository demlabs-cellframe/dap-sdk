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
 * @file dap_json_stage1_neon.c
 * @brief Stage 1 JSON tokenization - ARM NEON implementation
 * @details SIMD-optimized structural + value tokenization using NEON (16 bytes/iteration)
 * 
 * Architecture: Hybrid SIMD+scalar approach
 * - SIMD: Fast detection of token boundaries (structural chars, strings, numbers, literals)
 * - Scalar: Precise validation (UTF-8, number parsing, literal matching)
 * 
 * NEON specific:
 * - Chunk size: 16 bytes (128-bit)
 * - Intrinsics: v* (ARM NEON)
 * - Available on: ARM64 (baseline), ARM32 with NEON flag
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_neon"

// NEON chunk size
#define NEON_CHUNK_SIZE 16

/* ========================================================================== */
/*                        SIMD PRIMITIVES - NEON                              */
/* ========================================================================== */

#ifdef __ARM_NEON

/**
 * @brief Classify bytes as structural characters using NEON
 * @details Finds {, }, [, ], :, , in 16 bytes
 */
static inline uint8x16_t s_classify_structural_neon(const uint8x16_t a_chunk)
{
    const uint8x16_t l_open_brace   = vdupq_n_u8('{');
    const uint8x16_t l_close_brace  = vdupq_n_u8('}');
    const uint8x16_t l_open_bracket = vdupq_n_u8('[');
    const uint8x16_t l_close_bracket= vdupq_n_u8(']');
    const uint8x16_t l_colon        = vdupq_n_u8(':');
    const uint8x16_t l_comma        = vdupq_n_u8(',');
    
    uint8x16_t l_result = vceqq_u8(a_chunk, l_open_brace);
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_close_brace));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_open_bracket));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_close_bracket));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_colon));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_comma));
    
    return l_result;
}

/**
 * @brief Classify bytes as whitespace using NEON
 * @details Finds space, tab, \\r, \\n in 16 bytes
 */
static inline uint8x16_t s_classify_whitespace_neon(const uint8x16_t a_chunk)
{
    const uint8x16_t l_space = vdupq_n_u8(' ');
    const uint8x16_t l_tab   = vdupq_n_u8('\t');
    const uint8x16_t l_cr    = vdupq_n_u8('\r');
    const uint8x16_t l_lf    = vdupq_n_u8('\n');
    
    uint8x16_t l_result = vceqq_u8(a_chunk, l_space);
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_tab));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_cr));
    l_result = vorrq_u8(l_result, vceqq_u8(a_chunk, l_lf));
    
    return l_result;
}

/**
 * @brief Classify bytes as quotes using NEON
 */
static inline uint8x16_t s_classify_quotes_neon(const uint8x16_t a_chunk)
{
    const uint8x16_t l_quote = vdupq_n_u8('"');
    return vceqq_u8(a_chunk, l_quote);
}

/**
 * @brief Classify bytes as backslashes using NEON
 */
static inline uint8x16_t s_classify_backslashes_neon(const uint8x16_t a_chunk)
{
    const uint8x16_t l_backslash = vdupq_n_u8('\\');
    return vceqq_u8(a_chunk, l_backslash);
}

/**
 * @brief Convert NEON comparison result to bitmask
 * @details Extract MSB from each byte to create 16-bit mask
 */
static inline uint16_t s_neon_to_bitmask(const uint8x16_t a_vec)
{
    // NEON doesn't have direct movemask, so we extract bits manually
    // Shift and combine approach
    static const uint8_t __attribute__((aligned(16))) shift_arr[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    
    uint8x16_t l_shift = vld1q_u8(shift_arr);
    uint8x16_t l_masked = vshrq_n_u8(a_vec, 7);  // Extract MSB
    
    // Combine into 16-bit mask using shifts
    uint16_t l_mask = 0;
    uint8_t l_bytes[16];
    vst1q_u8(l_bytes, l_masked);
    
    for (int i = 0; i < 16; i++) {
        l_mask |= (l_bytes[i] ? 1 : 0) << i;
    }
    
    return l_mask;
}

/**
 * @brief Compute escaped quotes (same algorithm as SSE2/AVX2)
 */
static inline uint16_t s_compute_escaped_quotes(
    uint16_t a_quotes,
    uint16_t a_backslashes,
    uint64_t *a_prev_backslash_run
)
{
    uint64_t l_bs_start = a_backslashes & ~((a_backslashes << 1) | *a_prev_backslash_run);
    uint64_t l_bs_end   = a_backslashes ^ (a_backslashes << 1);
    
    uint64_t l_bs_even = l_bs_start;
    l_bs_even ^= (l_bs_even << 1);
    l_bs_even ^= (l_bs_even << 2);
    l_bs_even ^= (l_bs_even << 4);
    l_bs_even ^= (l_bs_even << 8);
    
    uint64_t l_escaped = (l_bs_even & l_bs_end) >> 1;
    *a_prev_backslash_run = (a_backslashes >> 15) & 1;
    
    return a_quotes & ~((uint16_t)l_escaped);
}

#endif // __ARM_NEON

/* ========================================================================== */
/*                        MAIN NEON IMPLEMENTATION                            */
/* ========================================================================== */

/**
 * @brief NEON implementation of Stage 1 tokenization
 */
int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1)
{
#ifndef __ARM_NEON
    // Fallback to reference if NEON not available
    log_it(L_WARNING, "NEON not available at compile time, using reference implementation");
    return dap_json_stage1_run_ref(a_stage1);
#else
    
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    log_it(L_DEBUG, "Starting NEON Stage 1 tokenization (%zu bytes)", l_input_len);
    
    // Process in 16-byte chunks
    size_t l_pos = 0;
    bool l_in_string = false;
    uint64_t l_prev_backslash_run = 0;
    
    // Main SIMD loop
    while (l_pos + NEON_CHUNK_SIZE <= l_input_len) {
        // Load 16 bytes
        uint8x16_t l_chunk = vld1q_u8(l_input + l_pos);
        
        // Classify all bytes in parallel
        uint8x16_t l_structural = s_classify_structural_neon(l_chunk);
        uint8x16_t l_whitespace = s_classify_whitespace_neon(l_chunk);
        uint8x16_t l_quotes     = s_classify_quotes_neon(l_chunk);
        uint8x16_t l_backslashes= s_classify_backslashes_neon(l_chunk);
        
        // Convert to bitmasks
        uint16_t l_struct_mask = s_neon_to_bitmask(l_structural);
        uint16_t l_ws_mask     = s_neon_to_bitmask(l_whitespace);
        uint16_t l_quote_mask  = s_neon_to_bitmask(l_quotes);
        uint16_t l_bs_mask     = s_neon_to_bitmask(l_backslashes);
        
        // Compute real quotes
        uint16_t l_real_quotes = s_compute_escaped_quotes(l_quote_mask, l_bs_mask, &l_prev_backslash_run);
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcount(l_struct_mask);
        a_stage1->whitespace_chars += __builtin_popcount(l_ws_mask);
        
        // Process quotes
        if (l_real_quotes) {
            int l_bit_idx = __builtin_ctz(l_real_quotes);
            while (l_bit_idx < 16) {
                size_t l_abs_pos = l_pos + l_bit_idx;
                
                if (!l_in_string) {
                    size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                    if (l_str_end == l_abs_pos) {
                        return a_stage1->error_code;
                    }
                    
                    size_t l_str_len = l_str_end - l_abs_pos;
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, (uint32_t)l_str_len,
                                              TOKEN_TYPE_STRING, 0);
                    
                    if (l_str_end > l_pos + NEON_CHUNK_SIZE) {
                        l_pos = l_str_end;
                        goto next_chunk;
                    }
                }
                
                l_in_string = !l_in_string;
                
                l_real_quotes &= ~(1 << l_bit_idx);
                if (!l_real_quotes) break;
                l_bit_idx = __builtin_ctz(l_real_quotes);
            }
        }
        
        // Process structural characters
        if (!l_in_string && l_struct_mask) {
            for (int i = 0; i < 16; i++) {
                if (l_struct_mask & (1 << i)) {
                    uint8_t l_char = l_input[l_pos + i];
                    dap_json_stage1_add_token(a_stage1, (uint32_t)(l_pos + i), 0,
                                              TOKEN_TYPE_STRUCTURAL, l_char);
                }
            }
        }
        
        // Process numbers and literals
        if (!l_in_string) {
            uint16_t l_value_mask = ~(l_struct_mask | l_ws_mask | l_quote_mask);
            
            if (l_value_mask) {
                for (int i = 0; i < 16; i++) {
                    if (l_value_mask & (1 << i)) {
                        size_t l_abs_pos = l_pos + i;
                        uint8_t l_char = l_input[l_abs_pos];
                        
                        // Number
                        if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                            size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_abs_pos);
                            if (l_num_end > l_abs_pos) {
                                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                          (uint32_t)(l_num_end - l_abs_pos),
                                                          TOKEN_TYPE_NUMBER, 0);
                                i += (l_num_end - l_abs_pos - 1);
                            }
                        }
                        // Literal
                        else if (l_char == 't' || l_char == 'f' || l_char == 'n') {
                            size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, l_abs_pos);
                            if (l_lit_end > l_abs_pos) {
                                uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                                if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                                else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                                else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                                
                                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                          (uint32_t)(l_lit_end - l_abs_pos),
                                                          TOKEN_TYPE_LITERAL, l_lit_type);
                                i += (l_lit_end - l_abs_pos - 1);
                            }
                        }
                    }
                }
            }
        }
        
next_chunk:
        l_pos += NEON_CHUNK_SIZE;
    }
    
    // Process tail using reference
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
            
            // Whitespace
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
    
    // Statistics
    size_t l_str_count = 0, l_num_count = 0, l_lit_count = 0;
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        switch (a_stage1->indices[i].type) {
            case TOKEN_TYPE_STRING: l_str_count++; break;
            case TOKEN_TYPE_NUMBER: l_num_count++; break;
            case TOKEN_TYPE_LITERAL: l_lit_count++; break;
            default: break;
        }
    }
    
    log_it(L_INFO, "NEON Stage 1 complete: %zu tokens (%zu structural, %zu strings, %zu numbers, %zu literals)",
           a_stage1->indices_count,
           a_stage1->indices_count - l_str_count - l_num_count - l_lit_count,
           l_str_count, l_num_count, l_lit_count);
    
    return STAGE1_SUCCESS;
    
#endif // __ARM_NEON
}
