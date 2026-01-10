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
 * Performance target: 1.5-2 GB/s (single-core)
 * 
 * Architecture: Zero-Overhead Hybrid SIMD
 * - SIMD: Parallel classification of all bytes (structural, whitespace, quotes)
 * - Position-Order Processing: All tokens processed in strict position order
 * - Fast Path: Tokens within chunk processed immediately with byte-skipping
 * - Slow Path: Tokens extending beyond chunk deferred to next chunk
 * - No buffers, no sorting - zero overhead!
 * 
 * SSE2 specific:
 * - Chunk size: 16 bytes (128-bit)
 * - Intrinsics: _mm_* operations
 * - Available on: All x86-64 CPUs (baseline instruction set)
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

// Debug flag for detailed SIMD loop tracing
static bool s_debug_more = false;

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
    size_t l_input_len = a_stage1->input_len;
    
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
                debug_if(s_debug_more, L_DEBUG, "  Boundary number at %zu, len=%zu", 
                         l_start, l_num_end - l_start);
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
                
                debug_if(s_debug_more, L_DEBUG, "  Boundary literal at %zu, len=%zu", 
                         l_start, l_lit_end - l_start);
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
 * @brief Set debug mode for SSE2 implementation
 * @param a_enable Enable (true) or disable (false) debug logging
 */
void dap_json_stage1_sse2_set_debug(bool a_enable)
{
    s_debug_more = a_enable ? 1 : 0;
}

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
    size_t l_skip_until = 0;  // Hybrid: track values extending beyond chunk (slow path)
    
    debug_if(s_debug_more, L_DEBUG, "SSE2 HYBRID Stage 1: input_len=%zu", l_input_len);
    
    // Main SIMD loop - process 16 bytes at a time
    while (l_pos + SSE2_CHUNK_SIZE <= l_input_len) {
        debug_if(s_debug_more, L_DEBUG, "Chunk @%zu, skip_until=%zu, in_string=%d", 
                 l_pos, l_skip_until, l_in_string);
        
        // HYBRID SLOW PATH: Process boundary value from previous chunk
        // NOTE: Don't process if we're inside a string - strings are handled in position loop
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, 
                                                     l_pos + SSE2_CHUNK_SIZE, 
                                                     l_skip_until);
        }
        
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
        
        // NOTE: We DON'T use s_compute_escaped_quotes here!
        // Why? The algorithm doesn't know context (inside/outside string).
        // It incorrectly marks opening quotes as "escaped" when followed by backslash.
        // Example: "\"\\uXXXX" - opening quote is marked as escaped!
        // Solution: Process quotes in position order with state tracking.
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcount(l_struct_mask);
        a_stage1->whitespace_chars += __builtin_popcount(l_ws_mask);
        
        // ZERO-OVERHEAD HYBRID: Process ALL tokens in POSITION ORDER
        // This ensures correct ordering without any sorting or buffering!
        for (int i = 0; i < 16; i++) {
            size_t l_abs_pos = l_pos + i;
            
            // Skip if already processed by boundary handler
            if (l_abs_pos < l_skip_until) {
                continue;
            }
            
            // Check if we've finished skipping an extended string
            if (l_in_string && l_abs_pos == l_skip_until) {
                l_in_string = false;  // String ended at previous position
                l_skip_until = 0;     // Reset skip marker
            }
            
            // Priority 1: Quotes/Strings (can extend beyond chunk, affects state)
            // Check quote_mask (not real_quotes!) and validate with state
            if (l_quote_mask & (1 << i)) {
                // Skip quotes inside extended strings - they're part of string content
                if (l_in_string) {
                    continue;  // Inside extended string, ignore all quotes
                }
                
                // This is an opening quote (we're outside string)
                size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                if (l_str_end == l_abs_pos) {
                    return a_stage1->error_code;
                }
                
                // FAST PATH: String within chunk
                if (l_str_end <= l_pos + SSE2_CHUNK_SIZE) {
                    debug_if(s_debug_more, L_DEBUG, "  FAST: string @%zu len=%zu", 
                             l_abs_pos, l_str_end - l_abs_pos);
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                              (uint32_t)(l_str_end - l_abs_pos),
                                              TOKEN_TYPE_STRING, 0);
                    // Skip processed bytes (including closing quote)
                    size_t l_str_len = l_str_end - l_abs_pos;
                    i += (l_str_len - 1);
                }
                // SLOW PATH: String extends beyond chunk
                else {
                    debug_if(s_debug_more, L_DEBUG, "  SLOW: string @%zu extends to %zu", 
                             l_abs_pos, l_str_end);
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                              (uint32_t)(l_str_end - l_abs_pos),
                                              TOKEN_TYPE_STRING, 0);
                    l_skip_until = l_str_end;
                    l_in_string = true;  // String extends, mark as inside
                    break;  // End processing this chunk
                }
                continue;
            }
            
            // Skip processing inside strings
            if (l_in_string) continue;
            
            // Priority 2: Structural (always single byte, no boundary issues)
            if (l_struct_mask & (1 << i)) {
                uint8_t l_char = l_input[l_abs_pos];
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, 0,
                                          TOKEN_TYPE_STRUCTURAL, l_char);
                continue;  // Next position
            }
            
            // Priority 3: Whitespace (skip)
            if (l_ws_mask & (1 << i)) {
                continue;  // Next position
            }
                
                // Priority 4: Numbers/Literals (can extend beyond chunk)
                uint8_t l_char = l_input[l_abs_pos];
                
                // Number
                if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                    size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_abs_pos);
                    if (l_num_end > l_abs_pos) {
                        // FAST PATH: Number within chunk
                        if (l_num_end <= l_pos + SSE2_CHUNK_SIZE) {
                            debug_if(s_debug_more, L_DEBUG, "  FAST: number @%zu len=%zu", 
                                     l_abs_pos, l_num_end - l_abs_pos);
                            dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                      (uint32_t)(l_num_end - l_abs_pos),
                                                      TOKEN_TYPE_NUMBER, 0);
                            // Skip processed bytes
                            i += (l_num_end - l_abs_pos - 1);
                        }
                        // SLOW PATH: Number extends beyond chunk
                        else {
                            debug_if(s_debug_more, L_DEBUG, "  SLOW: number @%zu extends to %zu", 
                                     l_abs_pos, l_num_end);
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
                        if (l_lit_end <= l_pos + SSE2_CHUNK_SIZE) {
                            uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                            if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                            else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                            else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                            
                            debug_if(s_debug_more, L_DEBUG, "  FAST: literal @%zu len=%zu", 
                                     l_abs_pos, l_lit_end - l_abs_pos);
                            dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                                      (uint32_t)(l_lit_end - l_abs_pos),
                                                      TOKEN_TYPE_LITERAL, l_lit_type);
                            // Skip processed bytes
                            i += (l_lit_end - l_abs_pos - 1);
                        }
                        // SLOW PATH: Literal extends beyond chunk
                        else {
                            debug_if(s_debug_more, L_DEBUG, "  SLOW: literal @%zu extends to %zu", 
                                     l_abs_pos, l_lit_end);
                            l_skip_until = l_lit_end;
                            break;  // End processing this chunk
                        }
                    }
                }
            }
        
        // Always advance to next chunk
        l_pos += SSE2_CHUNK_SIZE;
    }
    
    // Process remaining bytes (tail < 16 bytes) - full processing
    if (l_pos < l_input_len) {
        debug_if(s_debug_more, L_DEBUG, "Processing tail: %zu bytes from position %zu, skip_until=%zu, in_string=%d", 
                 l_input_len - l_pos, l_pos, l_skip_until, l_in_string);
        
        // First, handle any boundary value from last chunk
        // NOTE: Don't process if we're inside a string
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, l_input_len, l_skip_until);
        }
        
        for (size_t i = l_pos; i < l_input_len; i++) {
            // Skip if already processed by boundary handler
            if (i < l_skip_until) continue;
            
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
    
    debug_if(s_debug_more, L_DEBUG, "SSE2 HYBRID complete: %zu total tokens", 
             a_stage1->indices_count);
    
    return STAGE1_SUCCESS;
}
