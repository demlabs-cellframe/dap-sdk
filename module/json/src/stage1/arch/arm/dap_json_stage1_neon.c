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
 * Performance target: 1.5-2 GB/s (single-core)
 * 
 * Architecture: Zero-Overhead Hybrid SIMD
 * - SIMD: Parallel classification of all bytes (structural, whitespace, quotes)
 * - Position-Order Processing: All tokens processed in strict position order
 * - Fast Path: Tokens within chunk processed immediately with byte-skipping
 * - Slow Path: Tokens extending beyond chunk deferred to next chunk
 * - No buffers, no sorting - zero overhead!
 * 
 * NEON specific:
 * - Chunk size: 16 bytes (128-bit)
 * - Intrinsics: v* operations (ARM NEON)
 * - Available on: All ARM64 CPUs (baseline), ARM32 with -mfpu=neon
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "dap_common.h"
#include "dap_json.h"
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
/*            HYBRID: BOUNDARY VALUES (Slow Path Only)                        */
/* ========================================================================== */

/**
 * @brief Process boundary values that extended beyond chunk
 * 
 * This is the "slow path" of HYBRID approach - processes only values
 * (numbers/literals) that started in one chunk but extended into next chunk.
 * 
 * NOTE: Strings are handled differently - tracked via l_in_string state.
 * 
 * @param a_stage1 Stage 1 context
 * @param a_chunk_start Start of current chunk
 * @param a_chunk_end End of current chunk
 * @param a_skip_until Position until which to skip (already processed)
 * @return Updated skip_until position
 */
static size_t s_process_boundary_values(
    dap_json_stage1_t *a_stage1,
    size_t a_chunk_start,
    size_t a_chunk_end,
    size_t a_skip_until)
{
    const uint8_t *l_input = a_stage1->input;
    
    // If we have skip_until from previous chunk, process the boundary value
    if (a_skip_until > a_chunk_start) {
        size_t l_start = a_chunk_start;
        
        // Find the actual start of the value (it's in previous chunk)
        while (l_start > 0) {
            uint8_t l_char = l_input[l_start - 1];
            if (dap_json_classify_char(l_char) == CHAR_CLASS_WHITESPACE ||
                dap_json_classify_char(l_char) == CHAR_CLASS_STRUCTURAL ||
                l_char == '"') {
                break;
            }
            l_start--;
        }
        
        uint8_t l_char = l_input[l_start];
        
        // Number
        if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
            size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_start);
            if (l_num_end > l_start) {
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_start,
                                          (uint32_t)(l_num_end - l_start),
                                          TOKEN_TYPE_NUMBER, 0);
                return l_num_end;
            }
        }
        // Literal
        else if (l_char == 't' || l_char == 'f' || l_char == 'n') {
            size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, l_start);
            if (l_lit_end > l_start) {
                uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_start,
                                          (uint32_t)(l_lit_end - l_start),
                                          TOKEN_TYPE_LITERAL, l_lit_type);
                return l_lit_end;
            }
        }
    }
    
    return a_skip_until;
}

/* ========================================================================== */
/*                        MAIN NEON IMPLEMENTATION                            */
/* ========================================================================== */

/**
 * @brief NEON implementation of Stage 1 tokenization
 * 
 * @warning This function REQUIRES ARM NEON support at compile time.
 *          It will FAIL FAST with compile error if NEON is not available.
 *          Use CPU dispatch mechanism to select implementation at runtime.
 */
int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1)
{
#ifndef __ARM_NEON
    #error "ARM NEON not available at compile time! This file should not be compiled without NEON support. Check CMakeLists.txt architecture detection."
#endif
    
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    debug_if(dap_json_get_debug(), "Starting NEON HYBRID Stage 1 tokenization (%zu bytes)", l_input_len);
    
    // State
    bool l_in_string = false;
    uint64_t l_prev_backslash_run = 0;
    size_t l_pos = 0;
    size_t l_skip_until = 0;  // HYBRID: track values extending beyond chunk (slow path)
    
    // Process full NEON chunks (16 bytes)
    while (l_pos + NEON_CHUNK_SIZE <= l_input_len) {
        // HYBRID SLOW PATH: Process boundary value from previous chunk
        // Only process numbers/literals (not strings - they're tracked via l_in_string)
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, 
                                                     l_pos + NEON_CHUNK_SIZE, 
                                                     l_skip_until);
        }
        
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
        
        // ZERO-OVERHEAD HYBRID: Process ALL tokens in POSITION ORDER
        // This ensures correct ordering without any sorting or buffering!
        for (int i = 0; i < NEON_CHUNK_SIZE; i++) {
            size_t l_abs_pos = l_pos + i;
            
            // Check if we've finished skipping an extended string/value
            if (l_in_string && l_abs_pos == l_skip_until) {
                l_in_string = false;  // String ended
                l_skip_until = 0;     // Reset skip marker
            }
            
            // Skip if already processed by boundary handler or inside extended string
            if (l_abs_pos < l_skip_until) continue;
            
            uint16_t l_bit = (1U << i);
            
            // Priority 1: Quotes/Strings (can extend beyond chunk, affects state)
            if (l_real_quotes & l_bit) {
                if (!l_in_string) {
                    // String start
                    size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                    if (l_str_end == l_abs_pos) {
                        return a_stage1->error_code;
                    }
                    
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                              (uint32_t)(l_str_end - l_abs_pos),
                                              TOKEN_TYPE_STRING, 0);
                    
                    // FAST PATH: String within chunk
                    if (l_str_end <= l_pos + NEON_CHUNK_SIZE) {
                        // Skip processed bytes (including closing quote)
                        size_t l_str_len = l_str_end - l_abs_pos;
                        i += (l_str_len - 1);
                    }
                    // SLOW PATH: String extends beyond chunk
                    else {
                        l_skip_until = l_str_end;
                        l_in_string = true;  // Mark that we're inside a string spanning chunks
                        break;  // End processing this chunk
                    }
                }
                // else: We're inside an extended string, this quote is internal (escaped or part of content)
                continue;
            }
            
            // Skip processing inside strings
            if (l_in_string) continue;
            
            // Priority 2: Structural (always single byte, no boundary issues)
            if (l_struct_mask & l_bit) {
                uint8_t l_char = l_input[l_abs_pos];
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, 0,
                                          TOKEN_TYPE_STRUCTURAL, l_char);
                continue;  // Next position
            }
            
            // Priority 3: Whitespace (skip)
            if (l_ws_mask & l_bit) {
                continue;  // Next position
            }
            
            // Priority 4: Numbers/Literals (can extend beyond chunk)
            uint8_t l_char = l_input[l_abs_pos];
            
            // Number
            if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_abs_pos);
                if (l_num_end > l_abs_pos) {
                    // FAST PATH: Number within chunk
                    if (l_num_end <= l_pos + NEON_CHUNK_SIZE) {
                        dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                  (uint32_t)(l_num_end - l_abs_pos),
                                                  TOKEN_TYPE_NUMBER, 0);
                        // Skip processed bytes
                        i += (l_num_end - l_abs_pos - 1);
                    }
                    // SLOW PATH: Number extends beyond chunk
                    else {
                        l_skip_until = l_num_end;
                        break;  // End processing this chunk
                    }
                }
                continue;
            }
            
            // Literal
            if (l_char == 't' || l_char == 'f' || l_char == 'n') {
                size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, l_abs_pos);
                if (l_lit_end > l_abs_pos) {
                    // FAST PATH: Literal within chunk
                    if (l_lit_end <= l_pos + NEON_CHUNK_SIZE) {
                        uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                        if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                        else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                        else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                        
                        dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                  (uint32_t)(l_lit_end - l_abs_pos),
                                                  TOKEN_TYPE_LITERAL, l_lit_type);
                        // Skip processed bytes
                        i += (l_lit_end - l_abs_pos - 1);
                    }
                    // SLOW PATH: Literal extends beyond chunk
                    else {
                        l_skip_until = l_lit_end;
                        break;  // End processing this chunk
                    }
                }
            }
        }
        
        // Always advance to next chunk
        l_pos += NEON_CHUNK_SIZE;
    }
    
    // TAIL PROCESSING: Process remaining bytes (< 16 bytes)
    // Apply HYBRID approach to tail as well
    if (l_pos < l_input_len) {
        // Process boundary values from last chunk if needed
        // Only for numbers/literals (not strings - they're tracked via l_in_string)
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, 
                                                     l_input_len, 
                                                     l_skip_until);
        }
        
        // Process remaining bytes in position order
        while (l_pos < l_input_len) {
            // Check if we've finished skipping an extended string/value
            if (l_in_string && l_pos == l_skip_until) {
                l_in_string = false;
                l_skip_until = 0;
            }
            
            // Skip if already processed
            if (l_pos < l_skip_until) {
                l_pos++;
                continue;
            }
            
            uint8_t l_char = l_input[l_pos];
            
            // Priority 1: Quotes/Strings
            if (l_char == '"' && !l_in_string) {
                size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_pos);
                if (l_str_end == l_pos) {
                    return a_stage1->error_code;
                }
                
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_pos,
                                          (uint32_t)(l_str_end - l_pos),
                                          TOKEN_TYPE_STRING, 0);
                l_pos = l_str_end;
                continue;
            }
            
            // Skip if inside extended string
            if (l_in_string) {
                l_pos++;
                continue;
            }
            
            // Priority 2: Structural
            dap_json_char_class_t l_class = dap_json_classify_char(l_char);
            if (l_class == CHAR_CLASS_STRUCTURAL) {
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_pos, 0,
                                          TOKEN_TYPE_STRUCTURAL, l_char);
                a_stage1->structural_chars++;
                l_pos++;
                continue;
            }
            
            // Priority 3: Whitespace
            if (l_class == CHAR_CLASS_WHITESPACE) {
                a_stage1->whitespace_chars++;
                l_pos++;
                continue;
            }
            
            // Priority 4: Numbers
            if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_pos);
                if (l_num_end > l_pos) {
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_pos,
                                              (uint32_t)(l_num_end - l_pos),
                                              TOKEN_TYPE_NUMBER, 0);
                    l_pos = l_num_end;
                } else {
                    l_pos++;
                }
                continue;
            }
            
            // Priority 5: Literals
            if (l_char == 't' || l_char == 'f' || l_char == 'n') {
                size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, l_pos);
                if (l_lit_end > l_pos) {
                    uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                    if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                    else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                    else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                    
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_pos,
                                              (uint32_t)(l_lit_end - l_pos),
                                              TOKEN_TYPE_LITERAL, l_lit_type);
                    l_pos = l_lit_end;
                } else {
                    l_pos++;
                }
                continue;
            }
            
            // Unknown character
            debug_if(dap_json_get_debug(), "Unexpected character 0x%02X at position %zu", l_char, l_pos);
            a_stage1->error_code = STAGE1_ERROR_INVALID_INPUT;
            a_stage1->error_position = l_pos;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Unexpected character: 0x%02X", l_char);
            return STAGE1_ERROR_INVALID_INPUT;
        }
    }
    
    debug_if(dap_json_get_debug(), "NEON HYBRID Stage 1 complete: %zu tokens, %zu structural, %zu whitespace",
           a_stage1->indices_count,
           a_stage1->structural_chars,
           a_stage1->whitespace_chars);
    
    return STAGE1_SUCCESS;
}
