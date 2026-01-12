/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP JSON Native Implementation Team
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
 * @file dap_json_string_simd_{{ARCH_LOWER}}.c
 * @brief SIMD JSON String Scanner - {{ARCH_UPPER}} Implementation
 * 
 * GENERATED FILE - DO NOT EDIT MANUALLY
 * Generated from: dap_json_string_simd.c.tpl
 * Architecture: {{ARCH_UPPER}}
 * 
 * {{ARCH_UPPER}} string scanner - processes {{CHUNK_SIZE}} bytes at once.
 * 
 * Algorithm:
 * 1. Load {{CHUNK_SIZE}} bytes into SIMD register
 * 2. Compare all bytes with '"' in parallel
 * 3. Compare all bytes with '\' in parallel  
 * 4. Combine results with OR
 * 5. Find first match with movemask + ctz
 * 
 * Performance: ~{{CHUNK_SIZE}} chars per cycle (vs 1 char/cycle reference)
 * Expected impact: +{{SPEEDUP}}x for string scanning
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2026-01-12
 */

#include "dap_common.h"
#include "internal/dap_json_string.h"

#include <stdint.h>
#include <stdbool.h>

{{#if USE_X86}}
#ifdef __{{ARCH_UPPER}}__
#include <immintrin.h>

#define LOG_TAG "dap_json_string_{{ARCH_LOWER}}"

/* ========================================================================== */
/*                        {{ARCH_UPPER}} STRING SCANNER                       */
/* ========================================================================== */

/**
 * @brief {{ARCH_UPPER}} SIMD string scanner
 * @details Processes {{CHUNK_SIZE}} bytes at once using {{ARCH_UPPER}} instructions.
 * 
 * Performance: ~{{CHUNK_SIZE}}x faster than character-by-character scanning
 */
bool dap_json_string_scan_{{ARCH_LOWER}}(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
)
{
    if (!a_input || !a_out_string || !a_out_end_offset) {
        return false;
    }
    
    if (a_input_len == 0 || a_input[0] != '"') {
        log_it(L_ERROR, "Expected opening quote");
        return false;
    }
    
    const uint32_t l_string_start = 1; // after opening quote
    uint32_t l_pos = l_string_start;
    bool l_has_escape = false;
    
    // SIMD: {{CHUNK_SIZE}}-byte chunks
    const size_t l_chunk_size = {{CHUNK_SIZE}};
    
    // Prepare comparison vectors
    const {{SIMD_TYPE}} l_quote_vec = {{SET1_INTRINSIC}}('"');
    const {{SIMD_TYPE}} l_backslash_vec = {{SET1_INTRINSIC}}('\\');
    
    // Process {{CHUNK_SIZE}}-byte chunks
    while (l_pos + l_chunk_size <= a_input_len) {
        // Load {{CHUNK_SIZE}} bytes
        {{SIMD_TYPE}} l_chunk = {{LOAD_INTRINSIC}}((const {{SIMD_TYPE}}*)(a_input + l_pos));
        
        // Find quotes: all bytes == '"'
        {{SIMD_TYPE}} l_quote_mask = {{CMPEQ_INTRINSIC}}(l_chunk, l_quote_vec);
        
        // Find backslashes: all bytes == '\\'
        {{SIMD_TYPE}} l_backslash_mask = {{CMPEQ_INTRINSIC}}(l_chunk, l_backslash_vec);
        
        // Combine: any quote or backslash
        {{SIMD_TYPE}} l_combined = {{OR_INTRINSIC}}(l_quote_mask, l_backslash_mask);
        
        // Convert to bitmask
        {{#if USE_AVX512_MASK}}
        // AVX-512: use native kmask
        uint64_t l_mask = _mm512_mask_test_epi8_mask(_mm512_set1_epi8(0xFF), l_combined, _mm512_set1_epi8(0xFF));
        {{else}}
        // SSE2/AVX2: use movemask
        uint32_t l_mask = (uint32_t){{MOVEMASK_INTRINSIC}}(l_combined);
        {{/if}}
        
        if (l_mask != 0) {
            // Found quote or backslash
            {{#if USE_AVX512_MASK}}
            uint32_t l_first_match = __builtin_ctzll(l_mask);
            {{else}}
            uint32_t l_first_match = __builtin_ctz(l_mask);
            {{/if}}
            l_pos += l_first_match;
            
            if (a_input[l_pos] == '"') {
                // Found closing quote - done!
                const uint32_t l_string_len = l_pos - l_string_start;
                
                a_out_string->data = (const char*)(a_input + l_string_start);
                a_out_string->length = l_string_len;
                a_out_string->needs_unescape = l_has_escape;
                a_out_string->unescaped_valid = false;
                a_out_string->unescaped = NULL;
                a_out_string->unescaped_length = 0;
                a_out_string->reserved = 0;
                
                *a_out_end_offset = l_pos + 1; // after closing quote
                return true;
            } else if (a_input[l_pos] == '\\') {
                // Found escape sequence
                l_has_escape = true;
                l_pos++; // skip backslash
                if (l_pos >= a_input_len) {
                    log_it(L_ERROR, "Unterminated escape sequence");
                    return false;
                }
                l_pos++; // skip escaped character
            }
        } else {
            // No quote or backslash in this chunk
            l_pos += l_chunk_size;
        }
    }
    
    // Handle remaining bytes (< {{CHUNK_SIZE}}) character-by-character
    while (l_pos < a_input_len) {
        uint8_t l_byte = a_input[l_pos];
        
        if (l_byte == '"') {
            // Found closing quote
            const uint32_t l_string_len = l_pos - l_string_start;
            
            a_out_string->data = (const char*)(a_input + l_string_start);
            a_out_string->length = l_string_len;
            a_out_string->needs_unescape = l_has_escape;
            a_out_string->unescaped_valid = false;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            a_out_string->reserved = 0;
            
            *a_out_end_offset = l_pos + 1;
            return true;
        }
        
        if (l_byte == '\\') {
            l_has_escape = true;
            l_pos++; // skip backslash
            if (l_pos >= a_input_len) {
                log_it(L_ERROR, "Unterminated escape sequence");
                return false;
            }
            // Skip escaped character
        }
        
        l_pos++;
    }
    
    log_it(L_ERROR, "Unterminated string (missing closing quote)");
    return false;
}

#endif /* __{{ARCH_UPPER}}__ */
{{/if}}

{{#if USE_ARM}}
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>

#define LOG_TAG "dap_json_string_{{ARCH_LOWER}}"

/* ARM NEON movemask helper */
static inline uint16_t dap_neon_movemask_u8(uint8x16_t a_input) {
    // Extract high bit from each byte
    uint8x16_t l_shifted = vshrq_n_u8(a_input, 7);
    
    // Multiply by powers of 2 for bit positions
    const uint8_t l_mult_lut[16] = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t l_multiplier = vld1q_u8(l_mult_lut);
    uint8x16_t l_weighted = vmulq_u8(l_shifted, l_multiplier);
    
    // Horizontal sum using pairwise additions
    uint8x8_t l_low = vget_low_u8(l_weighted);
    uint8x8_t l_high = vget_high_u8(l_weighted);
    
    uint8x8_t l_sum1 = vpadd_u8(l_low, l_high);
    uint8x8_t l_sum2 = vpadd_u8(l_sum1, l_sum1);
    uint8x8_t l_sum3 = vpadd_u8(l_sum2, l_sum2);
    uint8x8_t l_sum4 = vpadd_u8(l_sum3, l_sum3);
    
    return (uint16_t)vget_lane_u8(l_sum4, 0) | ((uint16_t)vget_lane_u8(l_sum4, 1) << 8);
}

/**
 * @brief ARM NEON SIMD string scanner
 * @details Processes 16 bytes at once using NEON instructions.
 */
bool dap_json_string_scan_{{ARCH_LOWER}}(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
)
{
    if (!a_input || !a_out_string || !a_out_end_offset) {
        return false;
    }
    
    if (a_input_len == 0 || a_input[0] != '"') {
        log_it(L_ERROR, "Expected opening quote");
        return false;
    }
    
    const uint32_t l_string_start = 1;
    uint32_t l_pos = l_string_start;
    bool l_has_escape = false;
    
    const size_t l_chunk_size = 16;
    
    // Prepare comparison vectors
    const uint8x16_t l_quote_vec = vdupq_n_u8('"');
    const uint8x16_t l_backslash_vec = vdupq_n_u8('\\');
    
    // Process 16-byte chunks
    while (l_pos + l_chunk_size <= a_input_len) {
        uint8x16_t l_chunk = vld1q_u8(a_input + l_pos);
        
        uint8x16_t l_quote_mask = vceqq_u8(l_chunk, l_quote_vec);
        uint8x16_t l_backslash_mask = vceqq_u8(l_chunk, l_backslash_vec);
        
        uint8x16_t l_combined = vorrq_u8(l_quote_mask, l_backslash_mask);
        
        uint16_t l_mask = dap_neon_movemask_u8(l_combined);
        
        if (l_mask != 0) {
            uint32_t l_first_match = __builtin_ctz(l_mask);
            l_pos += l_first_match;
            
            if (a_input[l_pos] == '"') {
                const uint32_t l_string_len = l_pos - l_string_start;
                
                a_out_string->data = (const char*)(a_input + l_string_start);
                a_out_string->length = l_string_len;
                a_out_string->needs_unescape = l_has_escape;
                a_out_string->unescaped_valid = false;
                a_out_string->unescaped = NULL;
                a_out_string->unescaped_length = 0;
                a_out_string->reserved = 0;
                
                *a_out_end_offset = l_pos + 1;
                return true;
            } else if (a_input[l_pos] == '\\') {
                l_has_escape = true;
                l_pos++;
                if (l_pos >= a_input_len) {
                    log_it(L_ERROR, "Unterminated escape sequence");
                    return false;
                }
                l_pos++;
            }
        } else {
            l_pos += l_chunk_size;
        }
    }
    
    // Handle remaining bytes
    while (l_pos < a_input_len) {
        uint8_t l_byte = a_input[l_pos];
        
        if (l_byte == '"') {
            const uint32_t l_string_len = l_pos - l_string_start;
            
            a_out_string->data = (const char*)(a_input + l_string_start);
            a_out_string->length = l_string_len;
            a_out_string->needs_unescape = l_has_escape;
            a_out_string->unescaped_valid = false;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            a_out_string->reserved = 0;
            
            *a_out_end_offset = l_pos + 1;
            return true;
        }
        
        if (l_byte == '\\') {
            l_has_escape = true;
            l_pos++;
            if (l_pos >= a_input_len) {
                log_it(L_ERROR, "Unterminated escape sequence");
                return false;
            }
        }
        
        l_pos++;
    }
    
    log_it(L_ERROR, "Unterminated string (missing closing quote)");
    return false;
}

#endif /* __ARM_NEON || __aarch64__ */
{{/if}}
