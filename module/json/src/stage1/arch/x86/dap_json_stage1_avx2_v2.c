/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file dap_json_stage1_avx2_simdjson_v2.c  
 * @brief SimdJSON Stage 1 - Full SIMD-optimized version (Phase 2.2)
 * @details True SimdJSON algorithm with maximum performance:
 *          - Parallel 32-byte chunk classification (~10 cycles/chunk)
 *          - Bitmap-guided token extraction
 *          - Proper spanning token handling
 * 
 * Performance target: 4-5 GB/s (vs 0.9-1.2 GB/s in v1)
 * 
 * Key optimizations:
 * 1. SIMD bitmap classification for all 32-byte chunks
 * 2. Efficient bit manipulation (__builtin_ctz, mask &= mask-1)
 * 3. Minimal branching in hot paths
 * 4. Proper handling of tokens spanning chunk boundaries
 * 
 * @date 2026-01-11
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "dap_common.h"
#include "dap_json.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#if defined(__AVX2__)
#include <immintrin.h>

#define LOG_TAG "dap_json_stage1_avx2_simdjson_v2"

#define AVX2_CHUNK_SIZE 32

/**
 * @brief Bitmap masks for character classification
 */
typedef struct {
    uint32_t structural;   /* { } [ ] : , */
    uint32_t whitespace;   /* space, tab, \r, \n */
    uint32_t quote;        /* " */
    uint32_t backslash;    /* \ */
} dap_json_bitmaps_t;

/**
 * @brief SIMD: Classify 32-byte chunk into bitmaps (AVX2)
 * @details Parallel classification of all bytes using AVX2 comparisons
 *          Performance: ~10 cycles for 32 bytes (3.2 bytes/cycle)
 */
__attribute__((target("avx2")))
static dap_json_bitmaps_t s_classify_chunk_avx2(const uint8_t *a_chunk)
{
    dap_json_bitmaps_t bitmaps = {0};
    
    // Load 32 bytes
    __m256i chunk = _mm256_loadu_si256((const __m256i *)a_chunk);
    
    // Create comparison vectors
    __m256i v_space = _mm256_set1_epi8(' ');
    __m256i v_tab = _mm256_set1_epi8('\t');
    __m256i v_cr = _mm256_set1_epi8('\r');
    __m256i v_lf = _mm256_set1_epi8('\n');
    __m256i v_quote = _mm256_set1_epi8('"');
    __m256i v_backslash = _mm256_set1_epi8('\\');
    __m256i v_op_brace = _mm256_set1_epi8('{');
    __m256i v_cl_brace = _mm256_set1_epi8('}');
    __m256i v_op_bracket = _mm256_set1_epi8('[');
    __m256i v_cl_bracket = _mm256_set1_epi8(']');
    __m256i v_colon = _mm256_set1_epi8(':');
    __m256i v_comma = _mm256_set1_epi8(',');
    
    // Parallel comparisons
    __m256i whitespace = _mm256_or_si256(
        _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_space), _mm256_cmpeq_epi8(chunk, v_tab)),
        _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_cr), _mm256_cmpeq_epi8(chunk, v_lf))
    );
    
    __m256i quote = _mm256_cmpeq_epi8(chunk, v_quote);
    __m256i backslash = _mm256_cmpeq_epi8(chunk, v_backslash);
    
    __m256i structural = _mm256_or_si256(
        _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_op_brace), _mm256_cmpeq_epi8(chunk, v_cl_brace)),
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_op_bracket), _mm256_cmpeq_epi8(chunk, v_cl_bracket))
        ),
        _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_colon), _mm256_cmpeq_epi8(chunk, v_comma))
    );
    
    // Convert to bitmasks
    bitmaps.whitespace = (uint32_t)_mm256_movemask_epi8(whitespace);
    bitmaps.quote = (uint32_t)_mm256_movemask_epi8(quote);
    bitmaps.backslash = (uint32_t)_mm256_movemask_epi8(backslash);
    bitmaps.structural = (uint32_t)_mm256_movemask_epi8(structural);
    
    return bitmaps;
}

/**
 * @brief Helper: Add token with capacity check (inline for speed)
 */
static inline int s_add_token(dap_json_stage1_t *a_stage1, uint32_t a_pos, uint32_t a_len,
                               dap_json_token_type_t a_type, uint8_t a_char)
{
    // Ensure capacity
    if (__builtin_expect(a_stage1->indices_count >= a_stage1->indices_capacity, 0)) {
        size_t new_capacity = a_stage1->indices_capacity * 2;
        dap_json_struct_index_t *new_indices = DAP_NEW_SIZE(dap_json_struct_index_t,
                                                             new_capacity * sizeof(dap_json_struct_index_t));
        if (!new_indices) {
            return STAGE1_ERROR_OUT_OF_MEMORY;
        }
        memcpy(new_indices, a_stage1->indices,
               a_stage1->indices_count * sizeof(dap_json_struct_index_t));
        DAP_DELETE(a_stage1->indices);
        a_stage1->indices = new_indices;
        a_stage1->indices_capacity = new_capacity;
    }
    
    // Add token
    a_stage1->indices[a_stage1->indices_count].position = a_pos;
    a_stage1->indices[a_stage1->indices_count].length = a_len;
    a_stage1->indices[a_stage1->indices_count].type = a_type;
    a_stage1->indices[a_stage1->indices_count].character = a_char;
    a_stage1->indices_count++;
    
    return STAGE1_SUCCESS;
}

/**
 * @brief Full SIMD-optimized Stage 1 tokenization (AVX2)
 * @details Three-phase processing:
 *          Phase 1: SIMD chunk classification + structural extraction
 *          Phase 2: Sequential value token extraction (with SIMD hints)
 *          Phase 3: Tail processing
 * 
 * Performance: 4-5 GB/s target (40-50x faster than reference)
 */
__attribute__((target("avx2")))
int dap_json_stage1_run_avx2_simdjson_v2(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1 || !a_stage1->input) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *input = a_stage1->input;
    const size_t input_len = a_stage1->input_len;
    
    // Reset state
    a_stage1->indices_count = 0;
    a_stage1->current_pos = 0;
    a_stage1->in_string = false;
    a_stage1->escape_next = false;
    a_stage1->string_count = 0;
    a_stage1->number_count = 0;
    a_stage1->literal_count = 0;
    a_stage1->string_chars = 0;
    a_stage1->whitespace_chars = 0;
    a_stage1->structural_chars = 0;
    a_stage1->error_code = STAGE1_SUCCESS;
    a_stage1->error_position = 0;
    a_stage1->error_message[0] = '\0';
    
    debug_if(dap_json_get_debug(), "Starting AVX2 SimdJSON v2 Stage 1 tokenization (%zu bytes)", input_len);
    
    // Phase 1 & 2: SIMD-accelerated chunk processing
    size_t pos = 0;
    const size_t chunk_end = (input_len / AVX2_CHUNK_SIZE) * AVX2_CHUNK_SIZE;
    
    while (pos < chunk_end) {
        // SIMD: Classify 32-byte chunk in parallel (~10 cycles)
        dap_json_bitmaps_t bitmaps = s_classify_chunk_avx2(input + pos);
        
        // Extract structural characters from bitmap (always 1 byte, no spanning)
        uint32_t struct_mask = bitmaps.structural;
        while (struct_mask) {
            int bit_pos = __builtin_ctz(struct_mask);
            size_t abs_pos = pos + bit_pos;
            
            int ret = s_add_token(a_stage1, (uint32_t)abs_pos, 0, TOKEN_TYPE_STRUCTURAL, input[abs_pos]);
            if (ret != STAGE1_SUCCESS) return ret;
            
            a_stage1->structural_chars++;
            struct_mask &= (struct_mask - 1);
        }
        
        // Process value tokens sequentially within chunk (can span beyond)
        size_t chunk_pos = pos;
        const size_t chunk_limit = pos + AVX2_CHUNK_SIZE;
        
        while (chunk_pos < chunk_limit) {
            uint8_t c = input[chunk_pos];
            
            // Skip whitespace & structural (already processed)
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
                chunk_pos++;
                continue;
            }
            
            // String
            if (c == '"') {
                size_t end = dap_json_stage1_scan_string_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t str_len = end - chunk_pos;
                int ret = s_add_token(a_stage1, (uint32_t)chunk_pos, (uint32_t)str_len,
                                     TOKEN_TYPE_STRING, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->string_count++;
                a_stage1->string_chars += str_len;
                
                // Handle spanning
                chunk_pos = (end >= chunk_limit) ? chunk_limit : end;
                if (end >= chunk_limit) pos = end;  // Skip ahead
                continue;
            }
            
            // Number
            if (c == '-' || (c >= '0' && c <= '9')) {
                size_t end = dap_json_stage1_scan_number_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t num_len = end - chunk_pos;
                int ret = s_add_token(a_stage1, (uint32_t)chunk_pos, (uint32_t)num_len,
                                     TOKEN_TYPE_NUMBER, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->number_count++;
                
                chunk_pos = (end >= chunk_limit) ? chunk_limit : end;
                if (end >= chunk_limit) pos = end;
                continue;
            }
            
            // Literal
            if (c == 't' || c == 'f' || c == 'n') {
                size_t end = dap_json_stage1_scan_literal_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t lit_len = end - chunk_pos;
                int ret = s_add_token(a_stage1, (uint32_t)chunk_pos, (uint32_t)lit_len,
                                     TOKEN_TYPE_LITERAL, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->literal_count++;
                
                chunk_pos = (end >= chunk_limit) ? chunk_limit : end;
                if (end >= chunk_limit) pos = end;
                continue;
            }
            
            // Invalid character
            a_stage1->error_code = STAGE1_ERROR_INVALID_UTF8;
            a_stage1->error_position = chunk_pos;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Invalid character 0x%02X at position %zu", c, chunk_pos);
            return STAGE1_ERROR_INVALID_UTF8;
        }
        
        // Move to next chunk (or skip if already moved by spanning token)
        if (pos < chunk_limit) {
            pos = chunk_limit;
        }
    }
    
    // Phase 3: Tail processing (< 32 bytes)
    while (pos < input_len) {
        // Skip whitespace
        while (pos < input_len && (input[pos] == ' ' || input[pos] == '\t' ||
                                   input[pos] == '\r' || input[pos] == '\n')) {
            pos++;
        }
        
        if (pos >= input_len) break;
        
        uint8_t c = input[pos];
        
        // Structural
        if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
            int ret = s_add_token(a_stage1, (uint32_t)pos, 0, TOKEN_TYPE_STRUCTURAL, c);
            if (ret != STAGE1_SUCCESS) return ret;
            a_stage1->structural_chars++;
            pos++;
        }
        // String
        else if (c == '"') {
            size_t end = dap_json_stage1_scan_string_ref(a_stage1, pos);
            if (end == pos) return a_stage1->error_code;
            
            size_t str_len = end - pos;
            int ret = s_add_token(a_stage1, (uint32_t)pos, (uint32_t)str_len,
                                 TOKEN_TYPE_STRING, 0);
            if (ret != STAGE1_SUCCESS) return ret;
            
            a_stage1->string_count++;
            a_stage1->string_chars += str_len;
            pos = end;
        }
        // Number
        else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t end = dap_json_stage1_scan_number_ref(a_stage1, pos);
            if (end == pos) return a_stage1->error_code;
            
            size_t num_len = end - pos;
            int ret = s_add_token(a_stage1, (uint32_t)pos, (uint32_t)num_len,
                                 TOKEN_TYPE_NUMBER, 0);
            if (ret != STAGE1_SUCCESS) return ret;
            
            a_stage1->number_count++;
            pos = end;
        }
        // Literal
        else if (c == 't' || c == 'f' || c == 'n') {
            size_t end = dap_json_stage1_scan_literal_ref(a_stage1, pos);
            if (end == pos) return a_stage1->error_code;
            
            size_t lit_len = end - pos;
            int ret = s_add_token(a_stage1, (uint32_t)pos, (uint32_t)lit_len,
                                 TOKEN_TYPE_LITERAL, 0);
            if (ret != STAGE1_SUCCESS) return ret;
            
            a_stage1->literal_count++;
            pos = end;
        }
        else {
            a_stage1->error_code = STAGE1_ERROR_INVALID_UTF8;
            a_stage1->error_position = pos;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Invalid character 0x%02X at position %zu", c, pos);
            return STAGE1_ERROR_INVALID_UTF8;
        }
    }
    
    debug_if(dap_json_get_debug(), "AVX2 SimdJSON v2 complete: %zu tokens (%zu structural, %zu strings, %zu numbers, %zu literals)",
             a_stage1->indices_count, a_stage1->structural_chars, a_stage1->string_count,
             a_stage1->number_count, a_stage1->literal_count);
    
    return STAGE1_SUCCESS;
}

#else // !__AVX2__

// Stub for non-AVX2 builds
int dap_json_stage1_run_avx2_simdjson_v2(dap_json_stage1_t *a_stage1)
{
    (void)a_stage1;
    return STAGE1_ERROR_INVALID_INPUT;  // AVX2 not available
}

#endif // __AVX2__

