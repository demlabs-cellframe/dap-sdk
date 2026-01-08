/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP JSON Native Implementation Team
 * Copyright  (c) 2017-2025
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
 * @file dap_json_stage2_ref.c
 * @brief Stage 2: DOM Building - Reference Implementation
 * 
 * Pure C reference implementation для построения DOM из structural indices.
 * Служит baseline для correctness testing.
 * 
 * Алгоритм:
 * 1. Sequential walk по structural indices
 * 2. Value parsing (strings, numbers, literals)
 * 3. DOM node creation
 * 4. Tree assembly
 * 
 * Performance target: 0.8-1.2 GB/s (reference C baseline)
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2025-01-07
 */

#include "dap_common.h"
#include "internal/dap_json_stage2.h"
#include "dap_arena.h"
#include "dap_string_pool.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

#define LOG_TAG "dap_json_stage2_ref"

// Initial capacity for arrays/objects
#define INITIAL_ARRAY_CAPACITY 8
#define INITIAL_OBJECT_CAPACITY 8

// Growth factors
#define ARRAY_GROWTH_FACTOR 2
#define OBJECT_GROWTH_FACTOR 2

// Maximum nesting depth (защита от stack overflow)
#define MAX_NESTING_DEPTH 1000

/* ========================================================================== */
/*                    INTERNAL ARENA-BASED HELPERS                            */
/* ========================================================================== */

/**
 * @brief Add element to array using Arena (copy-on-grow pattern)
 * @details Internal function for Stage 2 parsing only
 */
static bool s_array_add_arena(
    dap_arena_t *a_arena,
    dap_json_value_t *a_array,
    dap_json_value_t *a_element
)
{
    if(!a_array || a_array->type != DAP_JSON_TYPE_ARRAY) {
        log_it(L_ERROR, "Invalid array");
        return false;
    }
    
    if(!a_element) {
        log_it(L_ERROR, "NULL element");
        return false;
    }
    
    // Grow if needed
    if(a_array->array.count >= a_array->array.capacity) {
        size_t l_new_capacity = (a_array->array.capacity == 0) ? 
            INITIAL_ARRAY_CAPACITY : 
            (a_array->array.capacity * ARRAY_GROWTH_FACTOR);
        
        // Allocate new array in Arena
        dap_json_value_t **l_new_elements = (dap_json_value_t **)dap_arena_alloc(
            a_arena,
            l_new_capacity * sizeof(dap_json_value_t*)
        );
        
        if(!l_new_elements) {
            log_it(L_ERROR, "Arena allocation failed for array growth to %zu elements", l_new_capacity);
            return false;
        }
        
        // Copy old elements
        if(a_array->array.elements && a_array->array.count > 0) {
            memcpy(l_new_elements, a_array->array.elements,
                   a_array->array.count * sizeof(dap_json_value_t*));
        }
        
        // Old array becomes garbage in Arena (no free needed)
        a_array->array.elements = l_new_elements;
        a_array->array.capacity = l_new_capacity;
    }
    
    a_array->array.elements[a_array->array.count++] = a_element;
    return true;
}

/**
 * @brief Add key-value pair to object using Arena + String Pool
 * @details Internal function for Stage 2 parsing only
 */
static bool s_object_add_arena(
    dap_arena_t *a_arena,
    dap_string_pool_t *a_string_pool,
    dap_json_value_t *a_object,
    const char *a_key,
    dap_json_value_t *a_value
)
{
    if(!a_object || a_object->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Invalid object");
        return false;
    }
    
    if(!a_key || !a_value) {
        log_it(L_ERROR, "NULL key or value");
        return false;
    }
    
    // Intern key in String Pool for deduplication
    const char *l_interned_key = dap_string_pool_intern(a_string_pool, a_key);
    if (!l_interned_key) {
        log_it(L_ERROR, "Failed to intern object key");
        return false;
    }
    
    // Check for duplicate key (O(1) pointer comparison after interning)
    for(size_t i = 0; i < a_object->object.count; i++) {
        if(a_object->object.pairs[i].key == l_interned_key) {
            log_it(L_WARNING, "Duplicate object key: %s", a_key);
            // Overwrite existing value
            a_object->object.pairs[i].value = a_value;
            return true;
        }
    }
    
    // Grow if needed
    if(a_object->object.count >= a_object->object.capacity) {
        size_t l_new_capacity = (a_object->object.capacity == 0) ?
            INITIAL_OBJECT_CAPACITY :
            (a_object->object.capacity * OBJECT_GROWTH_FACTOR);
        
        // Allocate new pairs array in Arena
        dap_json_object_pair_t *l_new_pairs = (dap_json_object_pair_t *)dap_arena_alloc(
            a_arena,
            l_new_capacity * sizeof(dap_json_object_pair_t)
        );
        
        if(!l_new_pairs) {
            log_it(L_ERROR, "Arena allocation failed for object growth to %zu pairs", l_new_capacity);
            return false;
        }
        
        // Copy old pairs
        if(a_object->object.pairs && a_object->object.count > 0) {
            memcpy(l_new_pairs, a_object->object.pairs,
                   a_object->object.count * sizeof(dap_json_object_pair_t));
        }
        
        // Old array becomes garbage in Arena (no free needed)
        a_object->object.pairs = l_new_pairs;
        a_object->object.capacity = l_new_capacity;
    }
    
    // Add new pair (key already interned, no strdup needed)
    a_object->object.pairs[a_object->object.count].key = (char *)l_interned_key;
    a_object->object.pairs[a_object->object.count].value = a_value;
    a_object->object.count++;
    
    return true;
}

/* ========================================================================== */
/*                         VALUE CREATION                                     */
/* ========================================================================== */

/**
 * @brief Create value using Arena allocator (internal, high-performance)
 */
static inline dap_json_value_t *s_create_value_arena(dap_arena_t *a_arena)
{
    dap_json_value_t *l_value = (dap_json_value_t *)dap_arena_alloc_zero(
        a_arena, 
        sizeof(dap_json_value_t)
    );
    
    if (!l_value) {
        log_it(L_ERROR, "Arena allocation failed for value");
    }
    
    return l_value;
}

/**
 * @brief Create null value (public API - uses malloc)
 */
dap_json_value_t *dap_json_value_v2_create_null(void)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate null value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_NULL;
    return l_value;
}

/**
 * @brief Create boolean value
 */
dap_json_value_t *dap_json_value_v2_create_bool(bool a_value)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate bool value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_BOOLEAN;
    l_value->boolean = a_value;
    return l_value;
}

/**
 * @brief Create number value (integer)
 */
dap_json_value_t *dap_json_value_v2_create_int(int64_t a_value)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate int value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_INT;
    l_value->number.i = a_value;
    l_value->number.is_double = false;
    return l_value;
}

/**
 * @brief Create number value (double)
 */
dap_json_value_t *dap_json_value_v2_create_double(double a_value)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate double value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_DOUBLE;
    l_value->number.d = a_value;
    l_value->number.is_double = true;
    return l_value;
}

/**
 * @brief Create string value
 */
dap_json_value_t *dap_json_value_v2_create_string(const char *a_data, size_t a_length)
{
    if(!a_data) {
        log_it(L_ERROR, "NULL string data");
        return NULL;
    }
    
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate string value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_STRING;
    l_value->string.length = a_length;
    l_value->string.data = DAP_NEW_Z_SIZE(char, a_length + 1);
    
    if(!l_value->string.data) {
        log_it(L_ERROR, "Failed to allocate string data (%zu bytes)", a_length + 1);
        DAP_DELETE(l_value);
        return NULL;
    }
    
    memcpy(l_value->string.data, a_data, a_length);
    l_value->string.data[a_length] = '\0';
    l_value->string.needs_free = true;
    
    return l_value;
}

/**
 * @brief Create array value
 */
dap_json_value_t *dap_json_value_v2_create_array(void)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate array value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_ARRAY;
    l_value->array.capacity = INITIAL_ARRAY_CAPACITY;
    l_value->array.count = 0;
    l_value->array.elements = DAP_NEW_Z_SIZE(dap_json_value_t*, 
                                              INITIAL_ARRAY_CAPACITY * sizeof(dap_json_value_t*));
    
    if(!l_value->array.elements) {
        log_it(L_ERROR, "Failed to allocate array elements");
        DAP_DELETE(l_value);
        return NULL;
    }
    
    return l_value;
}

/**
 * @brief Create object value
 */
dap_json_value_t *dap_json_value_v2_create_object(void)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if(!l_value) {
        log_it(L_ERROR, "Failed to allocate object value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_OBJECT;
    l_value->object.capacity = INITIAL_OBJECT_CAPACITY;
    l_value->object.count = 0;
    l_value->object.pairs = DAP_NEW_Z_SIZE(dap_json_object_pair_t,
                                            INITIAL_OBJECT_CAPACITY * sizeof(dap_json_object_pair_t));
    
    if(!l_value->object.pairs) {
        log_it(L_ERROR, "Failed to allocate object pairs");
        DAP_DELETE(l_value);
        return NULL;
    }
    
    return l_value;
}

/**
 * @brief Free JSON value recursively
 * 
 * @note When using Arena allocator (Phase 1.5), this function is a NO-OP.
 *       All memory is owned by the Arena and will be freed when the Arena is freed.
 *       This function is kept for API compatibility but does nothing.
 */
void dap_json_value_v2_free(dap_json_value_t *a_value)
{
    // NO-OP: All memory is managed by Arena allocator
    // Memory will be freed when dap_arena_free() is called
    (void)a_value;
}

/* ========================================================================== */
/*                         ARRAY/OBJECT OPERATIONS                            */
/* ========================================================================== */

/**
 * @brief Add element to array
 */
bool dap_json_array_v2_add(dap_json_value_t *a_array, dap_json_value_t *a_element)
{
    if(!a_array || a_array->type != DAP_JSON_TYPE_ARRAY) {
        log_it(L_ERROR, "Invalid array");
        return false;
    }
    
    if(!a_element) {
        log_it(L_ERROR, "NULL element");
        return false;
    }
    
    // Grow if needed
    if(a_array->array.count >= a_array->array.capacity) {
        size_t l_new_capacity = a_array->array.capacity * ARRAY_GROWTH_FACTOR;
        dap_json_value_t **l_new_elements = DAP_REALLOC(
            a_array->array.elements,
            l_new_capacity * sizeof(dap_json_value_t*)
        );
        
        if(!l_new_elements) {
            log_it(L_ERROR, "Failed to grow array to %zu elements", l_new_capacity);
            return false;
        }
        
        a_array->array.elements = l_new_elements;
        a_array->array.capacity = l_new_capacity;
    }
    
    a_array->array.elements[a_array->array.count++] = a_element;
    return true;
}

/**
 * @brief Add key-value pair to object
 */
bool dap_json_object_v2_add(dap_json_value_t *a_object, const char *a_key, dap_json_value_t *a_value)
{
    if(!a_object || a_object->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Invalid object");
        return false;
    }
    
    if(!a_key || !a_value) {
        log_it(L_ERROR, "NULL key or value");
        return false;
    }
    
    // Check for duplicate key
    for(size_t i = 0; i < a_object->object.count; i++) {
        if(strcmp(a_object->object.pairs[i].key, a_key) == 0) {
            log_it(L_WARNING, "Duplicate key: %s", a_key);
            return false;
        }
    }
    
    // Grow if needed
    if(a_object->object.count >= a_object->object.capacity) {
        size_t l_new_capacity = a_object->object.capacity * OBJECT_GROWTH_FACTOR;
        dap_json_object_pair_t *l_new_pairs = DAP_REALLOC(
            a_object->object.pairs,
            l_new_capacity * sizeof(dap_json_object_pair_t)
        );
        
        if(!l_new_pairs) {
            log_it(L_ERROR, "Failed to grow object to %zu pairs", l_new_capacity);
            return false;
        }
        
        a_object->object.pairs = l_new_pairs;
        a_object->object.capacity = l_new_capacity;
    }
    
    // Add pair
    size_t l_key_len = strlen(a_key);
    char *l_key_copy = DAP_NEW_Z_SIZE(char, l_key_len + 1);
    
    if(!l_key_copy) {
        log_it(L_ERROR, "Failed to allocate key copy");
        return false;
    }
    
    memcpy(l_key_copy, a_key, l_key_len + 1);
    
    a_object->object.pairs[a_object->object.count].key = l_key_copy;
    a_object->object.pairs[a_object->object.count].value = a_value;
    a_object->object.count++;
    
    return true;
}

/**
 * @brief Get array element by index
 */
dap_json_value_t *dap_json_array_v2_get(const dap_json_value_t *a_array, size_t a_index)
{
    if(!a_array || a_array->type != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    
    if(a_index >= a_array->array.count) {
        return NULL;
    }
    
    return a_array->array.elements[a_index];
}

/**
 * @brief Get object value by key
 */
dap_json_value_t *dap_json_object_v2_get(const dap_json_value_t *a_object, const char *a_key)
{
    if(!a_object || a_object->type != DAP_JSON_TYPE_OBJECT || !a_key) {
        return NULL;
    }
    
    for(size_t i = 0; i < a_object->object.count; i++) {
        if(strcmp(a_object->object.pairs[i].key, a_key) == 0) {
            return a_object->object.pairs[i].value;
        }
    }
    
    return NULL;
}

/* ========================================================================== */
/*                         VALUE PARSING HELPERS                              */
/* ========================================================================== */

/**
 * @brief Parse number from input
 * @details Поддерживает integers и floating point
 * 
 * @param[in] a_input Input buffer
 * @param[in] a_start Start offset
 * @param[in] a_end End offset (exclusive)
 * @param[out] a_out_value Output: parsed value
 * @return true on success, false on parse error
 */
/**
 * @brief Parse number value
 * @details Uses Arena for allocation, no malloc
 */
static bool s_parse_number(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    uint32_t a_end,
    dap_json_value_t **a_out_value
)
{
    const uint8_t *a_input = a_stage2->input;
    
    if(!a_input || !a_out_value || a_start >= a_end) {
        return false;
    }
    
    // Extract substring
    size_t l_len = a_end - a_start;
    char l_buffer[256];
    
    if(l_len >= sizeof(l_buffer)) {
        log_it(L_ERROR, "Number too long: %zu bytes", l_len);
        return false;
    }
    
    memcpy(l_buffer, a_input + a_start, l_len);
    l_buffer[l_len] = '\0';
    
    // Check for decimal point or exponent (indicates double)
    bool l_is_double = false;
    for(size_t i = 0; i < l_len; i++) {
        if(l_buffer[i] == '.' || l_buffer[i] == 'e' || l_buffer[i] == 'E') {
            l_is_double = true;
            break;
        }
    }
    
    // Create value using Arena
    dap_json_value_t *l_value = s_create_value_arena(a_stage2->arena);
    if (!l_value) {
        return false;
    }
    
    if(l_is_double) {
        // Parse as double
        char *l_endptr = NULL;
        errno = 0;
        double l_dval = strtod(l_buffer, &l_endptr);
        
        if(errno != 0 || l_endptr == l_buffer || *l_endptr != '\0') {
            log_it(L_ERROR, "Invalid double: %s", l_buffer);
            return false;
        }
        
        if(!isfinite(l_dval)) {
            log_it(L_ERROR, "Double out of range: %s", l_buffer);
            return false;
        }
        
        l_value->type = DAP_JSON_TYPE_DOUBLE;
        l_value->number.d = l_dval;
        l_value->number.is_double = true;
    }
    else {
        // Parse as int64
        char *l_endptr = NULL;
        errno = 0;
        long long l_ival = strtoll(l_buffer, &l_endptr, 10);
        
        if(errno != 0 || l_endptr == l_buffer || *l_endptr != '\0') {
            log_it(L_ERROR, "Invalid integer: %s", l_buffer);
            return false;
        }
        
        l_value->type = DAP_JSON_TYPE_INT;
        l_value->number.i = (int64_t)l_ival;
        l_value->number.is_double = false;
    }
    
    *a_out_value = l_value;
    return true;
}

/**
 * @brief Unescape JSON string
 * @details Обрабатывает escape sequences: \", \\, \/, \b, \f, \n, \r, \t, \uXXXX
 * 
 * @param[in] a_input Input string (without quotes)
 * @param[in] a_length Input length
 * @param[out] a_out_data Output: unescaped string (must be freed)
 * @param[out] a_out_length Output: unescaped length
 * @return true on success, false on invalid escape
 */
static bool s_unescape_string(
    const uint8_t *a_input,
    size_t a_length,
    char **a_out_data,
    size_t *a_out_length
)
{
    if(!a_input || !a_out_data || !a_out_length) {
        return false;
    }
    
    // Allocate worst-case size (no escapes)
    char *l_output = DAP_NEW_Z_SIZE(char, a_length + 1);
    if(!l_output) {
        log_it(L_ERROR, "Failed to allocate unescaped string");
        return false;
    }
    
    size_t l_out_pos = 0;
    
    for(size_t i = 0; i < a_length; i++) {
        if(a_input[i] == '\\' && i + 1 < a_length) {
            i++; // Skip backslash
            
            switch(a_input[i]) {
                case '"':  l_output[l_out_pos++] = '"';  break;
                case '\\': l_output[l_out_pos++] = '\\'; break;
                case '/':  l_output[l_out_pos++] = '/';  break;
                case 'b':  l_output[l_out_pos++] = '\b'; break;
                case 'f':  l_output[l_out_pos++] = '\f'; break;
                case 'n':  l_output[l_out_pos++] = '\n'; break;
                case 'r':  l_output[l_out_pos++] = '\r'; break;
                case 't':  l_output[l_out_pos++] = '\t'; break;
                
                case 'u': {
                    // Unicode escape: \uXXXX
                    if(i + 4 >= a_length) {
                        log_it(L_ERROR, "Incomplete unicode escape");
                        DAP_DELETE(l_output);
                        return false;
                    }
                    
                    // Parse hex digits
                    uint32_t l_codepoint = 0;
                    for(int j = 0; j < 4; j++) {
                        char l_c = a_input[i + 1 + j];
                        uint32_t l_digit;
                        
                        if(l_c >= '0' && l_c <= '9') {
                            l_digit = l_c - '0';
                        }
                        else if(l_c >= 'a' && l_c <= 'f') {
                            l_digit = 10 + (l_c - 'a');
                        }
                        else if(l_c >= 'A' && l_c <= 'F') {
                            l_digit = 10 + (l_c - 'A');
                        }
                        else {
                            log_it(L_ERROR, "Invalid hex digit in unicode escape: %c", l_c);
                            DAP_DELETE(l_output);
                            return false;
                        }
                        
                        l_codepoint = (l_codepoint << 4) | l_digit;
                    }
                    
                    i += 4; // Skip hex digits
                    
                    // Encode as UTF-8
                    if(l_codepoint <= 0x7F) {
                        // 1-byte UTF-8
                        l_output[l_out_pos++] = (char)l_codepoint;
                    }
                    else if(l_codepoint <= 0x7FF) {
                        // 2-byte UTF-8
                        l_output[l_out_pos++] = (char)(0xC0 | (l_codepoint >> 6));
                        l_output[l_out_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    else if(l_codepoint <= 0xFFFF) {
                        // 3-byte UTF-8
                        l_output[l_out_pos++] = (char)(0xE0 | (l_codepoint >> 12));
                        l_output[l_out_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_out_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    // NOTE: Full surrogate pair handling будет добавлен позже
                    
                    break;
                }
                
                default:
                    log_it(L_ERROR, "Invalid escape sequence: \\%c", a_input[i]);
                    DAP_DELETE(l_output);
                    return false;
            }
        }
        else {
            l_output[l_out_pos++] = a_input[i];
        }
    }
    
    l_output[l_out_pos] = '\0';
    *a_out_data = l_output;
    *a_out_length = l_out_pos;
    
    return true;
}

/**
 * @brief Parse string from input
 * @details Находит строку между кавычками и unescapes её
 * 
 * @param[in] a_input Input buffer
 * @param[in] a_start Start offset (должна быть opening quote)
 * @param[in] a_input_len Input buffer length
 * @param[out] a_out_value Output: parsed string value
 * @param[out] a_out_end_offset Output: offset after closing quote
 * @return true on success, false on parse error
 */
/**
 * @brief Parse string value
 * @details Uses Arena for allocation, no malloc
 */
static bool s_parse_string(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    dap_json_value_t **a_out_value,
    uint32_t *a_out_end_offset
)
{
    const uint8_t *a_input = a_stage2->input;
    size_t a_input_len = a_stage2->input_len;
    
    if(!a_input || !a_out_value || !a_out_end_offset) {
        return false;
    }
    
    if(a_start >= a_input_len || a_input[a_start] != '"') {
        log_it(L_ERROR, "Expected opening quote at offset %u", a_start);
        return false;
    }
    
    // Find closing quote
    uint32_t l_pos = a_start + 1;
    uint32_t l_string_start = l_pos;
    
    while(l_pos < a_input_len) {
        if(a_input[l_pos] == '"') {
            // Found closing quote
            uint32_t l_string_end = l_pos;
            
            // Unescape string - use Arena for temp buffer
            char *l_unescaped = NULL;
            size_t l_unescaped_len = 0;
            
            if(!s_unescape_string(
                a_input + l_string_start,
                l_string_end - l_string_start,
                &l_unescaped,
                &l_unescaped_len
            )) {
                return false;
            }
            
            // Create value using Arena
            dap_json_value_t *l_value = s_create_value_arena(a_stage2->arena);
            if (!l_value) {
                DAP_DELETE(l_unescaped);
                return false;
            }
            
            l_value->type = DAP_JSON_TYPE_STRING;
            // Copy string to Arena
            l_value->string.data = dap_arena_strndup(a_stage2->arena, l_unescaped, l_unescaped_len);
            l_value->string.length = l_unescaped_len;
            l_value->string.needs_free = false; // Arena owns it
            
            DAP_DELETE(l_unescaped); // Free temp buffer
            
            if (!l_value->string.data) {
                return false;
            }
            
            *a_out_value = l_value;
            *a_out_end_offset = l_pos + 1; // After closing quote
            return true;
        }
        else if(a_input[l_pos] == '\\') {
            // Skip escape sequence
            l_pos += 2;
        }
        else {
            l_pos++;
        }
    }
    
    log_it(L_ERROR, "Unclosed string starting at offset %u", a_start);
    return false;
}

/**
 * @brief Parse literal (true, false, null)
 * @details Uses Arena for allocation, no malloc
 */
static bool s_parse_literal(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    dap_json_value_t **a_out_value,
    uint32_t *a_out_end_offset
)
{
    const uint8_t *a_input = a_stage2->input;
    size_t a_input_len = a_stage2->input_len;
    
    if(!a_input || !a_out_value || !a_out_end_offset) {
        return false;
    }
    
    if(a_start >= a_input_len) {
        return false;
    }
    
    // Create value using Arena
    dap_json_value_t *l_value = s_create_value_arena(a_stage2->arena);
    if (!l_value) {
        return false;
    }
    
    // Check for "true"
    if(a_start + 4 <= a_input_len &&
       memcmp(a_input + a_start, "true", 4) == 0) {
        l_value->type = DAP_JSON_TYPE_BOOLEAN;
        l_value->boolean = true;
        *a_out_value = l_value;
        *a_out_end_offset = a_start + 4;
        return true;
    }
    
    // Check for "false"
    if(a_start + 5 <= a_input_len &&
       memcmp(a_input + a_start, "false", 5) == 0) {
        l_value->type = DAP_JSON_TYPE_BOOLEAN;
        l_value->boolean = false;
        *a_out_value = l_value;
        *a_out_end_offset = a_start + 5;
        return true;
    }
    
    // Check for "null"
    if(a_start + 4 <= a_input_len &&
       memcmp(a_input + a_start, "null", 4) == 0) {
        l_value->type = DAP_JSON_TYPE_NULL;
        *a_out_value = l_value;
        *a_out_end_offset = a_start + 4;
        return true;
    }
    
    log_it(L_ERROR, "Invalid literal at offset %u", a_start);
    return false;
}

/* ========================================================================== */
/*                         STAGE 2 MAIN PARSER                                */
/* ========================================================================== */

/**
 * @brief Error code to string
 */
const char *dap_json_stage2_error_to_string(dap_json_stage2_error_t a_error)
{
    switch(a_error) {
        case STAGE2_SUCCESS:                   return "Success";
        case STAGE2_ERROR_INVALID_INPUT:       return "Invalid input";
        case STAGE2_ERROR_OUT_OF_MEMORY:       return "Out of memory";
        case STAGE2_ERROR_UNEXPECTED_TOKEN:    return "Unexpected token";
        case STAGE2_ERROR_INVALID_NUMBER:      return "Invalid number";
        case STAGE2_ERROR_INVALID_STRING:      return "Invalid string";
        case STAGE2_ERROR_INVALID_LITERAL:     return "Invalid literal";
        case STAGE2_ERROR_UNEXPECTED_END:      return "Unexpected end";
        case STAGE2_ERROR_MISMATCHED_BRACKETS: return "Mismatched brackets";
        case STAGE2_ERROR_MISSING_VALUE:       return "Missing value";
        case STAGE2_ERROR_MISSING_COLON:       return "Missing colon";
        case STAGE2_ERROR_DUPLICATE_KEY:       return "Duplicate key";
        default:                               return "Unknown error";
    }
}

/**
 * @brief Initialize Stage 2 parser
 */
dap_json_stage2_t *dap_json_stage2_init(const dap_json_stage1_t *a_stage1)
{
    if(!a_stage1) {
        log_it(L_ERROR, "NULL Stage 1 input");
        return NULL;
    }
    
    if(a_stage1->error_code != STAGE1_SUCCESS) {
        log_it(L_ERROR, "Stage 1 has errors, cannot proceed to Stage 2");
        return NULL;
    }
    
    dap_json_stage2_t *l_stage2 = DAP_NEW_Z(dap_json_stage2_t);
    if(!l_stage2) {
        log_it(L_ERROR, "Failed to allocate Stage 2 parser");
        return NULL;
    }
    
    l_stage2->input = a_stage1->input;
    l_stage2->input_len = a_stage1->input_len;
    l_stage2->indices = a_stage1->indices;
    l_stage2->indices_count = a_stage1->indices_count;
    l_stage2->max_depth = MAX_NESTING_DEPTH;
    
    // Create Arena for DOM nodes (estimate: ~100 bytes per token)
    size_t l_estimated_size = a_stage1->indices_count * 100;
    if (l_estimated_size < 4096) {
        l_estimated_size = 4096;
    }
    
    l_stage2->arena = dap_arena_new(l_estimated_size);
    if (!l_stage2->arena) {
        log_it(L_ERROR, "Failed to create Arena allocator");
        DAP_DELETE(l_stage2);
        return NULL;
    }
    
    // Create String Pool for object keys (estimate: token_count / 4)
    // String Pool will use the same Arena for all allocations
    size_t l_string_pool_capacity = a_stage1->indices_count / 4;
    if (l_string_pool_capacity < 32) {
        l_string_pool_capacity = 32;
    }
    
    l_stage2->string_pool = dap_string_pool_new(l_stage2->arena, l_string_pool_capacity);
    if (!l_stage2->string_pool) {
        log_it(L_ERROR, "Failed to create String Pool");
        dap_arena_free(l_stage2->arena);
        DAP_DELETE(l_stage2);
        return NULL;
    }
    
    log_it(L_DEBUG, "Stage 2 initialized with %zu indices, Arena: %zu bytes, String Pool: %zu capacity", 
           l_stage2->indices_count, l_estimated_size, l_string_pool_capacity);
    
    return l_stage2;
}

/**
 * @brief Get root value
 */
dap_json_value_t *dap_json_stage2_get_root(const dap_json_stage2_t *a_stage2)
{
    if(!a_stage2) {
        return NULL;
    }
    
    return a_stage2->root;
}

/**
 * @brief Get Stage 2 statistics
 */
void dap_json_stage2_get_stats(
    const dap_json_stage2_t *a_stage2,
    size_t *a_out_objects,
    size_t *a_out_arrays,
    size_t *a_out_strings,
    size_t *a_out_numbers
)
{
    if(!a_stage2) {
        return;
    }
    
    if(a_out_objects) *a_out_objects = a_stage2->objects_created;
    if(a_out_arrays)  *a_out_arrays  = a_stage2->arrays_created;
    if(a_out_strings) *a_out_strings = a_stage2->strings_created;
    if(a_out_numbers) *a_out_numbers = a_stage2->numbers_created;
}

/**
 * @brief Free Stage 2 parser
 */
void dap_json_stage2_free(dap_json_stage2_t *a_stage2)
{
    if(!a_stage2) {
        return;
    }
    
    // Free Arena (frees all DOM nodes allocated from it)
    dap_arena_free(a_stage2->arena);
    
    // Free String Pool (frees all interned strings)
    dap_string_pool_free(a_stage2->string_pool);
    
    // NOTE: root pointer becomes invalid after Arena free
    // Caller should use root BEFORE calling this function
    DAP_DELETE(a_stage2);
}

/* Forward declaration для recursive parsing */
static dap_json_value_t *s_parse_value(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
);

/**
 * @brief Parse array value recursively
 * @details Начинает с '[' index, парсит elements, ожидает ']'
 * 
 * @param[in,out] a_stage2 Stage 2 parser
 * @param[in,out] a_idx Current index (will be updated)
 * @return Parsed array value, or NULL on error
 */
static dap_json_value_t *s_parse_array(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    // Check depth
    if(a_stage2->current_depth >= a_stage2->max_depth) {
        snprintf(a_stage2->error_message, sizeof(a_stage2->error_message),
                 "Maximum nesting depth (%zu) exceeded", a_stage2->max_depth);
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
        return NULL;
    }
    
    a_stage2->current_depth++;
    
    // Create array value using Arena
    dap_json_value_t *l_array = s_create_value_arena(a_stage2->arena);
    if(!l_array) {
        a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
        a_stage2->current_depth--;
        return NULL;
    }
    
    l_array->type = DAP_JSON_TYPE_ARRAY;
    l_array->array.elements = NULL;
    l_array->array.count = 0;
    l_array->array.capacity = 0;
    
    a_stage2->arrays_created++;
    (*a_idx)++; // Skip '['
    
    // Check for empty array
    if(*a_idx < a_stage2->indices_count && 
       a_stage2->indices[*a_idx].character == ']') {
        (*a_idx)++; // Skip ']'
        a_stage2->current_depth--;
        return l_array;
    }
    
    // Parse elements
    while(*a_idx < a_stage2->indices_count) {
        // Parse value
        dap_json_value_t *l_element = s_parse_value(a_stage2, a_idx);
        if(!l_element) {
            dap_json_value_v2_free(l_array);
            a_stage2->current_depth--;
            return NULL;
        }
        
        if(!s_array_add_arena(a_stage2->arena, l_array, l_element)) {
            dap_json_value_v2_free(l_element);
            dap_json_value_v2_free(l_array);
            a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
            a_stage2->current_depth--;
            return NULL;
        }
        
        // Check next: ',' or ']'
        if(*a_idx >= a_stage2->indices_count) {
            dap_json_value_v2_free(l_array);
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
            a_stage2->current_depth--;
            return NULL;
        }
        
        uint8_t l_next = a_stage2->indices[*a_idx].character;
        
        if(l_next == ']') {
            (*a_idx)++; // Skip ']'
            a_stage2->current_depth--;
            return l_array;
        }
        else if(l_next == ',') {
            (*a_idx)++; // Skip ','
            continue;
        }
        else {
            dap_json_value_v2_free(l_array);
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->current_depth--;
            return NULL;
        }
    }
    
    dap_json_value_v2_free(l_array);
    a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
    a_stage2->current_depth--;
    return NULL;
}

/**
 * @brief Parse object value recursively
 * @details Начинает с '{' index, парсит key-value pairs, ожидает '}'
 * 
 * @param[in,out] a_stage2 Stage 2 parser
 * @param[in,out] a_idx Current index (will be updated)
 * @return Parsed object value, or NULL on error
 */
static dap_json_value_t *s_parse_object(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    // Check depth
    if(a_stage2->current_depth >= a_stage2->max_depth) {
        snprintf(a_stage2->error_message, sizeof(a_stage2->error_message),
                 "Maximum nesting depth (%zu) exceeded", a_stage2->max_depth);
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
        return NULL;
    }
    
    a_stage2->current_depth++;
    
    // Create object value using Arena
    dap_json_value_t *l_object = s_create_value_arena(a_stage2->arena);
    if(!l_object) {
        a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
        a_stage2->current_depth--;
        return NULL;
    }
    
    l_object->type = DAP_JSON_TYPE_OBJECT;
    l_object->object.pairs = NULL;
    l_object->object.count = 0;
    l_object->object.capacity = 0;
    
    a_stage2->objects_created++;
    (*a_idx)++; // Skip '{'
    
    // Check for empty object
    if(*a_idx < a_stage2->indices_count && 
       a_stage2->indices[*a_idx].character == '}') {
        (*a_idx)++; // Skip '}'
        a_stage2->current_depth--;
        return l_object;
    }
    
    // Parse key-value pairs
    while(*a_idx < a_stage2->indices_count) {
        // Parse key (must be string)
        uint32_t l_key_offset = a_stage2->indices[*a_idx].position;
        
        if(a_stage2->input[l_key_offset] != '"') {
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->error_position = l_key_offset;
            a_stage2->current_depth--;
            return NULL;
        }
        
        dap_json_value_t *l_key_value = NULL;
        uint32_t l_key_end = 0;
        
        if(!s_parse_string(a_stage2, l_key_offset,
                           &l_key_value, &l_key_end)) {
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_INVALID_STRING;
            a_stage2->error_position = l_key_offset;
            a_stage2->current_depth--;
            return NULL;
        }
        
        const char *l_key = l_key_value->string.data;
        (*a_idx)++; // Skip key string index
        
        // Expect ':'
        if(*a_idx >= a_stage2->indices_count ||
           a_stage2->indices[*a_idx].character != ':') {
            dap_json_value_v2_free(l_key_value);
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_MISSING_COLON;
            a_stage2->current_depth--;
            return NULL;
        }
        
        (*a_idx)++; // Skip ':'
        
        // Parse value
        dap_json_value_t *l_value = s_parse_value(a_stage2, a_idx);
        if(!l_value) {
            dap_json_value_v2_free(l_key_value);
            dap_json_value_v2_free(l_object);
            a_stage2->current_depth--;
            return NULL;
        }
        
        // Add to object using Arena + String Pool
        if(!s_object_add_arena(a_stage2->arena, a_stage2->string_pool, l_object, l_key, l_value)) {
            dap_json_value_v2_free(l_value);
            dap_json_value_v2_free(l_key_value);
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_DUPLICATE_KEY;
            a_stage2->current_depth--;
            return NULL;
        }
        
        // Key now interned in String Pool, key_value can be freed
        // (но Arena owns it, так что просто забудем про него)
        // dap_json_value_v2_free(l_key_value); - не нужно, Arena owns
        
        // Check next: ',' or '}'
        if(*a_idx >= a_stage2->indices_count) {
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
            a_stage2->current_depth--;
            return NULL;
        }
        
        uint8_t l_next = a_stage2->indices[*a_idx].character;
        
        if(l_next == '}') {
            (*a_idx)++; // Skip '}'
            a_stage2->current_depth--;
            return l_object;
        }
        else if(l_next == ',') {
            (*a_idx)++; // Skip ','
            continue;
        }
        else {
            dap_json_value_v2_free(l_object);
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->current_depth--;
            return NULL;
        }
    }
    
    dap_json_value_v2_free(l_object);
    a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
    a_stage2->current_depth--;
    return NULL;
}

/**
 * @brief Parse value (any JSON value)
 * @details Диспетчер для всех типов значений
 * 
 * @param[in,out] a_stage2 Stage 2 parser
 * @param[in,out] a_idx Current index (will be updated)
 * @return Parsed value, or NULL on error
 */
static dap_json_value_t *s_parse_value(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    if(*a_idx >= a_stage2->indices_count) {
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
        return NULL;
    }
    
    uint32_t l_offset = a_stage2->indices[*a_idx].position;
    uint8_t l_type = a_stage2->indices[*a_idx].character;
    uint8_t l_char = a_stage2->input[l_offset];
    
    dap_json_value_t *l_value = NULL;
    uint32_t l_end_offset = 0;
    
    // Structural characters
    if(l_type == '{') {
        return s_parse_object(a_stage2, a_idx);
    }
    else if(l_type == '[') {
        return s_parse_array(a_stage2, a_idx);
    }
    
    // String
    else if(l_char == '"') {
        if(!s_parse_string(a_stage2, l_offset,
                           &l_value, &l_end_offset)) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_STRING;
            a_stage2->error_position = l_offset;
            return NULL;
        }
        a_stage2->strings_created++;
        (*a_idx)++;
        return l_value;
    }
    
    // Number
    else if(l_char == '-' || (l_char >= '0' && l_char <= '9')) {
        // Find end of number
        uint32_t l_number_end = l_offset + 1;
        while(l_number_end < a_stage2->input_len) {
            uint8_t l_c = a_stage2->input[l_number_end];
            if((l_c >= '0' && l_c <= '9') || l_c == '.' || l_c == 'e' || l_c == 'E' ||
               l_c == '+' || l_c == '-') {
                l_number_end++;
            }
            else {
                break;
            }
        }
        
        if(!s_parse_number(a_stage2, l_offset, l_number_end, &l_value)) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_NUMBER;
            a_stage2->error_position = l_offset;
            return NULL;
        }
        a_stage2->numbers_created++;
        (*a_idx)++;
        return l_value;
    }
    
    // Literal (true, false, null)
    else {
        if(!s_parse_literal(a_stage2, l_offset,
                            &l_value, &l_end_offset)) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_LITERAL;
            a_stage2->error_position = l_offset;
            return NULL;
        }
        (*a_idx)++;
        return l_value;
    }
}

/**
 * @brief Run Stage 2 DOM building
 */
dap_json_stage2_error_t dap_json_stage2_run(dap_json_stage2_t *a_stage2)
{
    if(!a_stage2) {
        return STAGE2_ERROR_INVALID_INPUT;
    }
    
    if(a_stage2->indices_count == 0) {
        log_it(L_ERROR, "No structural indices from Stage 1");
        a_stage2->error_code = STAGE2_ERROR_INVALID_INPUT;
        return a_stage2->error_code;
    }
    
    log_it(L_DEBUG, "Starting Stage 2 DOM building...");
    
    size_t l_idx = 0;
    a_stage2->root = s_parse_value(a_stage2, &l_idx);
    
    if(!a_stage2->root) {
        log_it(L_ERROR, "Stage 2 parsing failed: %s (position %zu)",
               dap_json_stage2_error_to_string(a_stage2->error_code),
               a_stage2->error_position);
        return a_stage2->error_code;
    }
    
    log_it(L_INFO, "Stage 2 completed: objects=%zu arrays=%zu strings=%zu numbers=%zu",
           a_stage2->objects_created, a_stage2->arrays_created,
           a_stage2->strings_created, a_stage2->numbers_created);
    
    a_stage2->error_code = STAGE2_SUCCESS;
    return STAGE2_SUCCESS;
}

