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
 * Strategy:
 * - SIMD для быстрого поиска structural characters и quotes
 * - Reference functions для value parsing (numbers, literals, strings)
 * - Hybrid approach: SIMD detection + scalar validation
 * 
 * Architecture: Intel/AMD x86-64 with AVX2 support
 */

#ifdef __AVX2__

#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "internal/dap_json_stage1.h"

#define LOG_TAG "dap_json_stage1_avx2"

// AVX2 processes 32 bytes per chunk
#define AVX2_CHUNK_SIZE 32

/* ========================================================================== */
/*                    FORWARD DECLARATIONS (from reference)                   */
/* ========================================================================== */

// These functions are defined in dap_json_stage1_ref.c
// We reuse them for value parsing (numbers, literals, strings)
extern dap_json_char_class_t dap_json_classify_char(uint8_t a_char);
extern bool dap_json_stage1_add_token(
    dap_json_stage1_t *a_stage1,
    uint32_t a_position,
    uint32_t a_length,
    dap_json_token_type_t a_type,
    uint8_t a_character_or_subtype
);

// Scan functions from reference (for validation)
static size_t s_scan_string_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

static size_t s_scan_number_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

static size_t s_scan_literal_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

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
/*          REFERENCE FUNCTIONS (imported for validation)                     */
/* ========================================================================== */

/**
 * @brief Scan string using reference implementation
 * @details Handles escape sequences and returns end position
 * @param[in,out] a_stage1 Stage 1 state
 * @param[in] a_start_pos Position of opening quote
 * @return Position after closing quote, or a_start_pos on error
 */
static size_t s_scan_string_ref(dap_json_stage1_t *a_stage1, size_t a_start_pos)
{
    // This will be implemented by calling reference implementation
    // For now, stub that scans to next unescaped quote
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    
    size_t l_pos = a_start_pos + 1; // Skip opening quote
    bool l_escaped = false;
    
    while (l_pos < l_len) {
        uint8_t l_char = l_input[l_pos];
        
        if (l_escaped) {
            l_escaped = false;
            l_pos++;
            continue;
        }
        
        if (l_char == '\\') {
            l_escaped = true;
            l_pos++;
            continue;
        }
        
        if (l_char == '"') {
            return l_pos + 1; // Position after closing quote
        }
        
        l_pos++;
    }
    
    // Unterminated string
    a_stage1->error_code = STAGE1_ERROR_UNTERMINATED_STRING;
    a_stage1->error_position = a_start_pos;
    snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
             "Unterminated string");
    return a_start_pos;
}

/**
 * @brief Scan number using reference implementation
 * @param[in,out] a_stage1 Stage 1 state
 * @param[in] a_start_pos Position of first digit/minus
 * @return Position after last number character
 */
static size_t s_scan_number_ref(dap_json_stage1_t *a_stage1, size_t a_start_pos)
{
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    size_t l_pos = a_start_pos;
    
    // Skip minus
    if (l_pos < l_len && l_input[l_pos] == '-') {
        l_pos++;
    }
    
    // Require at least one digit
    if (l_pos >= l_len || !isdigit(l_input[l_pos])) {
        return a_start_pos;
    }
    
    // Skip digits
    while (l_pos < l_len && isdigit(l_input[l_pos])) {
        l_pos++;
    }
    
    // Optional decimal
    if (l_pos < l_len && l_input[l_pos] == '.') {
        l_pos++;
        while (l_pos < l_len && isdigit(l_input[l_pos])) {
            l_pos++;
        }
    }
    
    // Optional exponent
    if (l_pos < l_len && (l_input[l_pos] == 'e' || l_input[l_pos] == 'E')) {
        l_pos++;
        if (l_pos < l_len && (l_input[l_pos] == '+' || l_input[l_pos] == '-')) {
            l_pos++;
        }
        while (l_pos < l_len && isdigit(l_input[l_pos])) {
            l_pos++;
        }
    }
    
    return l_pos;
}

/**
 * @brief Scan literal (true/false/null) using reference implementation
 * @param[in,out] a_stage1 Stage 1 state
 * @param[in] a_start_pos Position of first character (t/f/n)
 * @return Position after literal, or a_start_pos if not a literal
 */
static size_t s_scan_literal_ref(dap_json_stage1_t *a_stage1, size_t a_start_pos)
{
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    
    if (a_start_pos >= l_len) return a_start_pos;
    
    uint8_t l_first = l_input[a_start_pos];
    
    if (l_first == 't' && a_start_pos + 4 <= l_len &&
        memcmp(&l_input[a_start_pos], "true", 4) == 0) {
        return a_start_pos + 4;
    }
    
    if (l_first == 'f' && a_start_pos + 5 <= l_len &&
        memcmp(&l_input[a_start_pos], "false", 5) == 0) {
        return a_start_pos + 5;
    }
    
    if (l_first == 'n' && a_start_pos + 4 <= l_len &&
        memcmp(&l_input[a_start_pos], "null", 4) == 0) {
        return a_start_pos + 4;
    }
    
    return a_start_pos; // Not a literal
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
    
    log_it(L_DEBUG, "Starting AVX2 Stage 1 tokenization (%zu bytes)", a_stage1->input_len);
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    // State
    bool l_in_string = false;
    uint32_t l_prev_backslash_run = 0;
    size_t l_pos = 0;
    
    // Process full AVX2 chunks
    const size_t l_chunk_end = (l_input_len / AVX2_CHUNK_SIZE) * AVX2_CHUNK_SIZE;
    
    while (l_pos < l_chunk_end) {
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
        
        // Process this chunk byte by byte (hybrid approach)
        for (size_t l_offset = 0; l_offset < AVX2_CHUNK_SIZE; l_offset++) {
            size_t l_abs_pos = l_pos + l_offset;
            uint8_t l_char = l_input[l_abs_pos];
            uint32_t l_bit = (1U << l_offset);
            
            // Skip if inside string (except for quotes)
            if (l_in_string) {
                if (l_real_quotes & l_bit) {
                    // End of string
                    l_in_string = false;
                }
                a_stage1->string_chars++;
                continue;
            }
            
            // Outside string: process tokens
            
            // Check for quote (string start)
            if (l_real_quotes & l_bit) {
                size_t l_string_end = s_scan_string_ref(a_stage1, l_abs_pos);
                if (l_string_end == l_abs_pos) {
                    // Error in string parsing
                    return a_stage1->error_code;
                }
                
                // Add string token
                size_t l_string_len = l_string_end - l_abs_pos;
                dap_json_stage1_add_token(
                    a_stage1,
                    (uint32_t)l_abs_pos,
                    (uint32_t)l_string_len,
                    DAP_JSON_TOKEN_TYPE_STRING,
                    0
                );
                
                // Skip to end of string (will be processed in next chunks)
                size_t l_skip = l_string_end - l_abs_pos - 1;
                l_offset += l_skip;
                l_in_string = true;
                continue;
            }
            
            // Check for structural character
            if (l_structural_mask & l_bit) {
                dap_json_stage1_add_token(
                    a_stage1,
                    (uint32_t)l_abs_pos,
                    0,  // Length 0 for structural
                    DAP_JSON_TOKEN_TYPE_STRUCTURAL,
                    l_char
                );
                a_stage1->structural_chars++;
                continue;
            }
            
            // Check for whitespace
            if (l_whitespace_mask & l_bit) {
                a_stage1->whitespace_chars++;
                continue;
            }
            
            // Check for number start
            dap_json_char_class_t l_class = dap_json_classify_char(l_char);
            if (l_class == CHAR_CLASS_DIGIT || l_class == CHAR_CLASS_MINUS) {
                size_t l_num_end = s_scan_number_ref(a_stage1, l_abs_pos);
                if (l_num_end > l_abs_pos) {
                    size_t l_num_len = l_num_end - l_abs_pos;
                    dap_json_stage1_add_token(
                        a_stage1,
                        (uint32_t)l_abs_pos,
                        (uint32_t)l_num_len,
                        DAP_JSON_TOKEN_TYPE_NUMBER,
                        0
                    );
                    
                    // Skip number
                    size_t l_skip = l_num_len - 1;
                    l_offset += l_skip;
                    continue;
                }
            }
            
            // Check for literal start (t, f, n)
            if (l_class == CHAR_CLASS_LETTER) {
                size_t l_lit_end = s_scan_literal_ref(a_stage1, l_abs_pos);
                if (l_lit_end > l_abs_pos) {
                    size_t l_lit_len = l_lit_end - l_abs_pos;
                    
                    // Determine literal type
                    uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                    if (l_char == 't') l_lit_type = DAP_JSON_LITERAL_TRUE;
                    else if (l_char == 'f') l_lit_type = DAP_JSON_LITERAL_FALSE;
                    else if (l_char == 'n') l_lit_type = DAP_JSON_LITERAL_NULL;
                    
                    dap_json_stage1_add_token(
                        a_stage1,
                        (uint32_t)l_abs_pos,
                        (uint32_t)l_lit_len,
                        DAP_JSON_TOKEN_TYPE_LITERAL,
                        l_lit_type
                    );
                    
                    // Skip literal
                    size_t l_skip = l_lit_len - 1;
                    l_offset += l_skip;
                    continue;
                }
            }
            
            // Unknown character - continue
        }
        
        l_pos += AVX2_CHUNK_SIZE;
    }
    
    // Process tail bytes with scalar code (< 32 bytes)
    while (l_pos < l_input_len) {
        uint8_t l_char = l_input[l_pos];
        
        // Use reference parsing for tail
        // (This is simplified - real implementation would reuse above logic)
        dap_json_char_class_t l_class = dap_json_classify_char(l_char);
        
        if (!l_in_string) {
            if (l_char == '"') {
                // Start string
                l_in_string = true;
            } else if (l_class == CHAR_CLASS_STRUCTURAL) {
                dap_json_stage1_add_token(
                    a_stage1,
                    (uint32_t)l_pos,
                    0,
                    DAP_JSON_TOKEN_TYPE_STRUCTURAL,
                    l_char
                );
                a_stage1->structural_chars++;
            } else if (l_class == CHAR_CLASS_WHITESPACE) {
                a_stage1->whitespace_chars++;
            }
        } else {
            // Inside string
            if (l_char == '"' && (l_pos == 0 || l_input[l_pos-1] != '\\')) {
                l_in_string = false;
            }
            a_stage1->string_chars++;
        }
        
        l_pos++;
    }
    
    log_it(L_INFO, "AVX2 Stage 1 complete: %zu tokens, %zu structural, %zu whitespace, %zu string",
           a_stage1->indices_count,
           a_stage1->structural_chars,
           a_stage1->whitespace_chars,
           a_stage1->string_chars);
    
    return STAGE1_SUCCESS;
}

#endif // __AVX2__
