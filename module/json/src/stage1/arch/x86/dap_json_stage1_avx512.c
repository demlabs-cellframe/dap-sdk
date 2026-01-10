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
 * @file dap_json_stage1_avx512.c
 * @brief Stage 1 JSON tokenization - AVX-512 (x86/x64) implementation
 * @details SIMD-optimized structural + value tokenization using AVX-512 (64 bytes/iteration)
 * 
 * Performance target: 6-8 GB/s (single-core)
 * 
 * Architecture: Zero-Overhead Hybrid SIMD
 * - SIMD: Parallel classification of all bytes using mask operations
 * - Position-Order Processing: All tokens processed in strict position order
 * - Fast Path: Tokens within chunk processed immediately with byte-skipping
 * - Slow Path: Tokens extending beyond chunk deferred to next chunk
 * - No buffers, no sorting - zero overhead!
 * 
 * AVX-512 specific:
 * - Chunk size: 64 bytes (512-bit)
 * - Intrinsics: _mm512_* + __mmask64 operations
 * - Available on: Intel Ice Lake+, AMD Zen 4+ (Ryzen 7000+)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512BW__)
#include <immintrin.h>  // AVX-512
#endif

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_avx512"

// AVX-512 chunk size
#define AVX512_CHUNK_SIZE 64

/* ========================================================================== */
/*                        SIMD PRIMITIVES - AVX-512                           */
/* ========================================================================== */

#if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512BW__)

/**
 * @brief Classify bytes as structural characters using AVX-512
 * @details Finds {, }, [, ], :, , in 64 bytes using mask operations
 */
static inline __mmask64 s_classify_structural_avx512(const __m512i a_chunk)
{
    const __m512i l_open_brace   = _mm512_set1_epi8('{');
    const __m512i l_close_brace  = _mm512_set1_epi8('}');
    const __m512i l_open_bracket = _mm512_set1_epi8('[');
    const __m512i l_close_bracket= _mm512_set1_epi8(']');
    const __m512i l_colon        = _mm512_set1_epi8(':');
    const __m512i l_comma        = _mm512_set1_epi8(',');
    
    __mmask64 l_result = _mm512_cmpeq_epi8_mask(a_chunk, l_open_brace);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_close_brace);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_open_bracket);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_close_bracket);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_colon);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_comma);
    
    return l_result;
}

/**
 * @brief Classify bytes as whitespace using AVX-512
 */
static inline __mmask64 s_classify_whitespace_avx512(const __m512i a_chunk)
{
    const __m512i l_space = _mm512_set1_epi8(' ');
    const __m512i l_tab   = _mm512_set1_epi8('\t');
    const __m512i l_cr    = _mm512_set1_epi8('\r');
    const __m512i l_lf    = _mm512_set1_epi8('\n');
    
    __mmask64 l_result = _mm512_cmpeq_epi8_mask(a_chunk, l_space);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_tab);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_cr);
    l_result |= _mm512_cmpeq_epi8_mask(a_chunk, l_lf);
    
    return l_result;
}

/**
 * @brief Classify bytes as quotes using AVX-512
 */
static inline __mmask64 s_classify_quotes_avx512(const __m512i a_chunk)
{
    const __m512i l_quote = _mm512_set1_epi8('"');
    return _mm512_cmpeq_epi8_mask(a_chunk, l_quote);
}

/**
 * @brief Classify bytes as backslashes using AVX-512
 */
static inline __mmask64 s_classify_backslashes_avx512(const __m512i a_chunk)
{
    const __m512i l_backslash = _mm512_set1_epi8('\\');
    return _mm512_cmpeq_epi8_mask(a_chunk, l_backslash);
}

/**
 * @brief Compute escaped quotes (adapted for 64-bit masks)
 */
static inline uint64_t s_compute_escaped_quotes_avx512(
    uint64_t a_quotes,
    uint64_t a_backslashes,
    uint64_t *a_prev_backslash_run
)
{
    uint64_t l_bs_start = a_backslashes & ~((a_backslashes << 1) | *a_prev_backslash_run);
    uint64_t l_bs_end   = a_backslashes ^ (a_backslashes << 1);
    
    // Compute even-length backslash sequences (extended for 64-bit)
    uint64_t l_bs_even = l_bs_start;
    l_bs_even ^= (l_bs_even << 1);
    l_bs_even ^= (l_bs_even << 2);
    l_bs_even ^= (l_bs_even << 4);
    l_bs_even ^= (l_bs_even << 8);
    l_bs_even ^= (l_bs_even << 16);
    l_bs_even ^= (l_bs_even << 32);
    
    uint64_t l_escaped = (l_bs_even & l_bs_end) >> 1;
    *a_prev_backslash_run = (a_backslashes >> 63) & 1;
    
    return a_quotes & ~l_escaped;
}

#endif // AVX-512

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
/*                        MAIN AVX-512 IMPLEMENTATION                         */
/* ========================================================================== */

/**
 * @brief AVX-512 implementation of Stage 1 tokenization
 * 
 * @warning This function REQUIRES AVX-512 support at compile time.
 *          It will FAIL FAST with compile error if AVX-512 is not available.
 *          Use CPU dispatch mechanism to select implementation at runtime.
 */
int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1)
{
#if !defined(__AVX512F__) || !defined(__AVX512DQ__) || !defined(__AVX512BW__)
    #error "AVX-512 not available at compile time! This file should not be compiled without AVX-512 support. Check CMakeLists.txt architecture detection."
#endif
    
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    log_it(L_DEBUG, "Starting AVX-512 HYBRID Stage 1 tokenization (%zu bytes)", l_input_len);
    
    // State
    bool l_in_string = false;
    size_t l_pos = 0;
    size_t l_skip_until = 0;  // HYBRID: track values extending beyond chunk (slow path)
    
    // Process full AVX-512 chunks (64 bytes)
    while (l_pos + AVX512_CHUNK_SIZE <= l_input_len) {
        // HYBRID SLOW PATH: Process boundary value from previous chunk
        // NOTE: Don't process if we're inside a string
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, 
                                                     l_pos + AVX512_CHUNK_SIZE, 
                                                     l_skip_until);
        }
        
        // Load 64 bytes
        __m512i l_chunk = _mm512_loadu_si512((const __m512i*)(l_input + l_pos));
        
        // Classify all bytes in parallel using mask operations
        __mmask64 l_struct_mask = s_classify_structural_avx512(l_chunk);
        __mmask64 l_ws_mask     = s_classify_whitespace_avx512(l_chunk);
        __mmask64 l_quote_mask  = s_classify_quotes_avx512(l_chunk);
        __mmask64 l_bs_mask     = s_classify_backslashes_avx512(l_chunk);
        
        // NOTE: We DON'T use s_compute_escaped_quotes here!
        // Why? The algorithm doesn't know context (inside/outside string).
        // It incorrectly marks opening quotes as "escaped" when followed by backslash.
        // Example: "\"\\uXXXX" - opening quote is marked as escaped!
        // Solution: Process quotes in position order with state tracking.
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcountll(l_struct_mask);
        a_stage1->whitespace_chars += __builtin_popcountll(l_ws_mask);
        
        // ZERO-OVERHEAD HYBRID: Process ALL tokens in POSITION ORDER
        // This ensures correct ordering without any sorting or buffering!
        for (int i = 0; i < AVX512_CHUNK_SIZE; i++) {
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
            
            uint64_t l_bit = (1ULL << i);
            
            // Priority 1: Quotes/Strings (can extend beyond chunk, affects state)
            // Check quote_mask (not real_quotes!) and validate with state
            if (l_quote_mask & l_bit) {
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
                if (l_str_end <= l_pos + AVX512_CHUNK_SIZE) {
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos,
                                              (uint32_t)(l_str_end - l_abs_pos),
                                              TOKEN_TYPE_STRING, 0);
                    // Skip processed bytes (including closing quote)
                    size_t l_str_len = l_str_end - l_abs_pos;
                    i += (l_str_len - 1);
                }
                // SLOW PATH: String extends beyond chunk
                else {
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
                    if (l_num_end <= l_pos + AVX512_CHUNK_SIZE) {
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
                    if (l_lit_end <= l_pos + AVX512_CHUNK_SIZE) {
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
        l_pos += AVX512_CHUNK_SIZE;
    }
    
    // TAIL PROCESSING: Process remaining bytes (< 64 bytes)
    // Apply HYBRID approach to tail as well
    if (l_pos < l_input_len) {
        // Process boundary values from last chunk if needed
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
                
                // String may extend beyond tail
                if (l_str_end > l_input_len) {
                    l_skip_until = l_str_end;
                    l_in_string = true;
                }
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
            log_it(L_ERROR, "Unexpected character 0x%02X at position %zu", l_char, l_pos);
            a_stage1->error_code = STAGE1_ERROR_INVALID_INPUT;
            a_stage1->error_position = l_pos;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Unexpected character: 0x%02X", l_char);
            return STAGE1_ERROR_INVALID_INPUT;
        }
    }
    
    log_it(L_INFO, "AVX-512 HYBRID Stage 1 complete: %zu tokens, %zu structural, %zu whitespace",
           a_stage1->indices_count,
           a_stage1->structural_chars,
           a_stage1->whitespace_chars);
    
    return STAGE1_SUCCESS;
}
