/*
 * Authors:
 * Dmitry Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2024-2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_common.h"
#include "dap_json_type.h"
#include "internal/dap_json_serialization.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <locale.h>

/* ========================================================================== */
/*                     JSON SERIALIZATION IMPLEMENTATION                      */
/* ========================================================================== */

/**
 * @brief Locale-independent snprintf for doubles
 * @details JSON always uses '.' as decimal separator, regardless of system locale
 * @param a_buf Output buffer
 * @param a_size Buffer size
 * @param a_format Format string (must contain double format)
 * @param a_value Double value to format
 * @return Number of characters written (like snprintf)
 */
static int s_snprintf_double_c_locale(char *a_buf, size_t a_size, const char *a_format, double a_value)
{
    // Save current locale
    char *l_old_locale = setlocale(LC_NUMERIC, NULL);
    if (l_old_locale) {
        l_old_locale = strdup(l_old_locale);
    }
    
    // Switch to "C" locale for formatting (uses '.' as decimal separator)
    setlocale(LC_NUMERIC, "C");
    
    // Format with standard snprintf
    int l_result = snprintf(a_buf, a_size, a_format, a_value);
    
    // Restore original locale
    if (l_old_locale) {
        setlocale(LC_NUMERIC, l_old_locale);
        free(l_old_locale);
    }
    
    return l_result;
}

/**
 * @brief Append string to buffer with reallocation if needed
 */
static bool s_append_string(char **a_buffer, size_t *a_size, size_t *a_capacity, const char *a_str)
{
    if (!a_buffer || !a_size || !a_capacity || !a_str) {
        return false;
    }
    
    size_t l_str_len = strlen(a_str);
    while (*a_size + l_str_len + 1 > *a_capacity) {
        size_t l_new_capacity = *a_capacity * 2;
        char *l_new_buffer = DAP_REALLOC(*a_buffer, l_new_capacity);
        if (!l_new_buffer) {
            return false;
        }
        *a_buffer = l_new_buffer;
        *a_capacity = l_new_capacity;
    }
    
    memcpy(*a_buffer + *a_size, a_str, l_str_len);
    *a_size += l_str_len;
    (*a_buffer)[*a_size] = '\0';
    return true;
}

/**
 * @brief Escape string for JSON
 */
static char* s_escape_string(const char *a_str)
{
    if (!a_str) {
        return NULL;
    }
    
    size_t l_len = strlen(a_str);
    size_t l_capacity = l_len * 2 + 3; // Worst case: every char escaped + quotes + null
    char *l_result = DAP_NEW_Z_SIZE(char, l_capacity);
    if (!l_result) {
        return NULL;
    }
    
    size_t l_pos = 0;
    l_result[l_pos++] = '"';
    
    for (size_t i = 0; i < l_len; i++) {
        unsigned char c = (unsigned char)a_str[i];
        
        switch (c) {
            case '"':  l_result[l_pos++] = '\\'; l_result[l_pos++] = '"'; break;
            case '\\': l_result[l_pos++] = '\\'; l_result[l_pos++] = '\\'; break;
            case '\b': l_result[l_pos++] = '\\'; l_result[l_pos++] = 'b'; break;
            case '\f': l_result[l_pos++] = '\\'; l_result[l_pos++] = 'f'; break;
            case '\n': l_result[l_pos++] = '\\'; l_result[l_pos++] = 'n'; break;
            case '\r': l_result[l_pos++] = '\\'; l_result[l_pos++] = 'r'; break;
            case '\t': l_result[l_pos++] = '\\'; l_result[l_pos++] = 't'; break;
            default:
                if (c < 32) {
                    // Control character - use \uXXXX
                    char l_buf[7];
                    snprintf(l_buf, sizeof(l_buf), "\\u%04x", c);
                    memcpy(l_result + l_pos, l_buf, 6);
                    l_pos += 6;
                } else {
                    l_result[l_pos++] = c;
                }
                break;
        }
        
        // Reallocate if needed
        if (l_pos + 10 > l_capacity) {
            l_capacity *= 2;
            char *l_new = DAP_REALLOC(l_result, l_capacity);
            if (!l_new) {
                DAP_DELETE(l_result);
                return NULL;
            }
            l_result = l_new;
        }
    }
    
    l_result[l_pos++] = '"';
    l_result[l_pos] = '\0';
    return l_result;
}

// Forward declaration
static bool s_stringify_value(dap_json_value_t *a_value, char **a_buffer, size_t *a_size, size_t *a_capacity, int a_indent, bool a_pretty);

/**
 * @brief Stringify array
 */
static bool s_stringify_array(dap_json_array_t *a_array, char **a_buffer, size_t *a_size, size_t *a_capacity, int a_indent, bool a_pretty)
{
    if (!s_append_string(a_buffer, a_size, a_capacity, "[")) {
        return false;
    }
    
    for (size_t i = 0; i < a_array->count; i++) {
        if (a_pretty && a_array->count > 0) {
            s_append_string(a_buffer, a_size, a_capacity, "\n");
            for (int j = 0; j < a_indent + 1; j++) {
                s_append_string(a_buffer, a_size, a_capacity, "  ");
            }
        }
        
        if (!s_stringify_value(a_array->elements[i], a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
            return false;
        }
        
        if (i < a_array->count - 1) {
            s_append_string(a_buffer, a_size, a_capacity, ",");
        }
    }
    
    if (a_pretty && a_array->count > 0) {
        s_append_string(a_buffer, a_size, a_capacity, "\n");
        for (int j = 0; j < a_indent; j++) {
            s_append_string(a_buffer, a_size, a_capacity, "  ");
        }
    }
    
    return s_append_string(a_buffer, a_size, a_capacity, "]");
}

/**
 * @brief Stringify object
 */
static bool s_stringify_object(dap_json_object_t *a_object, char **a_buffer, size_t *a_size, size_t *a_capacity, int a_indent, bool a_pretty)
{
    if (!s_append_string(a_buffer, a_size, a_capacity, "{")) {
        return false;
    }
    
    for (size_t i = 0; i < a_object->count; i++) {
        if (a_pretty) {
            s_append_string(a_buffer, a_size, a_capacity, "\n");
            for (int j = 0; j < a_indent + 1; j++) {
                s_append_string(a_buffer, a_size, a_capacity, "  ");
            }
        }
        
        // Key
        char *l_escaped_key = s_escape_string(a_object->pairs[i].key);
        if (!l_escaped_key) {
            return false;
        }
        s_append_string(a_buffer, a_size, a_capacity, l_escaped_key);
        DAP_DELETE(l_escaped_key);
        
        s_append_string(a_buffer, a_size, a_capacity, a_pretty ? ": " : ":");
        
        // Value
        if (!s_stringify_value(a_object->pairs[i].value, a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
            return false;
        }
        
        if (i < a_object->count - 1) {
            s_append_string(a_buffer, a_size, a_capacity, ",");
        }
    }
    
    if (a_pretty && a_object->count > 0) {
        s_append_string(a_buffer, a_size, a_capacity, "\n");
        for (int j = 0; j < a_indent; j++) {
            s_append_string(a_buffer, a_size, a_capacity, "  ");
        }
    }
    
    return s_append_string(a_buffer, a_size, a_capacity, "}");
}

/**
 * @brief Stringify value recursively
 */
static bool s_stringify_value(dap_json_value_t *a_value, char **a_buffer, size_t *a_size, size_t *a_capacity, int a_indent, bool a_pretty)
{
    if (!a_value) {
        return s_append_string(a_buffer, a_size, a_capacity, "null");
    }
    
    char l_buf[64];
    
    switch (a_value->type) {
        case DAP_JSON_TYPE_NULL:
            return s_append_string(a_buffer, a_size, a_capacity, "null");
            
        case DAP_JSON_TYPE_BOOLEAN:
            return s_append_string(a_buffer, a_size, a_capacity, a_value->boolean ? "true" : "false");
            
        case DAP_JSON_TYPE_INT:
            snprintf(l_buf, sizeof(l_buf), "%" PRId64, a_value->number.i);
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
            
        case DAP_JSON_TYPE_UINT64:
            snprintf(l_buf, sizeof(l_buf), "%" PRIu64, a_value->number.u64);
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
            
        case DAP_JSON_TYPE_UINT128: {
            // Serialize uint128 as hex string quoted (for JSON compatibility)
            uint128_t l_u128 = a_value->number.u128;
            uint64_t l_hi = (uint64_t)(l_u128 >> 64);
            uint64_t l_lo = (uint64_t)l_u128;
            char l_u128_str[48];
            snprintf(l_u128_str, sizeof(l_u128_str), "\"0x%016" PRIx64 "%016" PRIx64 "\"", l_hi, l_lo);
            return s_append_string(a_buffer, a_size, a_capacity, l_u128_str);
        }
            
        case DAP_JSON_TYPE_UINT256: {
            // Serialize uint256 as hex string quoted
            uint256_t l_u256 = a_value->number.u256;
            char l_u256_str[128];
            snprintf(l_u256_str, sizeof(l_u256_str), "\"0x%016" PRIx64 "%016" PRIx64 "\"",
                     (uint64_t)(l_u256.hi), (uint64_t)(l_u256.lo));
            return s_append_string(a_buffer, a_size, a_capacity, l_u256_str);
        }
            
        case DAP_JSON_TYPE_DOUBLE: {
            // Format double with appropriate precision
            double d = a_value->number.d;
            
            // Handle Infinity and NaN (not standard JSON, but useful)
            if (isinf(d)) {
                return s_append_string(a_buffer, a_size, a_capacity, 
                                      d > 0 ? "\"Infinity\"" : "\"-Infinity\"");
            }
            if (isnan(d)) {
                return s_append_string(a_buffer, a_size, a_capacity, "\"NaN\"");
            }
            
            if (d == (int64_t)d) {
                s_snprintf_double_c_locale(l_buf, sizeof(l_buf), "%.1f", d); // e.g., 3.0
            } else {
                // IEEE 754 double precision requires 17 significant digits for lossless round-trip
                // (53 bits mantissa = log10(2^53) ≈ 15.95 digits, need 17 for full precision)
                s_snprintf_double_c_locale(l_buf, sizeof(l_buf), "%.17g", d);
            }
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
        }
            
        case DAP_JSON_TYPE_STRING: {
            char *l_escaped = s_escape_string(a_value->string.data);
            if (!l_escaped) {
                return false;
            }
            bool l_result = s_append_string(a_buffer, a_size, a_capacity, l_escaped);
            DAP_DELETE(l_escaped);
            return l_result;
        }
            
        case DAP_JSON_TYPE_ARRAY:
            return s_stringify_array(&a_value->array, a_buffer, a_size, a_capacity, a_indent, a_pretty);
            
        case DAP_JSON_TYPE_OBJECT:
            return s_stringify_object(&a_value->object, a_buffer, a_size, a_capacity, a_indent, a_pretty);
            
        default:
            return false;
    }
}

/* ========================================================================== */
/*                          PUBLIC API                                        */
/* ========================================================================== */

/**
 * @brief Serialize JSON value to string (compact format)
 */
char* dap_json_value_serialize(dap_json_value_t *a_value)
{
    if (!a_value) {
        return NULL;
    }
    
    size_t l_capacity = 1024;
    size_t l_size = 0;
    char *l_buffer = DAP_NEW_Z_SIZE(char, l_capacity);
    if (!l_buffer) {
        return NULL;
    }
    
    if (!s_stringify_value(a_value, &l_buffer, &l_size, &l_capacity, 0, false)) {
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    return l_buffer;
}

/**
 * @brief Serialize JSON value to string (pretty-printed format)
 */
char* dap_json_value_serialize_pretty(dap_json_value_t *a_value)
{
    if (!a_value) {
        return NULL;
    }
    
    size_t l_capacity = 1024;
    size_t l_size = 0;
    char *l_buffer = DAP_NEW_Z_SIZE(char, l_capacity);
    if (!l_buffer) {
        return NULL;
    }
    
    if (!s_stringify_value(a_value, &l_buffer, &l_size, &l_capacity, 0, true)) {
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    return l_buffer;
}

