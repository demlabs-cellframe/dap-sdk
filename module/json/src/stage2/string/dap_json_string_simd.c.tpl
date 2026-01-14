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
 * Chunk size: {{CHUNK_SIZE}} bytes
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
 * @date 2026-01-13
 */

#include "dap_common.h"
#include "internal/dap_json_string.h"

// Include architecture-specific SIMD macros
#include "dap_json_string_simd_{{ARCH_LOWER}}.h"

#define LOG_TAG "dap_json_string_{{ARCH_LOWER}}"

/**
 * @brief {{ARCH_UPPER}}-optimized JSON string scanner
 * @details Processes {{CHUNK_SIZE}} bytes at once using SIMD instructions
 * 
 * @param a_input Pointer to string data (after opening quote)
 * @param a_input_len Total length of JSON buffer
 * @param a_out_string Output zero-copy string metadata
 * @param a_out_end_offset Output: offset of closing quote
 * @return true on success, false on error
 */
bool dap_json_string_scan_{{ARCH_LOWER}}(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_zc_t *a_out_string,
    uint32_t *a_out_end_offset
) {
    if (!a_input || !a_out_string || !a_out_end_offset) {
        log_it(L_ERROR, "Invalid input (NULL pointer)");
        return false;
    }
    
    uint32_t l_pos = 0;
    bool l_has_escapes = false;
    
    // Create SIMD vectors for quote and backslash
    SIMD_VEC_TYPE l_quote_vec = SIMD_SET1('"');
    SIMD_VEC_TYPE l_backslash_vec = SIMD_SET1('\\');
    
    // SIMD main loop - architecture-specific implementation
    // Include the appropriate loop implementation based on architecture
#include "{{SIMD_LOOP_IMPL}}"
    
    // SCALAR TAIL - process remaining bytes
    while (l_pos < a_input_len) {
        uint8_t l_char = a_input[l_pos];
        
        if (l_char == '"') {
            // Found closing quote
            a_out_string->data = (const char*)a_input;
            a_out_string->length = l_pos;
            a_out_string->needs_unescape = l_has_escapes;
            a_out_string->unescaped_valid = 0;
            a_out_string->unescaped = NULL;
            a_out_string->unescaped_length = 0;
            *a_out_end_offset = l_pos;
            return true;
        } else if (l_char == '\\') {
            // Escape sequence
            l_has_escapes = true;
            l_pos++;
            if (l_pos >= a_input_len) {
                log_it(L_ERROR, "Unexpected end after escape at offset %u", l_pos - 1);
                return false;
            }
        }
        
        l_pos++;
    }
    
    // No closing quote found
    log_it(L_ERROR, "Unterminated string");
    return false;
}
