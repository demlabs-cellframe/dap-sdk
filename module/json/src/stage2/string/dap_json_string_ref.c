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
 * @file dap_json_string_ref.c
 * @brief Zero-Copy JSON String Scanner - Reference Implementation
 * 
 * Pure C reference implementation - NO SIMD, character-by-character scanning.
 * Serves as correctness baseline and fallback for platforms without SIMD.
 * 
 * Performance: ~1 char per cycle (baseline)
 * SIMD implementations will be 16-64x faster
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2026-01-12
 */

#include "dap_common.h"
#include "internal/dap_json_string.h"
#include "dap_arena.h"

#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_json_string_ref"

/* ========================================================================== */
/*                    ESCAPE SEQUENCE HANDLING                                */
/* ========================================================================== */

/**
 * @brief Unescape JSON string
 * @details Handles: \", \\, \/, \b, \f, \n, \r, \t, \uXXXX
 */
static bool s_unescape_string(
    const char *a_input,
    size_t a_input_len,
    struct dap_arena *a_arena,
    char **a_out_unescaped,
    size_t *a_out_length
)
{
    if (!a_input || !a_arena || !a_out_unescaped || !a_out_length) {
        return false;
    }
    
    // Allocate buffer (worst case: same size as input)
    char *l_output = (char*)dap_arena_alloc(a_arena, a_input_len + 1);
    if (!l_output) {
        log_it(L_ERROR, "Failed to allocate %zu bytes for unescaping", a_input_len);
        return false;
    }
    
    size_t l_output_pos = 0;
    size_t l_input_pos = 0;
    
    while (l_input_pos < a_input_len) {
        if (a_input[l_input_pos] == '\\' && l_input_pos + 1 < a_input_len) {
            // Escape sequence
            l_input_pos++; // skip backslash
            char l_escaped = a_input[l_input_pos++];
            
            switch (l_escaped) {
                case '"':  l_output[l_output_pos++] = '"'; break;
                case '\\': l_output[l_output_pos++] = '\\'; break;
                case '/':  l_output[l_output_pos++] = '/'; break;
                case 'b':  l_output[l_output_pos++] = '\b'; break;
                case 'f':  l_output[l_output_pos++] = '\f'; break;
                case 'n':  l_output[l_output_pos++] = '\n'; break;
                case 'r':  l_output[l_output_pos++] = '\r'; break;
                case 't':  l_output[l_output_pos++] = '\t'; break;
                
                case 'u': {
                    // Unicode escape: \uXXXX
                    if (l_input_pos + 4 > a_input_len) {
                        log_it(L_ERROR, "Invalid unicode escape at position %zu", l_input_pos);
                        return false;
                    }
                    
                    // Parse hex digits
                    uint16_t l_codepoint = 0;
                    for (int i = 0; i < 4; i++) {
                        char l_hex = a_input[l_input_pos++];
                        l_codepoint <<= 4;
                        
                        if (l_hex >= '0' && l_hex <= '9') {
                            l_codepoint |= (l_hex - '0');
                        } else if (l_hex >= 'a' && l_hex <= 'f') {
                            l_codepoint |= (l_hex - 'a' + 10);
                        } else if (l_hex >= 'A' && l_hex <= 'F') {
                            l_codepoint |= (l_hex - 'A' + 10);
                        } else {
                            log_it(L_ERROR, "Invalid hex digit '%c' in unicode escape", l_hex);
                            return false;
                        }
                    }
                    
                    // Encode as UTF-8
                    if (l_codepoint <= 0x7F) {
                        // 1-byte UTF-8
                        l_output[l_output_pos++] = (char)l_codepoint;
                    } else if (l_codepoint <= 0x7FF) {
                        // 2-byte UTF-8
                        l_output[l_output_pos++] = (char)(0xC0 | (l_codepoint >> 6));
                        l_output[l_output_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    } else {
                        // 3-byte UTF-8
                        l_output[l_output_pos++] = (char)(0xE0 | (l_codepoint >> 12));
                        l_output[l_output_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_output_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    break;
                }
                
                default:
                    log_it(L_ERROR, "Invalid escape sequence '\\%c'", l_escaped);
                    return false;
            }
        } else {
            // Regular character
            l_output[l_output_pos++] = a_input[l_input_pos++];
        }
    }
    
    l_output[l_output_pos] = '\0';
    *a_out_unescaped = l_output;
    *a_out_length = l_output_pos;
    return true;
}

/* ========================================================================== */
/*                    REFERENCE STRING SCANNER (NO SIMD)                      */
/* ========================================================================== */

/**
 * @brief Reference C string scanner - character-by-character
 * @details NO SIMD - pure C implementation for correctness baseline
 */
bool dap_json_string_scan_ref(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_zc_t *a_out_string,
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
    
    // Scan for closing quote or backslash
    uint32_t l_pos = 1; // skip opening quote
    const uint32_t l_string_start = l_pos;
    bool l_has_escape = false;
    
    while (l_pos < a_input_len) {
        uint8_t l_byte = a_input[l_pos];
        
        if (l_byte == '"') {
            // Found closing quote
            const uint32_t l_string_len = l_pos - l_string_start;
            
            // Initialize zero-copy string
            a_out_string->data = (const char*)(a_input + l_string_start);
            a_out_string->length = l_string_len;
            a_out_string->needs_unescape = l_has_escape;
            a_out_string->unescaped_valid = false;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            a_out_string->reserved = 0;
            
            *a_out_end_offset = l_pos + 1; // after closing quote
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

/* ========================================================================== */
/*                    LAZY UNESCAPING                                         */
/* ========================================================================== */

const char* dap_json_string_get_cstr(
    dap_json_string_zc_t *a_string,
    struct dap_arena *a_arena
)
{
    if (!a_string) {
        return NULL;
    }
    
    // If no escapes - return direct pointer (zero-copy)
    if (!a_string->needs_unescape) {
        return a_string->data;
    }
    
    // If already unescaped - return cached result
    if (a_string->unescaped_valid) {
        return a_string->unescaped;
    }
    
    // Lazy unescaping - do it now
    if (!a_arena) {
        log_it(L_ERROR, "Arena required for unescaping");
        return NULL;
    }
    
    char *l_unescaped = NULL;
    size_t l_unescaped_len = 0;
    
    if (!s_unescape_string(
        a_string->data,
        a_string->length,
        a_arena,
        &l_unescaped,
        &l_unescaped_len
    )) {
        return NULL;
    }
    
    a_string->unescaped = l_unescaped;
    a_string->unescaped_length = (uint32_t)l_unescaped_len;
    a_string->unescaped_valid = true;
    
    return a_string->unescaped;
}

void dap_json_string_free(dap_json_string_zc_t *a_string)
{
    if (!a_string) {
        return;
    }
    
    // NOTE: 'data' pointer is NOT freed (zero-copy into JSON buffer)
    // Only 'unescaped' is freed (but it's in arena, so no explicit free needed)
    
    a_string->unescaped = NULL;
    a_string->unescaped_valid = false;
    a_string->unescaped_length = 0;
}
