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

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "internal/dap_json_stage1.h"


#define LOG_TAG "dap_json_stage1_ref"

// Initial capacity for structural indices array
#define INITIAL_INDICES_CAPACITY    256

// Growth factor for indices array (2x)
#define INDICES_GROWTH_FACTOR       2

/* ========================================================================== */
/*                         MEMORY MANAGEMENT                                  */
/* ========================================================================== */

/**
 * @brief Grow structural indices array
 * @details Doubles the capacity of indices array using realloc
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return true on success, false on allocation failure
 */
static bool s_grow_indices_array(dap_json_stage1_t *a_stage1)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return false;
    }
    
    size_t l_new_capacity = a_stage1->indices_capacity * INDICES_GROWTH_FACTOR;
    dap_json_struct_index_t *l_new_indices = (dap_json_struct_index_t *)DAP_REALLOC(
        a_stage1->indices,
        l_new_capacity * sizeof(dap_json_struct_index_t)
    );
    
    if(!l_new_indices) {
        log_it(L_ERROR, "Failed to grow indices array to %zu entries", l_new_capacity);
        return false;
    }
    
    a_stage1->indices = l_new_indices;
    a_stage1->indices_capacity = l_new_capacity;
    
    log_it(L_DEBUG, "Grew indices array to %zu entries", l_new_capacity);
    return true;
}

/**
 * @brief Add structural index
 * @details Adds a structural character to indices array. Grows array if necessary.
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_position Position of structural character
 * @param[in] a_character Structural character
 * @return true on success, false on allocation failure
 */
static bool s_add_structural_index(
    dap_json_stage1_t *a_stage1,
    uint32_t a_position,
    uint8_t a_character
)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return false;
    }
    
    // Grow array if needed
    if(a_stage1->indices_count >= a_stage1->indices_capacity) {
        if(!s_grow_indices_array(a_stage1)) {
            a_stage1->error_code = STAGE1_ERROR_OUT_OF_MEMORY;
            a_stage1->error_position = a_position;
            snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                     "Out of memory growing indices array");
            return false;
        }
    }
    
    // Add index
    a_stage1->indices[a_stage1->indices_count].position = a_position;
    a_stage1->indices[a_stage1->indices_count].character = a_character;
    a_stage1->indices_count++;
    a_stage1->structural_chars++;
    
    return true;
}

/* ========================================================================== */
/*                          UTF-8 VALIDATION                                  */
/* ========================================================================== */

/**
 * @brief Validate UTF-8 continuation byte
 * @param[in] a_byte Byte to validate
 * @return true if valid continuation byte (10xxxxxx), false otherwise
 */
static inline bool s_is_utf8_continuation(uint8_t a_byte)
{
    return (a_byte & 0xC0) == 0x80;
}

/**
 * @brief Check for overlong UTF-8 encoding
 * @details Overlong encodings are security-critical: they can bypass validation
 * @param[in] a_first_byte First byte of sequence
 * @param[in] a_second_byte Second byte of sequence
 * @param[in] a_seq_len Sequence length
 * @return true if overlong, false if valid
 */
static bool s_is_overlong_encoding(uint8_t a_first_byte, uint8_t a_second_byte, int a_seq_len)
{
    switch(a_seq_len) {
        case 2:
            // 2-byte overlong if < 0x80 (should be 1-byte)
            return (a_first_byte & 0x1E) == 0;
        
        case 3:
            // 3-byte overlong if < 0x800 (should be 2-byte)
            return (a_first_byte == 0xE0) && ((a_second_byte & 0x20) == 0);
        
        case 4:
            // 4-byte overlong if < 0x10000 (should be 3-byte)
            return (a_first_byte == 0xF0) && ((a_second_byte & 0x30) == 0);
        
        default:
            return false;
    }
}

/**
 * @brief Check for UTF-16 surrogate
 * @details UTF-16 surrogates (U+D800..U+DFFF) are invalid in UTF-8.
 *          This is a common attack vector (surrogate smuggling).
 * @param[in] a_first_byte First byte of 3-byte sequence
 * @param[in] a_second_byte Second byte of 3-byte sequence
 * @return true if surrogate, false otherwise
 */
static bool s_is_utf16_surrogate(uint8_t a_first_byte, uint8_t a_second_byte)
{
    // U+D800..U+DFFF = 0xED 0xA0..0xBF ...
    return (a_first_byte == 0xED) && ((a_second_byte & 0xE0) == 0xA0);
}

/**
 * @brief Validate UTF-8 sequence (reference implementation)
 * @details Sequential (non-SIMD) UTF-8 validation.
 *          Reference implementation for correctness checking.
 *          Checks for:
 *          - Invalid start bytes
 *          - Invalid continuation bytes
 *          - Overlong encodings
 *          - UTF-16 surrogates
 *          - Code points > U+10FFFF
 * @param[in] a_input Input buffer
 * @param[in] a_len Buffer length
 * @param[out] a_out_error_pos Output: position of error (if any), can be NULL
 * @return true if valid UTF-8, false otherwise
 */
bool dap_json_validate_utf8_ref(
    const uint8_t *a_input,
    size_t a_len,
    size_t *a_out_error_pos
)
{
    if(!a_input) {
        log_it(L_ERROR, "NULL input pointer");
        if(a_out_error_pos)
            *a_out_error_pos = 0;
        return false;
    }
    
    if(a_len == 0) {
        // Empty input is valid UTF-8
        return true;
    }
    
    for(size_t i = 0; i < a_len; ) {
        uint8_t l_first = a_input[i];
        int l_seq_len = dap_json_utf8_sequence_length(l_first);
        
        // Invalid start byte
        if(l_seq_len == 0) {
            log_it(L_WARNING, "Invalid UTF-8 start byte 0x%02X at position %zu", l_first, i);
            if(a_out_error_pos)
                *a_out_error_pos = i;
            return false;
        }
        
        // Check if we have enough bytes
        if(i + (size_t)l_seq_len > a_len) {
            log_it(L_WARNING, "Incomplete UTF-8 sequence at position %zu", i);
            if(a_out_error_pos)
                *a_out_error_pos = i;
            return false;
        }
        
        // Single-byte (ASCII) - always valid
        if(l_seq_len == 1) {
            i++;
            continue;
        }
        
        // Multi-byte: validate continuation bytes
        for(int j = 1; j < l_seq_len; j++) {
            if(!s_is_utf8_continuation(a_input[i + j])) {
                log_it(L_WARNING, "Invalid UTF-8 continuation byte at position %zu", i + j);
                if(a_out_error_pos)
                    *a_out_error_pos = i + j;
                return false;
            }
        }
        
        // Check for overlong encoding (security-critical)
        if(s_is_overlong_encoding(a_input[i], a_input[i + 1], l_seq_len)) {
            log_it(L_WARNING, "Overlong UTF-8 encoding at position %zu", i);
            if(a_out_error_pos)
                *a_out_error_pos = i;
            return false;
        }
        
        // Check for UTF-16 surrogates (3-byte only, security-critical)
        if(l_seq_len == 3 && s_is_utf16_surrogate(a_input[i], a_input[i + 1])) {
            log_it(L_WARNING, "UTF-16 surrogate in UTF-8 at position %zu", i);
            if(a_out_error_pos)
                *a_out_error_pos = i;
            return false;
        }
        
        // Check for code points > U+10FFFF (4-byte only)
        if(l_seq_len == 4) {
            uint32_t l_codepoint = 
                ((a_input[i] & 0x07) << 18) |
                ((a_input[i + 1] & 0x3F) << 12) |
                ((a_input[i + 2] & 0x3F) << 6) |
                (a_input[i + 3] & 0x3F);
            
            if(l_codepoint > 0x10FFFF) {
                log_it(L_WARNING, "Code point 0x%X exceeds U+10FFFF at position %zu", l_codepoint, i);
                if(a_out_error_pos)
                    *a_out_error_pos = i;
                return false;
            }
        }
        
        i += (size_t)l_seq_len;
    }
    
    return true;
}

/* ========================================================================== */
/*                       STRING SCANNING                                      */
/* ========================================================================== */

/**
 * @brief Skip string content
 * @details Scans from current position (after opening ") until closing unescaped ".
 *          Handles escape sequences correctly.
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return true on success, false on unterminated string or invalid escape
 */
static bool s_skip_string(dap_json_stage1_t *a_stage1)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return false;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    size_t i = a_stage1->current_pos;
    
    // We're called right after opening quote, skip it
    i++;
    
    while(i < l_len) {
        uint8_t c = l_input[i];
        
        // Unescaped quote - end of string
        if(c == '"') {
            a_stage1->current_pos = i + 1;
            return true;
        }
        
        // Escape sequence
        if(c == '\\') {
            i++; // Skip backslash
            if(i >= l_len) {
                a_stage1->error_code = STAGE1_ERROR_UNTERMINATED_STRING;
                a_stage1->error_position = i - 1;
                snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                         "Unterminated string (escape at end)");
                return false;
            }
            
            uint8_t l_escaped = l_input[i];
            
            // Valid escape sequences: ", \, /, b, f, n, r, t, u
            if(l_escaped != '"' && l_escaped != '\\' && l_escaped != '/' &&
               l_escaped != 'b' && l_escaped != 'f' && l_escaped != 'n' &&
               l_escaped != 'r' && l_escaped != 't' && l_escaped != 'u') {
                a_stage1->error_code = STAGE1_ERROR_INVALID_ESCAPE;
                a_stage1->error_position = i - 1;
                snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                         "Invalid escape sequence: \\%c", l_escaped);
                return false;
            }
            
            // Handle \uXXXX
            if(l_escaped == 'u') {
                // Need 4 hex digits
                if(i + 4 >= l_len) {
                    a_stage1->error_code = STAGE1_ERROR_INVALID_ESCAPE;
                    a_stage1->error_position = i - 1;
                    snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
                             "Incomplete \\uXXXX sequence");
                    return false;
                }
                
                // Skip hex digits (validation can be done in Stage 2)
                i += 4;
            }
            
            a_stage1->string_chars++;
            i++;
            continue;
        }
        
        // Regular character in string
        a_stage1->string_chars++;
        i++;
    }
    
    // Reached end without closing quote
    a_stage1->error_code = STAGE1_ERROR_UNTERMINATED_STRING;
    a_stage1->error_position = a_stage1->current_pos;
    snprintf(a_stage1->error_message, sizeof(a_stage1->error_message),
             "Unterminated string");
    return false;
}

/* ========================================================================== */
/*                       STRUCTURAL INDEXING                                  */
/* ========================================================================== */

/**
 * @brief Run Stage 1 structural indexing
 * @details Scans input buffer and extracts all structural characters.
 *          After successful execution, indices array is filled.
 * @param[in,out] a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
int dap_json_stage1_run(dap_json_stage1_t *a_stage1)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_len = a_stage1->input_len;
    
    // Reset state
    a_stage1->indices_count = 0;
    a_stage1->current_pos = 0;
    a_stage1->in_string = false;
    a_stage1->escape_next = false;
    a_stage1->string_chars = 0;
    a_stage1->whitespace_chars = 0;
    a_stage1->structural_chars = 0;
    a_stage1->error_code = STAGE1_SUCCESS;
    a_stage1->error_position = 0;
    a_stage1->error_message[0] = '\0';
    
    log_it(L_DEBUG, "Starting Stage 1 structural indexing (%zu bytes)", l_len);
    
    // Main scanning loop
    while(a_stage1->current_pos < l_len) {
        uint8_t c = l_input[a_stage1->current_pos];
        dap_json_char_class_t l_char_class = dap_json_classify_char(c);
        
        switch(l_char_class) {
            case CHAR_CLASS_WHITESPACE:
                a_stage1->whitespace_chars++;
                a_stage1->current_pos++;
                break;
            
            case CHAR_CLASS_QUOTE:
                // Start of string - skip entire string
                if(!s_skip_string(a_stage1)) {
                    log_it(L_ERROR, "String parsing failed at position %zu: %s",
                           a_stage1->error_position, a_stage1->error_message);
                    return a_stage1->error_code;
                }
                break;
            
            case CHAR_CLASS_STRUCTURAL:
                // Add structural index
                if(!s_add_structural_index(a_stage1, (uint32_t)a_stage1->current_pos, c)) {
                    log_it(L_ERROR, "Failed to add structural index at position %zu", a_stage1->current_pos);
                    return a_stage1->error_code;
                }
                a_stage1->current_pos++;
                break;
            
            default:
                // All other characters (numbers, true/false/null literals, etc.)
                a_stage1->current_pos++;
                break;
        }
    }
    
    log_it(L_INFO, "Stage 1 complete: %zu structural indices, %zu string chars, %zu whitespace chars",
           a_stage1->structural_chars, a_stage1->string_chars, a_stage1->whitespace_chars);
    
    return STAGE1_SUCCESS;
}

/* ========================================================================== */
/*                           PUBLIC API                                       */
/* ========================================================================== */

/**
 * @brief Initialize Stage 1 parser
 * @details Allocates and initializes Stage 1 parser state.
 *          Must be freed with dap_json_stage1_free().
 * @param[in] a_input JSON input buffer (must remain valid during parsing)
 * @param[in] a_input_len Input buffer length in bytes
 * @return Initialized Stage 1 parser, or NULL on error
 */
dap_json_stage1_t *dap_json_stage1_init(const uint8_t *a_input, size_t a_input_len)
{
    if(!a_input) {
        log_it(L_ERROR, "NULL input pointer");
        return NULL;
    }
    
    if(a_input_len == 0) {
        log_it(L_ERROR, "Zero length input");
        return NULL;
    }
    
    // Allocate parser state
    dap_json_stage1_t *l_stage1 = DAP_NEW_Z(dap_json_stage1_t);
    if(!l_stage1) {
        log_it(L_ERROR, "Failed to allocate Stage 1 parser");
        return NULL;
    }
    
    // Initialize input
    l_stage1->input = a_input;
    l_stage1->input_len = a_input_len;
    
    // Allocate initial indices array
    l_stage1->indices_capacity = INITIAL_INDICES_CAPACITY;
    l_stage1->indices = DAP_NEW_Z_SIZE(dap_json_struct_index_t,
                                       l_stage1->indices_capacity * sizeof(dap_json_struct_index_t));
    
    if(!l_stage1->indices) {
        log_it(L_ERROR, "Failed to allocate indices array");
        DAP_DELETE(l_stage1);
        return NULL;
    }
    
    l_stage1->indices_count = 0;
    l_stage1->current_pos = 0;
    l_stage1->in_string = false;
    l_stage1->escape_next = false;
    l_stage1->string_chars = 0;
    l_stage1->whitespace_chars = 0;
    l_stage1->structural_chars = 0;
    l_stage1->error_code = STAGE1_SUCCESS;
    l_stage1->error_position = 0;
    l_stage1->error_message[0] = '\0';
    
    log_it(L_DEBUG, "Stage 1 parser initialized (%zu bytes, %zu initial indices capacity)",
           a_input_len, l_stage1->indices_capacity);
    
    return l_stage1;
}

/**
 * @brief Free Stage 1 parser
 * @param[in] a_stage1 Stage 1 parser to free (can be NULL)
 */
void dap_json_stage1_free(dap_json_stage1_t *a_stage1)
{
    if(!a_stage1) {
        return;
    }
    
    DAP_DELETE(a_stage1->indices);
    DAP_DELETE(a_stage1);
    
    log_it(L_DEBUG, "Stage 1 parser freed");
}

/**
 * @brief Get structural indices array
 * @details Returns pointer to structural indices array.
 *          Array is valid until dap_json_stage1_free() or next dap_json_stage1_run().
 * @param[in] a_stage1 Stage 1 parser
 * @param[out] a_out_count Output: number of indices (can be NULL)
 * @return Pointer to indices array, or NULL if not run yet
 */
const dap_json_struct_index_t *dap_json_stage1_get_indices(
    const dap_json_stage1_t *a_stage1,
    size_t *a_out_count
)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        if(a_out_count)
            *a_out_count = 0;
        return NULL;
    }
    
    if(a_out_count) {
        *a_out_count = a_stage1->indices_count;
    }
    
    return a_stage1->indices;
}

/**
 * @brief Get Stage 1 statistics
 * @details Returns parsing statistics for profiling.
 * @param[in] a_stage1 Stage 1 parser
 * @param[out] a_out_string_chars Output: chars in strings (can be NULL)
 * @param[out] a_out_whitespace_chars Output: whitespace chars (can be NULL)
 * @param[out] a_out_structural_chars Output: structural chars (can be NULL)
 */
void dap_json_stage1_get_stats(
    const dap_json_stage1_t *a_stage1,
    size_t *a_out_string_chars,
    size_t *a_out_whitespace_chars,
    size_t *a_out_structural_chars
)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return;
    }
    
    if(a_out_string_chars) {
        *a_out_string_chars = a_stage1->string_chars;
    }
    
    if(a_out_whitespace_chars) {
        *a_out_whitespace_chars = a_stage1->whitespace_chars;
    }
    
    if(a_out_structural_chars) {
        *a_out_structural_chars = a_stage1->structural_chars;
    }
}
