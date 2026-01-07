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
 * Architecture: Hybrid SIMD+scalar approach
 * - SIMD: Fast detection of token boundaries (structural chars, strings, numbers, literals)
 * - Scalar: Precise validation (UTF-8, number parsing, literal matching)
 * 
 * AVX-512 specific:
 * - Chunk size: 64 bytes (512-bit)
 * - Intrinsics: _mm512_* + mask operations
 * - Available on: Intel Ice Lake+, AMD Zen 4+ (Ryzen 7000+)
 * - Target: 6-8 GB/s single-core tokenization
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
/*                        MAIN AVX-512 IMPLEMENTATION                         */
/* ========================================================================== */

/**
 * @brief AVX-512 implementation of Stage 1 tokenization
 */
int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1)
{
#if !defined(__AVX512F__) || !defined(__AVX512DQ__) || !defined(__AVX512BW__)
    // Fallback to AVX2 if AVX-512 not available
    log_it(L_WARNING, "AVX-512 not available at compile time, using AVX2 fallback");
    return dap_json_stage1_run_avx2(a_stage1);
#else
    
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    log_it(L_DEBUG, "Starting AVX-512 Stage 1 tokenization (%zu bytes)", l_input_len);
    
    // Process in 64-byte chunks
    size_t l_pos = 0;
    bool l_in_string = false;
    uint64_t l_prev_backslash_run = 0;
    
    // Main SIMD loop - process 64 bytes at a time
    while (l_pos + AVX512_CHUNK_SIZE <= l_input_len) {
        // Load 64 bytes
        __m512i l_chunk = _mm512_loadu_si512((const __m512i*)(l_input + l_pos));
        
        // Classify all bytes in parallel using mask operations
        __mmask64 l_struct_mask = s_classify_structural_avx512(l_chunk);
        __mmask64 l_ws_mask     = s_classify_whitespace_avx512(l_chunk);
        __mmask64 l_quote_mask  = s_classify_quotes_avx512(l_chunk);
        __mmask64 l_bs_mask     = s_classify_backslashes_avx512(l_chunk);
        
        // Compute real quotes
        uint64_t l_real_quotes = s_compute_escaped_quotes_avx512(l_quote_mask, l_bs_mask, &l_prev_backslash_run);
        
        // Update statistics
        a_stage1->structural_chars += __builtin_popcountll(l_struct_mask);
        a_stage1->whitespace_chars += __builtin_popcountll(l_ws_mask);
        
        // Process quotes
        if (l_real_quotes) {
            int l_bit_idx = __builtin_ctzll(l_real_quotes);
            while (l_bit_idx < 64) {
                size_t l_abs_pos = l_pos + l_bit_idx;
                
                if (!l_in_string) {
                    size_t l_str_end = dap_json_stage1_scan_string_ref(a_stage1, l_abs_pos);
                    if (l_str_end == l_abs_pos) {
                        return a_stage1->error_code;
                    }
                    
                    size_t l_str_len = l_str_end - l_abs_pos;
                    dap_json_stage1_add_token(a_stage1, (uint32_t)l_abs_pos, (uint32_t)l_str_len,
                                              TOKEN_TYPE_STRING, 0);
                    
                    if (l_str_end > l_pos + AVX512_CHUNK_SIZE) {
                        l_pos = l_str_end;
                        goto next_chunk;
                    }
                }
                
                l_in_string = !l_in_string;
                
                l_real_quotes &= ~(1ULL << l_bit_idx);
                if (!l_real_quotes) break;
                l_bit_idx = __builtin_ctzll(l_real_quotes);
            }
        }
        
        // Process structural characters
        if (!l_in_string && l_struct_mask) {
            for (int i = 0; i < 64; i++) {
                if (l_struct_mask & (1ULL << i)) {
                    uint8_t l_char = l_input[l_pos + i];
                    dap_json_stage1_add_token(a_stage1, (uint32_t)(l_pos + i), 0,
                                              TOKEN_TYPE_STRUCTURAL, l_char);
                }
            }
        }
        
        // Process numbers and literals
        if (!l_in_string) {
            uint64_t l_value_mask = ~(l_struct_mask | l_ws_mask | l_quote_mask);
            
            if (l_value_mask) {
                for (int i = 0; i < 64; i++) {
                    if (l_value_mask & (1ULL << i)) {
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
        l_pos += AVX512_CHUNK_SIZE;
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
    
    log_it(L_INFO, "AVX-512 Stage 1 complete: %zu tokens (%zu structural, %zu strings, %zu numbers, %zu literals)",
           a_stage1->indices_count,
           a_stage1->indices_count - l_str_count - l_num_count - l_lit_count,
           l_str_count, l_num_count, l_lit_count);
    
    return STAGE1_SUCCESS;
    
#endif // AVX-512
}

