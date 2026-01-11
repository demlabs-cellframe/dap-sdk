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
 * @file dap_json_stage1_{{ARCH_LOWER}}.c
 * @brief SimdJSON-style Stage 1 tokenization with {{ARCH_NAME}} SIMD optimization
 * @details Auto-generated from template using dap_tpl
 * 
 * Performance target: {{PERF_TARGET}}
 * 
 * @date 2026-01-11
 * @generated
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
{{ARCH_INCLUDES}}

#include "dap_common.h"
#include "dap_json.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_{{ARCH_LOWER}}"

/* ========================================================================== */
/*                    {{ARCH_NAME}}-SPECIFIC CONFIGURATION                    */
/* ========================================================================== */

#define CHUNK_SIZE {{CHUNK_SIZE}}
#define VECTOR_TYPE {{VECTOR_TYPE}}
#define MASK_TYPE {{MASK_TYPE}}

{{#if USE_NEON_HELPER}}
// ============================================================================
// ARM NEON-specific arch helpers
// Separate header for architecture-specific optimizations (movemask, etc)
// ============================================================================
#include "dap_json_stage1_{{ARCH_LOWER}}_arch.h"
{{/if}}

/**
 * @brief Bitmap masks for character classification
 */
typedef struct {
    MASK_TYPE structural;   /* { } [ ] : , */
    MASK_TYPE whitespace;   /* space, tab, \r, \n */
    MASK_TYPE quote;        /* " */
    MASK_TYPE backslash;    /* \ */
} dap_json_bitmaps_{{ARCH_LOWER}}_t;

/* ========================================================================== */
/*                    SIMD PRIMITIVES - {{ARCH_NAME}}                         */
/* ========================================================================== */

/**
 * @brief SIMD: Classify chunk into bitmaps
 */
{{#if TARGET_ATTR}}
__attribute__((target("{{TARGET_ATTR}}")))
{{/if}}
static dap_json_bitmaps_{{ARCH_LOWER}}_t s_classify_chunk_{{ARCH_LOWER}}(const uint8_t *a_chunk)
{
    dap_json_bitmaps_{{ARCH_LOWER}}_t bitmaps = {0};
    
    // Load chunk
    VECTOR_TYPE chunk = {{LOADU}}((const VECTOR_TYPE *)a_chunk);
    
    // Create comparison vectors
    VECTOR_TYPE v_space = {{SET1_EPI8}}(' ');
    VECTOR_TYPE v_tab = {{SET1_EPI8}}('\t');
    VECTOR_TYPE v_cr = {{SET1_EPI8}}('\r');
    VECTOR_TYPE v_lf = {{SET1_EPI8}}('\n');
    VECTOR_TYPE v_quote = {{SET1_EPI8}}('"');
    VECTOR_TYPE v_backslash = {{SET1_EPI8}}('\\');
    VECTOR_TYPE v_op_brace = {{SET1_EPI8}}('{');
    VECTOR_TYPE v_cl_brace = {{SET1_EPI8}}('}');
    VECTOR_TYPE v_op_bracket = {{SET1_EPI8}}('[');
    VECTOR_TYPE v_cl_bracket = {{SET1_EPI8}}(']');
    VECTOR_TYPE v_colon = {{SET1_EPI8}}(':');
    VECTOR_TYPE v_comma = {{SET1_EPI8}}(',');
    
{{#if USE_AVX512_MASK}}
    // AVX-512: Direct mask comparisons (no movemask needed)
    MASK_TYPE whitespace = {{CMPEQ_EPI8_MASK}}(chunk, v_space);
    whitespace |= {{CMPEQ_EPI8_MASK}}(chunk, v_tab);
    whitespace |= {{CMPEQ_EPI8_MASK}}(chunk, v_cr);
    whitespace |= {{CMPEQ_EPI8_MASK}}(chunk, v_lf);
    
    MASK_TYPE quote = {{CMPEQ_EPI8_MASK}}(chunk, v_quote);
    MASK_TYPE backslash = {{CMPEQ_EPI8_MASK}}(chunk, v_backslash);
    
    MASK_TYPE structural = {{CMPEQ_EPI8_MASK}}(chunk, v_op_brace);
    structural |= {{CMPEQ_EPI8_MASK}}(chunk, v_cl_brace);
    structural |= {{CMPEQ_EPI8_MASK}}(chunk, v_op_bracket);
    structural |= {{CMPEQ_EPI8_MASK}}(chunk, v_cl_bracket);
    structural |= {{CMPEQ_EPI8_MASK}}(chunk, v_colon);
    structural |= {{CMPEQ_EPI8_MASK}}(chunk, v_comma);
    
    bitmaps.whitespace = whitespace;
    bitmaps.quote = quote;
    bitmaps.backslash = backslash;
    bitmaps.structural = structural;
{{else}}
    // SSE2/AVX2/NEON: Vector comparisons + movemask
    VECTOR_TYPE whitespace = {{OR}}(
        {{OR}}({{CMPEQ_EPI8}}(chunk, v_space), {{CMPEQ_EPI8}}(chunk, v_tab)),
        {{OR}}({{CMPEQ_EPI8}}(chunk, v_cr), {{CMPEQ_EPI8}}(chunk, v_lf))
    );
    
    VECTOR_TYPE quote = {{CMPEQ_EPI8}}(chunk, v_quote);
    VECTOR_TYPE backslash = {{CMPEQ_EPI8}}(chunk, v_backslash);
    
    VECTOR_TYPE structural = {{OR}}(
        {{OR}}(
            {{OR}}({{CMPEQ_EPI8}}(chunk, v_op_brace), {{CMPEQ_EPI8}}(chunk, v_cl_brace)),
            {{OR}}({{CMPEQ_EPI8}}(chunk, v_op_bracket), {{CMPEQ_EPI8}}(chunk, v_cl_bracket))
        ),
        {{OR}}({{CMPEQ_EPI8}}(chunk, v_colon), {{CMPEQ_EPI8}}(chunk, v_comma))
    );
    
    // Convert to bitmasks
    bitmaps.whitespace = (MASK_TYPE){{MOVEMASK_EPI8}}(whitespace);
    bitmaps.quote = (MASK_TYPE){{MOVEMASK_EPI8}}(quote);
    bitmaps.backslash = (MASK_TYPE){{MOVEMASK_EPI8}}(backslash);
    bitmaps.structural = (MASK_TYPE){{MOVEMASK_EPI8}}(structural);
{{/if}}
    
    return bitmaps;
}

/**
 * @brief Helper: Add token with capacity check (inline for speed)
 */
static inline int s_add_token_{{ARCH_LOWER}}(
    dap_json_stage1_t *a_stage1, uint32_t a_pos, uint32_t a_len,
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

/* ========================================================================== */
/*                    MAIN TOKENIZATION - {{ARCH_NAME}}                       */
/* ========================================================================== */

/**
 * @brief Full SIMD-optimized Stage 1 tokenization ({{ARCH_NAME}})
 * @details Three-phase processing:
 *          Phase 1: SIMD chunk classification + structural extraction
 *          Phase 2: Sequential value token extraction (with SIMD hints)
 *          Phase 3: Tail processing
 * 
 * Performance: {{PERF_TARGET}}
 */
{{#if TARGET_ATTR}}
__attribute__((target("{{TARGET_ATTR}}")))
{{/if}}
int dap_json_stage1_run_{{ARCH_LOWER}}(dap_json_stage1_t *a_stage1)
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
    
    debug_if(dap_json_get_debug(), "Starting {{ARCH_NAME}} SimdJSON Stage 1 tokenization (%zu bytes)", input_len);
    
    // Phase 1 & 2: SIMD-accelerated chunk processing
    size_t pos = 0;
    const size_t chunk_end = (input_len / CHUNK_SIZE) * CHUNK_SIZE;
    
    while (pos < chunk_end) {
        // SIMD: Classify chunk in parallel
        dap_json_bitmaps_{{ARCH_LOWER}}_t bitmaps = s_classify_chunk_{{ARCH_LOWER}}(input + pos);
        
        // Process chunk sequentially in position order, using bitmaps as hints
        size_t chunk_pos = pos;
        size_t chunk_limit = pos + CHUNK_SIZE;
        
        // CRITICAL: Clamp chunk_limit to input_len to avoid reading beyond buffer
        if (chunk_limit > input_len) {
            chunk_limit = input_len;
        }
        
        while (chunk_pos < chunk_limit) {
            uint8_t c = input[chunk_pos];
            size_t bit_offset = chunk_pos - pos;
            
            // Fast path: Check bitmap for whitespace (skip without token)
            if (bit_offset < CHUNK_SIZE && (bitmaps.whitespace & (((MASK_TYPE)1) << bit_offset))) {
                chunk_pos++;
                continue;
            }
            
            // Fast path: Check bitmap for structural (add token immediately)
            if (bit_offset < CHUNK_SIZE && (bitmaps.structural & (((MASK_TYPE)1) << bit_offset))) {
                int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)chunk_pos, 0, TOKEN_TYPE_STRUCTURAL, c);
                if (ret != STAGE1_SUCCESS) return ret;
                a_stage1->structural_chars++;
                chunk_pos++;
                continue;
            }
            
            // Slow path: Skip whitespace (not in chunk or missed by bitmap)
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                chunk_pos++;
                continue;
            }
            
            // String (can span beyond chunk)
            if (c == '"') {
                size_t end = dap_json_stage1_scan_string_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t str_len = end - chunk_pos;
                int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)chunk_pos, (uint32_t)str_len,
                                     TOKEN_TYPE_STRING, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->string_count++;
                a_stage1->string_chars += str_len;
                
                // Handle spanning
                if (end >= chunk_limit) {
                    pos = end;
                    chunk_pos = chunk_limit;
                } else {
                    chunk_pos = end;
                }
                continue;
            }
            
            // Number (can span beyond chunk)
            if (c == '-' || (c >= '0' && c <= '9')) {
                size_t end = dap_json_stage1_scan_number_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t num_len = end - chunk_pos;
                int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)chunk_pos, (uint32_t)num_len,
                                     TOKEN_TYPE_NUMBER, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->number_count++;
                
                if (end >= chunk_limit) {
                    pos = end;
                    chunk_pos = chunk_limit;
                } else {
                    chunk_pos = end;
                }
                continue;
            }
            
            // Literal (can span beyond chunk)
            if (c == 't' || c == 'f' || c == 'n') {
                size_t end = dap_json_stage1_scan_literal_ref(a_stage1, chunk_pos);
                if (end == chunk_pos) return a_stage1->error_code;
                
                size_t lit_len = end - chunk_pos;
                int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)chunk_pos, (uint32_t)lit_len,
                                     TOKEN_TYPE_LITERAL, 0);
                if (ret != STAGE1_SUCCESS) return ret;
                
                a_stage1->literal_count++;
                
                if (end >= chunk_limit) {
                    pos = end;
                    chunk_pos = chunk_limit;
                } else {
                    chunk_pos = end;
                }
                continue;
            }
            
            // Invalid character
            a_stage1->error_code = STAGE1_ERROR_INVALID_UTF8;
            a_stage1->error_position = chunk_pos;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Invalid character 0x%02X at position %zu", c, chunk_pos);
            return STAGE1_ERROR_INVALID_UTF8;
        }
        
        // Move to next chunk (if not already moved by spanning token)
        if (pos < chunk_limit) {
            pos = chunk_limit;
        }
    }
    
    // Phase 3: Tail processing (< CHUNK_SIZE bytes)
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
            int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)pos, 0, TOKEN_TYPE_STRUCTURAL, c);
            if (ret != STAGE1_SUCCESS) return ret;
            a_stage1->structural_chars++;
            pos++;
        }
        // String
        else if (c == '"') {
            size_t end = dap_json_stage1_scan_string_ref(a_stage1, pos);
            if (end == pos) return a_stage1->error_code;
            
            size_t str_len = end - pos;
            int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)pos, (uint32_t)str_len,
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
            int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)pos, (uint32_t)num_len,
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
            int ret = s_add_token_{{ARCH_LOWER}}(a_stage1, (uint32_t)pos, (uint32_t)lit_len,
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
    
    debug_if(dap_json_get_debug(), "{{ARCH_NAME}} SimdJSON Stage 1 complete: %zu tokens (%zu structural, %zu strings, %zu numbers, %zu literals)",
             a_stage1->indices_count, a_stage1->structural_chars, a_stage1->string_count,
             a_stage1->number_count, a_stage1->literal_count);
    
    return STAGE1_SUCCESS;
}

/**
 * @brief Enable/disable detailed debug logging for {{ARCH_NAME}} implementation
 * @param a_enable true to enable detailed logging
 */
void dap_json_stage1_{{ARCH_LOWER}}_set_debug(bool a_enable)
{
    // {{ARCH_NAME}} now uses global dap_json_get_debug(), no local flag needed
    (void)a_enable;
}

