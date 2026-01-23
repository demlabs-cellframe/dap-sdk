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
#include "dap_json_value.h"  // Phase 2.0.4: For helper functions
#include "internal/dap_json_stage2.h"   // Phase 2.0.4: For stage2 context
#include "internal/dap_json_serialization.h"
#include "internal/dap_json_internal.h"  // For storage structures
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <locale.h>

#define LOG_TAG "json_serialization"

// Forward declarations for MUTABLE mode storage structures
typedef struct {
    dap_json_value_t **elements;  // Array of pointers
    size_t count;                  // Current number of elements
    size_t capacity;               // Allocated capacity
} dap_json_array_storage_t;

typedef struct {
    char **keys;
    dap_json_value_t **values;
    size_t count;
    size_t capacity;
} dap_json_object_storage_t;

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
 * @brief Escape string for JSON with explicit length (for zero-copy support)
 * @param a_str String to escape (may not be null-terminated!)
 * @param a_len String length
 * @return Escaped string (with quotes), caller must free with DAP_DELETE
 */
static char* s_escape_string_n(const char *a_str, size_t a_len)
{
    if (!a_str) {
        return NULL;
    }
    
    size_t l_capacity = a_len * 2 + 3; // Worst case: every char escaped + quotes + null
    char *l_result = DAP_NEW_Z_SIZE(char, l_capacity);
    if (!l_result) {
        return NULL;
    }
    
    size_t l_pos = 0;
    l_result[l_pos++] = '"';
    
    for (size_t i = 0; i < a_len; i++) {
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

// Forward declaration (Phase 2.0.4: needs source_buffer + stage2)
static bool s_stringify_value(
    dap_json_value_t *a_value,
    const char *a_source_buffer,
    struct dap_json_stage2 *a_stage2,
    char **a_buffer,
    size_t *a_size,
    size_t *a_capacity,
    int a_indent,
    bool a_pretty
);

/**
 * @brief Stringify array (Phase 2.0.4: works with 8-byte values + source buffer)
 */
static bool s_stringify_array(
    dap_json_value_t *a_array_value,
    const char *a_source_buffer,
    struct dap_json_stage2 *a_stage2,
    char **a_buffer,
    size_t *a_size,
    size_t *a_capacity,
    int a_indent,
    bool a_pretty
)
{
    if (!s_append_string(a_buffer, a_size, a_capacity, "[")) {
        return false;
    }
    
    // Phase 2.0.4: Array elements stored as flat array of indices
    uint32_t *element_indices = (uint32_t*)(uintptr_t)a_array_value->offset;
    size_t count = a_array_value->length;
    
    for (size_t i = 0; i < count; i++) {
        if (a_pretty && count > 0) {
            s_append_string(a_buffer, a_size, a_capacity, "\n");
            for (int j = 0; j < a_indent + 1; j++) {
                s_append_string(a_buffer, a_size, a_capacity, "  ");
            }
        }
        
        // Get element value from Stage 2
        uint32_t element_idx = element_indices[i];
        dap_json_value_t *element_value = &a_stage2->values[element_idx];
        
        if (!s_stringify_value(element_value, a_source_buffer, a_stage2, a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
            return false;
        }
        
        if (i < count - 1) {
            s_append_string(a_buffer, a_size, a_capacity, ",");
        }
    }
    
    if (a_pretty && count > 0) {
        s_append_string(a_buffer, a_size, a_capacity, "\n");
        for (int j = 0; j < a_indent; j++) {
            s_append_string(a_buffer, a_size, a_capacity, "  ");
        }
    }
    
    return s_append_string(a_buffer, a_size, a_capacity, "]");
}

/**
 * @brief Stringify object (Phase 2.0.4: works with 8-byte values + source buffer)
 */
static bool s_stringify_object(
    dap_json_value_t *a_object_value,
    const char *a_source_buffer,
    struct dap_json_stage2 *a_stage2,
    char **a_buffer,
    size_t *a_size,
    size_t *a_capacity,
    int a_indent,
    bool a_pretty
)
{
    if (!s_append_string(a_buffer, a_size, a_capacity, "{")) {
        return false;
    }
    
    // Phase 2.0.4: Object pairs stored as flat array [key_idx, val_idx, key_idx, val_idx, ...]
    uint32_t *pair_indices = (uint32_t*)(uintptr_t)a_object_value->offset;
    size_t count = a_object_value->length;  // Number of pairs
    
    for (size_t i = 0; i < count; i++) {
        if (a_pretty) {
            s_append_string(a_buffer, a_size, a_capacity, "\n");
            for (int j = 0; j < a_indent + 1; j++) {
                s_append_string(a_buffer, a_size, a_capacity, "  ");
            }
        }
        
        // Get key and value from Stage 2
        uint32_t key_idx = pair_indices[i * 2];
        uint32_t val_idx = pair_indices[i * 2 + 1];
        
        dap_json_value_t *key_value = &a_stage2->values[key_idx];
        dap_json_value_t *value_value = &a_stage2->values[val_idx];
        
        // Key: get string from source buffer
        const char *key_data = dap_json_get_ptr(key_value, a_source_buffer);
        size_t key_len = dap_json_get_length(key_value);
        
        char *l_escaped_key = s_escape_string_n(key_data, key_len);
        if (!l_escaped_key) {
            return false;
        }
        s_append_string(a_buffer, a_size, a_capacity, l_escaped_key);
        DAP_DELETE(l_escaped_key);
        
        s_append_string(a_buffer, a_size, a_capacity, a_pretty ? ": " : ":");
        
        // Value
        if (!s_stringify_value(value_value, a_source_buffer, a_stage2, a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
            return false;
        }
        
        if (i < count - 1) {
            s_append_string(a_buffer, a_size, a_capacity, ",");
        }
    }
    
    if (a_pretty && count > 0) {
        s_append_string(a_buffer, a_size, a_capacity, "\n");
        for (int j = 0; j < a_indent; j++) {
            s_append_string(a_buffer, a_size, a_capacity, "  ");
        }
    }
    
    return s_append_string(a_buffer, a_size, a_capacity, "}");
}

/**
 * @brief Stringify value recursively (Phase 2.0.4: works with 8-byte values)
 */
static bool s_stringify_value(
    dap_json_value_t *a_value,
    const char *a_source_buffer,
    struct dap_json_stage2 *a_stage2,
    char **a_buffer,
    size_t *a_size,
    size_t *a_capacity,
    int a_indent,
    bool a_pretty
)
{
    if (!a_value) {
        return s_append_string(a_buffer, a_size, a_capacity, "null");
    }
    
    char l_buf[128];
    
    switch (a_value->type) {
        case DAP_JSON_TYPE_NULL:
            return s_append_string(a_buffer, a_size, a_capacity, "null");
            
        case DAP_JSON_TYPE_BOOLEAN: {
            // Phase 2.0.4: Boolean stored in source, get from buffer
            const char *bool_str = dap_json_get_ptr(a_value, a_source_buffer);
            bool is_true = (bool_str[0] == 't');  // "true" or "false"
            return s_append_string(a_buffer, a_size, a_capacity, is_true ? "true" : "false");
        }
            
        case DAP_JSON_TYPE_INT:
        case DAP_JSON_TYPE_DOUBLE: {
            // Phase 2.0.4: Lazy parsing - parse number from source
            const char *num_str = dap_json_get_ptr(a_value, a_source_buffer);
            size_t num_len = dap_json_get_length(a_value);
            
            // Copy number string (it's NOT null-terminated in source!)
            if (num_len >= sizeof(l_buf)) {
                log_it(L_ERROR, "Number too long: %zu bytes", num_len);
                return false;
            }
            memcpy(l_buf, num_str, num_len);
            l_buf[num_len] = '\0';
            
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
        }
            
        case DAP_JSON_TYPE_UINT64:
        case DAP_JSON_TYPE_UINT128:
        case DAP_JSON_TYPE_UINT256: {
            // Phase 2.0.4: Same as INT/DOUBLE - lazily parsed
            const char *num_str = dap_json_get_ptr(a_value, a_source_buffer);
            size_t num_len = dap_json_get_length(a_value);
            
            if (num_len >= sizeof(l_buf)) {
                log_it(L_ERROR, "Number too long: %zu bytes", num_len);
                return false;
            }
            memcpy(l_buf, num_str, num_len);
            l_buf[num_len] = '\0';
            
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
        }
            
        case DAP_JSON_TYPE_STRING: {
            // Phase 2.0.4: String in source buffer (zero-copy!)
            const char *str_data = dap_json_get_ptr(a_value, a_source_buffer);
            size_t str_len = dap_json_get_length(a_value);
            
            char *l_escaped = s_escape_string_n(str_data, str_len);
            if (!l_escaped) {
                return false;
            }
            bool result = s_append_string(a_buffer, a_size, a_capacity, l_escaped);
            DAP_DELETE(l_escaped);
            return result;
        }
            
        case DAP_JSON_TYPE_ARRAY:
            return s_stringify_array(a_value, a_source_buffer, a_stage2, a_buffer, a_size, a_capacity, a_indent, a_pretty);
            
        case DAP_JSON_TYPE_OBJECT:
            return s_stringify_object(a_value, a_source_buffer, a_stage2, a_buffer, a_size, a_capacity, a_indent, a_pretty);
            
        default:
            log_it(L_ERROR, "Unknown JSON type: %d", a_value->type);
            return false;
    }
}

/* ========================================================================== */
/*                          PUBLIC API                                        */
/* ========================================================================== */

/**
 * @brief Serialize JSON value to string (compact format)
 * Phase 2.0.4: Needs source_buffer and stage2 context
 */
/**
 * @brief Recursively serialize JSON value to string buffer
 * @param a_value Value to serialize
 * @param a_buffer Output buffer (will be reallocated if needed)
 * @param a_size Current size of data in buffer
 * @param a_capacity Current capacity of buffer
 * @param a_indent Current indentation level
 * @param a_pretty Pretty-print mode
 * @return true on success, false on error
 */
static bool s_serialize_value_recursive(
    const dap_json_value_t *a_value,
    char **a_buffer,
    size_t *a_size,
    size_t *a_capacity,
    int a_indent,
    bool a_pretty
)
{
    if (!a_value) {
        return s_append_string(a_buffer, a_size, a_capacity, "null");
    }
    
    switch (a_value->type) {
        case DAP_JSON_TYPE_NULL:
            return s_append_string(a_buffer, a_size, a_capacity, "null");
            
        case DAP_JSON_TYPE_BOOLEAN:
            return s_append_string(a_buffer, a_size, a_capacity,
                                  a_value->offset ? "true" : "false");
            
        case DAP_JSON_TYPE_INT: {
            char l_buf[32];
            // Check if inline or allocated
            if (a_value->length == 0) {
                // Inline int32
                snprintf(l_buf, sizeof(l_buf), "%d", (int32_t)a_value->offset);
            } else {
                // Allocated int64
                int64_t *l_ptr = (int64_t*)dap_json_get_storage_ptr(a_value);
                if (!l_ptr) return false;
                snprintf(l_buf, sizeof(l_buf), "%" PRId64, *l_ptr);
            }
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
        }
            
        case DAP_JSON_TYPE_DOUBLE: {
            char l_buf[64];
            double *l_ptr = (double*)dap_json_get_storage_ptr(a_value);
            if (!l_ptr) return false;
            s_snprintf_double_c_locale(l_buf, sizeof(l_buf), "%.17g", *l_ptr);
            return s_append_string(a_buffer, a_size, a_capacity, l_buf);
        }
            
        case DAP_JSON_TYPE_STRING: {
            const char *l_str = (const char*)dap_json_get_storage_ptr(a_value);
            if (!l_str) return false;
            
            if (!s_append_string(a_buffer, a_size, a_capacity, "\"")) return false;
            
            // Escape special characters
            for (const char *p = l_str; *p; p++) {
                switch (*p) {
                    case '"':  if (!s_append_string(a_buffer, a_size, a_capacity, "\\\"")) return false; break;
                    case '\\': if (!s_append_string(a_buffer, a_size, a_capacity, "\\\\")) return false; break;
                    case '\b': if (!s_append_string(a_buffer, a_size, a_capacity, "\\b")) return false; break;
                    case '\f': if (!s_append_string(a_buffer, a_size, a_capacity, "\\f")) return false; break;
                    case '\n': if (!s_append_string(a_buffer, a_size, a_capacity, "\\n")) return false; break;
                    case '\r': if (!s_append_string(a_buffer, a_size, a_capacity, "\\r")) return false; break;
                    case '\t': if (!s_append_string(a_buffer, a_size, a_capacity, "\\t")) return false; break;
                    default: {
                        char l_ch[2] = {*p, '\0'};
                        if (!s_append_string(a_buffer, a_size, a_capacity, l_ch)) return false;
                    }
                }
            }
            
            return s_append_string(a_buffer, a_size, a_capacity, "\"");
        }
            
        case DAP_JSON_TYPE_ARRAY: {
            dap_json_array_storage_t *l_storage = (dap_json_array_storage_t*)dap_json_get_storage_ptr(a_value);
            if (!l_storage) return false;
            
            if (!s_append_string(a_buffer, a_size, a_capacity, "[")) return false;
            
            for (size_t i = 0; i < l_storage->count; i++) {
                if (i > 0) {
                    if (!s_append_string(a_buffer, a_size, a_capacity, ",")) return false;
                }
                
                // Check element validity
                if (!l_storage->elements[i]) {
                    log_it(L_ERROR, "NULL element at index %zu", i);
                    return false;
                }
                
                if (!s_serialize_value_recursive(l_storage->elements[i], a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
                    return false;
                }
            }
            
            return s_append_string(a_buffer, a_size, a_capacity, "]");
        }
            
        case DAP_JSON_TYPE_OBJECT: {
            dap_json_object_storage_t *l_storage = (dap_json_object_storage_t*)dap_json_get_storage_ptr(a_value);
            if (!l_storage) {
                log_it(L_ERROR, "Object storage is NULL");
                return false;
            }
            
            if (!s_append_string(a_buffer, a_size, a_capacity, "{")) return false;
            
            for (size_t i = 0; i < l_storage->count; i++) {
                if (i > 0) {
                    if (!s_append_string(a_buffer, a_size, a_capacity, ",")) return false;
                }
                
                // Check key validity
                if (!l_storage->keys[i]) {
                    log_it(L_ERROR, "NULL key at index %zu", i);
                    return false;
                }
                
                // Serialize key
                if (!s_append_string(a_buffer, a_size, a_capacity, "\"")) return false;
                if (!s_append_string(a_buffer, a_size, a_capacity, l_storage->keys[i])) return false;
                if (!s_append_string(a_buffer, a_size, a_capacity, "\":")) return false;
                
                // Serialize value
                if (!s_serialize_value_recursive(l_storage->values[i], a_buffer, a_size, a_capacity, a_indent + 1, a_pretty)) {
                    return false;
                }
            }
            
            return s_append_string(a_buffer, a_size, a_capacity, "}");
        }
            
        default:
            log_it(L_ERROR, "Unknown JSON type: %d", a_value->type);
            return false;
    }
}

char* dap_json_value_serialize(dap_json_value_t *a_value)
{
    if (!a_value) {
        return NULL;
    }
    
    char *l_buffer = DAP_NEW_Z_COUNT(char, 256);
    size_t l_size = 0;
    size_t l_capacity = 256;
    
    if (!s_serialize_value_recursive(a_value, &l_buffer, &l_size, &l_capacity, 0, false)) {
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    return l_buffer;
}

/**
 * @brief Phase 2.0.4: Serialize JSON value to string (NOT YET IMPLEMENTED)
 * @note This function requires context (source_buffer + stage2 for ARENA, or wrapper for MALLOC)
 *       Use dap_json_object_to_string() instead, which works with wrappers.
 */
char* dap_json_value_serialize_pretty(dap_json_value_t *a_value)
{
    if (!a_value) {
        return NULL;
    }
    
    char *l_buffer = DAP_NEW_Z_COUNT(char, 256);
    size_t l_size = 0;
    size_t l_capacity = 256;
    
    if (!s_serialize_value_recursive(a_value, &l_buffer, &l_size, &l_capacity, 0, true)) {
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    return l_buffer;
}

