/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
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
 * @file dap_json_stage1_avx2.c
 * @brief AVX2-optimized Stage 1 JSON tokenization
 * @details Processes 32 bytes per iteration using AVX2 SIMD instructions
 * 
 * Performance target: 3-4 GB/s (single-core)
 * 
 * Architecture: Zero-Overhead Hybrid SIMD
 * - SIMD: Parallel classification of all bytes (structural, whitespace, quotes)
 * - Position-Order Processing: All tokens processed in strict position order
 * - Fast Path: Tokens within chunk processed immediately with byte-skipping
 * - Slow Path: Tokens extending beyond chunk deferred to next chunk
 * - No buffers, no sorting - zero overhead!
 * 
 * Available on: Intel/AMD x86-64 with AVX2 support (Haswell+, Zen+)
 */

#ifdef __AVX2__

#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_avx2"

// AVX2 processes 32 bytes per chunk
#define AVX2_CHUNK_SIZE 32

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
/*                         SIMD PRIMITIVES                                    */
/* ========================================================================== */

/**
 * @brief Find structural characters in AVX2 chunk
 * @details Identifies {, }, [, ], :, , in parallel
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is structural
 */
static inline uint32_t s_simd_find_structural_avx2(__m256i a_chunk)
{
    const __m256i l_brace_open  = _mm256_set1_epi8('{');
    const __m256i l_brace_close = _mm256_set1_epi8('}');
    const __m256i l_bracket_open  = _mm256_set1_epi8('[');
    const __m256i l_bracket_close = _mm256_set1_epi8(']');
    const __m256i l_colon = _mm256_set1_epi8(':');
    const __m256i l_comma = _mm256_set1_epi8(',');
    
    __m256i l_cmp1 = _mm256_cmpeq_epi8(a_chunk, l_brace_open);
    __m256i l_cmp2 = _mm256_cmpeq_epi8(a_chunk, l_brace_close);
    __m256i l_cmp3 = _mm256_cmpeq_epi8(a_chunk, l_bracket_open);
    __m256i l_cmp4 = _mm256_cmpeq_epi8(a_chunk, l_bracket_close);
    __m256i l_cmp5 = _mm256_cmpeq_epi8(a_chunk, l_colon);
    __m256i l_cmp6 = _mm256_cmpeq_epi8(a_chunk, l_comma);
    
    __m256i l_structural = _mm256_or_si256(l_cmp1, l_cmp2);
    l_structural = _mm256_or_si256(l_structural, l_cmp3);
    l_structural = _mm256_or_si256(l_structural, l_cmp4);
    l_structural = _mm256_or_si256(l_structural, l_cmp5);
    l_structural = _mm256_or_si256(l_structural, l_cmp6);
    
    return (uint32_t)_mm256_movemask_epi8(l_structural);
}

/**
 * @brief Find whitespace characters in AVX2 chunk
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is whitespace
 */
static inline uint32_t s_simd_find_whitespace_avx2(__m256i a_chunk)
{
    const __m256i l_space = _mm256_set1_epi8(' ');
    const __m256i l_tab   = _mm256_set1_epi8('\t');
    const __m256i l_nl    = _mm256_set1_epi8('\n');
    const __m256i l_cr    = _mm256_set1_epi8('\r');
    
    __m256i l_ws1 = _mm256_cmpeq_epi8(a_chunk, l_space);
    __m256i l_ws2 = _mm256_cmpeq_epi8(a_chunk, l_tab);
    __m256i l_ws3 = _mm256_cmpeq_epi8(a_chunk, l_nl);
    __m256i l_ws4 = _mm256_cmpeq_epi8(a_chunk, l_cr);
    
    __m256i l_whitespace = _mm256_or_si256(l_ws1, l_ws2);
    l_whitespace = _mm256_or_si256(l_whitespace, l_ws3);
    l_whitespace = _mm256_or_si256(l_whitespace, l_ws4);
    
    return (uint32_t)_mm256_movemask_epi8(l_whitespace);
}

/**
 * @brief Find quote characters in AVX2 chunk
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is "
 */
static inline uint32_t s_simd_find_quotes_avx2(__m256i a_chunk)
{
    const __m256i l_quote = _mm256_set1_epi8('"');
    __m256i l_quotes = _mm256_cmpeq_epi8(a_chunk, l_quote);
    return (uint32_t)_mm256_movemask_epi8(l_quotes);
}

/**
 * @brief Find backslash characters in AVX2 chunk
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is \
 */
static inline uint32_t s_simd_find_backslash_avx2(__m256i a_chunk)
{
    const __m256i l_backslash = _mm256_set1_epi8('\\');
    __m256i l_escapes = _mm256_cmpeq_epi8(a_chunk, l_backslash);
    return (uint32_t)_mm256_movemask_epi8(l_escapes);
}

/* ========================================================================== */
/*                         STRING HANDLING                                    */
/* ========================================================================== */

/**
 * @brief Compute escaped quotes bitmask
 * @details A quote preceded by ODD number of backslashes is escaped
 * @param[in] a_backslash_mask Bitmask of backslash positions
 * @param[in] a_quote_mask Bitmask of quote positions
 * @param[in,out] a_prev_backslash_run Backslash run carry from previous chunk
 * @return Bitmask of escaped quotes
 */
static inline uint32_t s_compute_escaped_quotes(
    uint32_t a_backslash_mask,
    uint32_t a_quote_mask,
    uint32_t *a_prev_backslash_run
)
{
    uint32_t l_escaped = 0;
    uint32_t l_backslash_run = *a_prev_backslash_run;
    
    for (int l_bit = 0; l_bit < AVX2_CHUNK_SIZE; l_bit++) {
        if (a_backslash_mask & (1U << l_bit)) {
            l_backslash_run++;
        } else if (a_quote_mask & (1U << l_bit)) {
            // Quote found: check if escaped (odd number of backslashes)
            if (l_backslash_run % 2 == 1) {
                l_escaped |= (1U << l_bit);
            }
            l_backslash_run = 0;
        } else {
            l_backslash_run = 0;
        }
    }
    
    *a_prev_backslash_run = l_backslash_run;
    return l_escaped;
}

/* ========================================================================== */
/*                         MAIN AVX2 TOKENIZER                                */
/* ========================================================================== */

/**
 * @brief AVX2-optimized Stage 1 tokenization
 * @details Hybrid SIMD+scalar approach for maximum performance
 * 
 * Strategy:
 * 1. SIMD finds structural chars and quotes in chunks
 * 2. Fall back to scalar for complex parsing (strings, numbers, literals)
 * 3. Process tail bytes with reference implementation
 * 
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS on success, error code on failure
 */
int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1 || !a_stage1->input) {
        log_it(L_ERROR, "Invalid stage1 or input");
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    log_it(L_DEBUG, "Starting AVX2 HYBRID Stage 1 tokenization (%zu bytes)", a_stage1->input_len);
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    // State
    bool l_in_string = false;
    uint32_t l_prev_backslash_run = 0;
    size_t l_pos = 0;
    size_t l_skip_until = 0;  // Hybrid: track values extending beyond chunk (slow path)
    
    // Process full AVX2 chunks (32 bytes)
    while (l_pos + AVX2_CHUNK_SIZE <= l_input_len) {
        // HYBRID SLOW PATH: Process boundary value from previous chunk
        // NOTE: Don't process if we're inside a string
        if (l_skip_until > l_pos && !l_in_string) {
            l_skip_until = s_process_boundary_values(a_stage1, l_pos, 
                                                     l_pos + AVX2_CHUNK_SIZE, 
                                                     l_skip_until);
        }
        
        // Load 32 bytes
        __m256i l_chunk = _mm256_loadu_si256((const __m256i*)(l_input + l_pos));
        
        // Classify characters (parallel)
        uint32_t l_structural_mask = s_simd_find_structural_avx2(l_chunk);
        uint32_t l_whitespace_mask = s_simd_find_whitespace_avx2(l_chunk);
        uint32_t l_quote_mask = s_simd_find_quotes_avx2(l_chunk);
        uint32_t l_backslash_mask = s_simd_find_backslash_avx2(l_chunk);
        
        // Compute escaped quotes
        uint32_t l_escaped_quotes = s_compute_escaped_quotes(
            l_backslash_mask,
            l_quote_mask,
            &l_prev_backslash_run
        );
        
        // Real quotes (not escaped)
        uint32_t l_real_quotes = l_quote_mask & ~l_escaped_quotes;
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcount(l_structural_mask);
        a_stage1->whitespace_chars += __builtin_popcount(l_whitespace_mask);
        
        // ZERO-OVERHEAD HYBRID: Process ALL tokens in POSITION ORDER
        // This ensures correct ordering without any sorting or buffering!
        for (int i = 0; i < AVX2_CHUNK_SIZE; i++) {
            size_t l_abs_pos = l_pos + i;
            
            // Check if we've finished skipping an extended string/value
            if (l_in_string && l_abs_pos == l_skip_until) {
                l_in_string = false;  // String ended
                l_skip_until = 0;     // Reset skip marker
            }
            
            // Skip if already processed by boundary handler or inside extended string
            if (l_abs_pos < l_skip_until) continue;
            
            uint32_t l_bit = (1U << i);
            
            // Priority 1: Quotes/Strings (can extend beyond chunk, affects state)
            if (l_real_quotes & l_bit) {
                if (!l_in_string) {
                    // String start
                    size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                    if (l_str_end == l_abs_pos) {
                        return a_stage1->error_code;
                    }
                    
                    // FAST PATH: String within chunk
                    if (l_str_end <= l_pos + AVX2_CHUNK_SIZE) {
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
                        l_in_string = true;  // String extends, so we're inside it for next chunks
                        break;  // End processing this chunk
                    }
                } else {
                    // String end (closing quote) - already processed when we opened the string
                    l_in_string = false;
                }
                continue;
            }
            
            // Skip processing inside strings
            if (l_in_string) continue;
            
            // Priority 2: Structural (always single byte, no boundary issues)
            if (l_structural_mask & l_bit) {
                uint8_t l_char = l_input[l_abs_pos];
                dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, 0,
                                          TOKEN_TYPE_STRUCTURAL, l_char);
                continue;  // Next position
            }
            
            // Priority 3: Whitespace (skip)
            if (l_whitespace_mask & l_bit) {
                continue;  // Next position
            }
            
            // Priority 4: Numbers/Literals (can extend beyond chunk)
            uint8_t l_char = l_input[l_abs_pos];
            
            // Number
            if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
                size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, l_abs_pos);
                if (l_num_end > l_abs_pos) {
                    // FAST PATH: Number within chunk
                    if (l_num_end <= l_pos + AVX2_CHUNK_SIZE) {
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
                    if (l_lit_end <= l_pos + AVX2_CHUNK_SIZE) {
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
        l_pos += AVX2_CHUNK_SIZE;
    }
    
    // TAIL PROCESSING: Process remaining bytes (< 32 bytes)
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
    
    log_it(L_INFO, "AVX2 HYBRID Stage 1 complete: %zu tokens, %zu structural, %zu whitespace",
           a_stage1->indices_count,
           a_stage1->structural_chars,
           a_stage1->whitespace_chars);
    
    return STAGE1_SUCCESS;
}

#endif // __AVX2__
