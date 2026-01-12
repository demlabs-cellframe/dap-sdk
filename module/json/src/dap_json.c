/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025-2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP SDK is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_json.c
 * @brief Native JSON Parser - Public API Adapter
 * @details Adapter layer между старым dap_json API и новым native parser (Stage 1 + Stage 2)
 * 
 * Architecture:
 *   dap_json.h (public API)
 *      ↓
 *   dap_json.c (THIS FILE - adapter)
 *      ↓
 *   Stage 1 (tokenization) + Stage 2 (DOM building)
 * 
 * Key design decisions:
 * - dap_json_t wraps dap_json_value_t from Stage 2
 * - Parse operations use Stage 1 → Stage 2 pipeline
 * - Creation operations directly create dap_json_value_t
 * - Zero json-c dependencies
 */

#include "dap_common.h"
#include "dap_json.h"
#include "dap_json_type.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage2.h"
#include "internal/dap_json_encoding.h"
#include "internal/dap_json_transcode.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <math.h>

#define LOG_TAG "dap_json"

/* Global debug flag for detailed logging (for benchmarks) */
static bool s_debug_more = false;

/**
 * @brief Enable/disable detailed debug logging for ALL JSON parser components
 * @param a_enable true to enable debug logging, false to disable (for benchmarks)
 */
void dap_json_set_debug(bool a_enable)
{
    s_debug_more = a_enable;
}

/**
 * @brief Get current debug logging state
 * @return true if debug logging enabled, false otherwise
 */
bool dap_json_get_debug(void)
{
    return s_debug_more;
}

/* ========================================================================== */
/*                          INTERNAL STRUCTURES                               */
/* ========================================================================== */

/**
 * @brief Wrapper structure for public API
 * @details Wraps dap_json_value_t for backward compatibility with old API
 */
struct dap_json {
    dap_json_value_t *value;         /**< Internal native value */
    dap_json_stage2_t *stage2_parser; /**< Stage 2 parser (owns Arena with all values) */
    int ref_count;                   /**< Reference counter for dap_json_object_ref */
    bool owns_value;                 /**< True if wrapper owns value and should free it */
    struct dap_json *parent;         /**< Parent wrapper for borrowed references (keeps parent alive) */
};

/* ========================================================================== */
/*                          MODULE INITIALIZATION                             */
/* ========================================================================== */

/**
 * @brief Initialize JSON module
 */
void dap_json_init(void)
{
    // Initialize Stage 1 (CPU detection)
    dap_json_stage1_init();
    
    debug_if(s_debug_more, L_NOTICE, "DAP JSON Native Parser initialized (SIMD arch: %s)", 
           dap_cpu_arch_get_name(dap_cpu_arch_get()));
}

/* ========================================================================== */
/*                    ARCHITECTURE SELECTION API                              */
/* ========================================================================== */

/**
 * @brief Set SIMD architecture manually (for testing/benchmarking)
 * @details Wrapper for dap_cpu_arch_set() - affects ALL DAP SDK modules
 */
int dap_json_set_arch(dap_cpu_arch_t a_arch)
{
    return dap_cpu_arch_set(a_arch);
}

/**
 * @brief Get currently selected SIMD architecture
 * @details Wrapper for dap_cpu_arch_get() - global for entire DAP SDK
 */
dap_cpu_arch_t dap_json_get_arch(void)
{
    return dap_cpu_arch_get();
}

/**
 * @brief Get human-readable name of architecture
 * @details Convenience wrapper for dap_cpu_arch_get_name()
 */
const char* dap_json_get_arch_name(dap_cpu_arch_t a_arch)
{
    return dap_cpu_arch_get_name(a_arch);
}

/* ========================================================================== */
/*                          HELPER FUNCTIONS                                  */
/* ========================================================================== */

// Forward declaration
static inline dap_json_t* s_wrap_value_ex(dap_json_value_t *a_value, bool a_owns);

/**
 * @brief Wrap dap_json_value_t into dap_json_t (with ownership)
 */
static inline dap_json_t* s_wrap_value(dap_json_value_t *a_value)
{
    return s_wrap_value_ex(a_value, true);
}

/**
 * @brief Wrap dap_json_value_t into dap_json_t (with optional ownership)
 */
static inline dap_json_t* s_wrap_value_ex(dap_json_value_t *a_value, bool a_owns)
{
    if (!a_value) {
        return NULL;
    }
    
    dap_json_t *l_json = DAP_NEW_Z(dap_json_t);
    if (!l_json) {
        log_it(L_ERROR, "Failed to allocate dap_json_t wrapper");
        return NULL;
    }
    
    l_json->value = a_value;
    l_json->ref_count = 1;
    l_json->owns_value = a_owns;
    l_json->parent = NULL;
    return l_json;
}

/**
 * @brief Wrap value as borrowed reference with parent tracking
 * @param a_value Value to wrap
 * @param a_parent Parent wrapper (will be ref'd to keep it alive)
 * @return Borrowed wrapper that keeps parent alive
 */
static inline dap_json_t* s_wrap_value_borrowed(dap_json_value_t *a_value, dap_json_t *a_parent)
{
    if (!a_value) {
        return NULL;
    }
    
    dap_json_t *l_json = DAP_NEW_Z(dap_json_t);
    if (!l_json) {
        log_it(L_ERROR, "Failed to allocate dap_json_t wrapper");
        return NULL;
    }
    
    l_json->value = a_value;
    l_json->ref_count = 1;
    l_json->owns_value = false; // Borrowed
    
    // Keep parent alive by incrementing its refcount
    if (a_parent) {
        a_parent->ref_count++;
        l_json->parent = a_parent;
    }
    
    return l_json;
}

/**
 * @brief Unwrap dap_json_t to get dap_json_value_t
 */
static inline dap_json_value_t* s_unwrap_value(dap_json_t *a_json)
{
    return a_json ? a_json->value : NULL;
}

/* ========================================================================== */
/*                          PARSING FUNCTIONS                                 */
/* ========================================================================== */

/**
 * @brief Parse JSON from binary buffer with explicit length
 * @details Supports UTF-8, UTF-16LE/BE, UTF-32LE/BE (auto-detection)
 */
dap_json_t* dap_json_parse_buffer(const char *a_json_buffer, size_t a_buffer_len)
{
    if (!a_json_buffer) {
        log_it(L_ERROR, "NULL JSON buffer");
        return NULL;
    }
    
    if (a_buffer_len == 0) {
        log_it(L_ERROR, "Empty JSON buffer");
        return NULL;
    }
    
    // Detect encoding (BOM + heuristics)
    dap_json_encoding_info_t l_enc_info;
    const uint8_t *l_input = (const uint8_t*)a_json_buffer;
    
    if (!dap_json_detect_encoding(l_input, a_buffer_len, &l_enc_info)) {
        log_it(L_ERROR, "Failed to detect encoding");
        return NULL;
    }
    
    // Fast path: UTF-8 (most common, ~99.9% of JSON)
    // Branch prediction hint: UTF-8 is likely
    uint8_t *l_transcoded = NULL;
    const uint8_t *l_parse_input = NULL;
    size_t l_parse_len = 0;
    
    if (__builtin_expect(l_enc_info.encoding == DAP_JSON_ENCODING_UTF8, 1)) {
        // Zero-copy: parse directly
        l_parse_input = l_enc_info.data_start;
        l_parse_len = l_enc_info.data_len;
        debug_if(s_debug_more, L_DEBUG, "UTF-8 fast path (zero-copy)");
    } else {
        // Slow path: transcode UTF-16/32 → UTF-8
        debug_if(s_debug_more, L_DEBUG, "Non-UTF-8 encoding detected: %s (transcoding required)",
               dap_json_encoding_name(l_enc_info.encoding));
        
        if (!dap_json_transcode_to_utf8(l_enc_info.data_start, l_enc_info.data_len,
                                        l_enc_info.encoding,
                                        &l_transcoded, &l_parse_len)) {
            log_it(L_ERROR, "Failed to transcode %s to UTF-8",
                   dap_json_encoding_name(l_enc_info.encoding));
            return NULL;
        }
        
        l_parse_input = l_transcoded;
        debug_if(s_debug_more, L_DEBUG, "Transcoded %s → UTF-8 (%zu bytes)",
               dap_json_encoding_name(l_enc_info.encoding), l_parse_len);
    }
    
    // Stage 1: Tokenization (UTF-8 only)
    dap_json_stage1_t *l_stage1 = dap_json_stage1_create(l_parse_input, l_parse_len);
    if (!l_stage1) {
        log_it(L_ERROR, "Failed to initialize Stage 1");
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    int l_ret = dap_json_stage1_run(l_stage1);
    if (l_ret != STAGE1_SUCCESS) {
        log_it(L_ERROR, "Stage 1 tokenization failed: error %d", l_ret);
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    // Stage 2: DOM Building
    dap_json_stage2_t *l_stage2 = dap_json_stage2_init(l_stage1);
    if (!l_stage2) {
        log_it(L_ERROR, "Failed to initialize Stage 2");
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    dap_json_stage2_error_t l_err = dap_json_stage2_run(l_stage2);
    if (l_err != STAGE2_SUCCESS) {
        log_it(L_ERROR, "Failed to parse JSON: %s", dap_json_stage2_error_to_string(l_err));
        dap_json_stage2_free(l_stage2);
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    // Get root value
    dap_json_value_t *l_root = dap_json_stage2_get_root(l_stage2);
    if (!l_root) {
        log_it(L_ERROR, "Stage 2 returned NULL root");
        dap_json_stage2_free(l_stage2);
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    // Wrap for public API (keep Stage 2 parser alive - it owns the Arena)
    dap_json_t *l_result = s_wrap_value_ex(l_root, true); // owns_value = true
    l_result->stage2_parser = l_stage2; // Keep Stage 2 alive for Arena lifetime
    
    // Cleanup Stage 1 and transcoded buffer (if any)
    dap_json_stage1_free(l_stage1);
    if (l_transcoded) DAP_DELETE(l_transcoded);
    
    return l_result;
}

/**
 * @brief Parse JSON string using native parser (Stage 1 + Stage 2)
 */
dap_json_t* dap_json_parse_string(const char *a_json_string)
{
    if (!a_json_string) {
        log_it(L_ERROR, "NULL JSON string");
        return NULL;
    }
    
    size_t l_len = strlen(a_json_string);
    if (l_len == 0) {
        log_it(L_ERROR, "Empty JSON string");
        return NULL;
    }
    
    return dap_json_parse_buffer(a_json_string, l_len);
}

/* ========================================================================== */
/*                          OBJECT CREATION                                   */
/* ========================================================================== */

/**
 * @brief Create new empty JSON object
 */
dap_json_t* dap_json_object_new(void)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_object();
    return s_wrap_value(l_value);
}

/**
 * @brief Create JSON object from integer
 */
dap_json_t* dap_json_object_new_int(int a_value)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_int((int64_t)a_value);
    return s_wrap_value(l_value);
}

/**
 * @brief Create new JSON integer (64-bit)
 */
dap_json_t* dap_json_object_new_int64(int64_t a_value)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_int(a_value);
    return s_wrap_value(l_value);
}

/**
 * @brief Create new JSON unsigned integer (64-bit)
 */
dap_json_t* dap_json_object_new_uint64(uint64_t a_value)
{
    // Store as int64_t - if value > INT64_MAX, will wrap
    // TODO: proper uint64 support requires extending dap_json_number_t
    dap_json_value_t *l_value = dap_json_value_v2_create_int((int64_t)a_value);
    return s_wrap_value(l_value);
}

/**
 * @brief Create JSON object from string
 */
dap_json_t* dap_json_object_new_string(const char* a_value)
{
    if (!a_value) {
        return NULL;
    }
    return dap_json_object_new_string_len(a_value, (int)strlen(a_value));
}

/**
 * @brief Create JSON object from string with length
 */
dap_json_t* dap_json_object_new_string_len(const char* a_value, int a_len)
{
    if (!a_value || a_len < 0) {
        return NULL;
    }
    dap_json_value_t *l_value = dap_json_value_v2_create_string(a_value, (size_t)a_len);
    return s_wrap_value(l_value);
}

/**
 * @brief Create JSON object from double
 */
dap_json_t* dap_json_object_new_double(double a_value)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_double(a_value);
    return s_wrap_value(l_value);
}

/**
 * @brief Create JSON object from boolean
 */
dap_json_t* dap_json_object_new_bool(bool a_value)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_bool(a_value);
    return s_wrap_value(l_value);
}

/* ========================================================================== */
/*                          OBJECT DESTRUCTION                                */
/* ========================================================================== */

/**
 * @brief Free JSON object
 */
void dap_json_object_free(dap_json_t* a_json)
{
    if (!a_json) {
        return;
    }
    
    // Decrement reference count
    if (a_json->ref_count > 0) {
        a_json->ref_count--;
        if (a_json->ref_count > 0) {
            return; // Still referenced
        }
    }
    
    // CRITICAL FIX: Borrowed references should NOT free stage2_parser/Arena
    // Only the root object (from parsing) should free the stage2_parser/Arena
    // Borrowed references (child elements) just dec-ref the parent and free wrapper
    if (a_json->parent) {
        // This is a borrowed reference from parent (e.g., array element, object value)
        // The value itself lives in parent's Arena, so don't free it
        // Just dec-ref parent and free this wrapper
        dap_json_object_free(a_json->parent);
        a_json->parent = NULL;
        // Free only the wrapper struct, not the value (it's in parent's Arena)
        DAP_DELETE(a_json);
        return;
    }
    
    // This is a root object - proceed with full cleanup
    // Two allocation strategies:
    // 1. Arena-based (from parsing): stage2_parser != NULL
    // 2. Malloc-based (manually created): stage2_parser == NULL
    
    if (a_json->stage2_parser) {
        // Strategy 1: Arena-based - free Stage 2 parser (frees Arena and all values)
        dap_json_stage2_free(a_json->stage2_parser);
        a_json->stage2_parser = NULL;
    } else if (a_json->owns_value && a_json->value) {
        // Strategy 2: Malloc-based - free value recursively
        dap_json_value_v2_free(a_json->value);
    }
    
    // Free wrapper
    DAP_DELETE(a_json);
}

/**
 * @brief Increment reference count
 */
dap_json_t* dap_json_object_ref(dap_json_t* a_json)
{
    if (a_json) {
        a_json->ref_count++;
    }
    return a_json;
}

/* ========================================================================== */
/*                          ARRAY CREATION                                    */
/* ========================================================================== */

/**
 * @brief Create new empty JSON array
 */
dap_json_t* dap_json_array_new(void)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_array();
    return s_wrap_value(l_value);
}

/* ========================================================================== */
/*                          ARRAY MANIPULATION                                */
/* ========================================================================== */

/**
 * @brief Add item to array
 */
int dap_json_array_add(dap_json_t* a_array, dap_json_t* a_item)
{
    if (!a_array || !a_item) {
        log_it(L_ERROR, "NULL array or item");
        return -1;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    dap_json_value_t *l_item = s_unwrap_value(a_item);
    
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        log_it(L_ERROR, "Not an array");
        return -1;
    }
    
    if (!l_item) {
        log_it(L_ERROR, "NULL item value");
        return -1;
    }
    
    return dap_json_array_v2_add(l_array, l_item) ? 0 : -1;
}

/**
 * @brief Get array length
 */
size_t dap_json_array_length(dap_json_t* a_array)
{
    if (!a_array) {
        return 0;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        return 0;
    }
    
    return l_array->array.count;
}

/**
 * @brief Get array element by index
 */
dap_json_t* dap_json_array_get_idx(dap_json_t* a_array, size_t a_idx)
{
    if (!a_array) {
        return NULL;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    
    if (a_idx >= l_array->array.count) {
        return NULL;
    }
    
    dap_json_value_t *l_value = l_array->array.elements[a_idx];
    return s_wrap_value_borrowed(l_value, a_array); // Borrowed reference with parent tracking
}

/**
 * @brief Get string element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return String value or NULL if not found or wrong type
 */
const char* dap_json_array_get_string(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_string(l_elem) : NULL;
}

/**
 * @brief Get int element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Integer value or 0 if not found or wrong type
 */
int dap_json_array_get_int(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_int(l_elem) : 0;
}

/**
 * @brief Get int64 element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Integer value or 0 if not found or wrong type
 */
int64_t dap_json_array_get_int64(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_int64(l_elem) : 0;
}

/**
 * @brief Get double element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Double value or 0.0 if not found or wrong type
 */
double dap_json_array_get_double(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_double(l_elem) : 0.0;
}

/**
 * @brief Get bool element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Boolean value or false if not found or wrong type
 */
bool dap_json_array_get_bool(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_bool(l_elem) : false;
}

/**
 * @brief Get object element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Object or NULL if not found or wrong type
 */
dap_json_t* dap_json_array_get_object(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    if (!l_elem || dap_json_get_type(l_elem) != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    return l_elem;
}

/**
 * @brief Get array element from array by index
 * @param a_array Array object
 * @param a_idx Element index
 * @return Array or NULL if not found or wrong type
 */
dap_json_t* dap_json_array_get_array(dap_json_t* a_array, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    if (!l_elem || dap_json_get_type(l_elem) != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    return l_elem;
}

/* ========================================================================== */
/*                      ARRAY ELEMENT INSERTION (TYPED)                       */
/* ========================================================================== */

/**
 * @brief Internal helper: insert element into array at specified position
 * @param a_array Array to insert into
 * @param a_idx Index where to insert (shifts existing elements right)
 * @param a_elem Element to insert
 * @return 0 on success, -1 on error
 */
static int s_array_insert_at(dap_json_t* a_array, size_t a_idx, dap_json_t* a_elem)
{
    if (!a_array || !a_elem) {
        log_it(L_ERROR, "NULL array or element");
        return -1;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    dap_json_value_t *l_elem = s_unwrap_value(a_elem);
    
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        log_it(L_ERROR, "Not an array");
        return -1;
    }
    
    if (!l_elem) {
        log_it(L_ERROR, "NULL element value");
        return -1;
    }
    
    size_t l_count = l_array->array.count;
    
    // If index is at or past the end, just append
    if (a_idx >= l_count) {
        return dap_json_array_v2_add(l_array, l_elem) ? 0 : -1;
    }
    
    // Grow array if needed
    if (l_count >= l_array->array.capacity) {
        size_t l_new_capacity = l_array->array.capacity * 2;
        if (l_new_capacity < 8) {
            l_new_capacity = 8;
        }
        
        dap_json_value_t **l_new_elements = DAP_REALLOC(
            l_array->array.elements,
            l_new_capacity * sizeof(dap_json_value_t*)
        );
        
        if (!l_new_elements) {
            log_it(L_ERROR, "Failed to grow array to %zu elements", l_new_capacity);
            return -1;
        }
        
        l_array->array.elements = l_new_elements;
        l_array->array.capacity = l_new_capacity;
    }
    
    // Shift elements right from insertion point
    memmove(&l_array->array.elements[a_idx + 1],
            &l_array->array.elements[a_idx],
            (l_count - a_idx) * sizeof(dap_json_value_t*));
    
    // Insert new element
    l_array->array.elements[a_idx] = l_elem;
    l_array->array.count++;
    
    return 0;
}

/**
 * @brief Insert string element into array at specified index
 */
int dap_json_array_insert_string(dap_json_t* a_array, size_t a_idx, const char* a_value)
{
    dap_json_t *l_elem = dap_json_object_new_string(a_value);
    if (!l_elem) {
        return -1;
    }
    
    int result = s_array_insert_at(a_array, a_idx, l_elem);
    if (result != 0) {
        dap_json_object_free(l_elem);
    }
    return result;
}

/**
 * @brief Insert int element into array at specified index
 */
int dap_json_array_insert_int(dap_json_t* a_array, size_t a_idx, int a_value)
{
    dap_json_t *l_elem = dap_json_object_new_int(a_value);
    if (!l_elem) {
        return -1;
    }
    
    int result = s_array_insert_at(a_array, a_idx, l_elem);
    if (result != 0) {
        dap_json_object_free(l_elem);
    }
    return result;
}

/**
 * @brief Insert double element into array at specified index
 */
int dap_json_array_insert_double(dap_json_t* a_array, size_t a_idx, double a_value)
{
    dap_json_t *l_elem = dap_json_object_new_double(a_value);
    if (!l_elem) {
        return -1;
    }
    
    int result = s_array_insert_at(a_array, a_idx, l_elem);
    if (result != 0) {
        dap_json_object_free(l_elem);
    }
    return result;
}

/**
 * @brief Insert bool element into array at specified index
 */
int dap_json_array_insert_bool(dap_json_t* a_array, size_t a_idx, bool a_value)
{
    dap_json_t *l_elem = dap_json_object_new_bool(a_value);
    if (!l_elem) {
        return -1;
    }
    
    int result = s_array_insert_at(a_array, a_idx, l_elem);
    if (result != 0) {
        dap_json_object_free(l_elem);
    }
    return result;
}

/**
 * @brief Insert object element into array at specified index
 */
int dap_json_array_insert_object(dap_json_t* a_array, size_t a_idx, dap_json_t* a_object)
{
    return s_array_insert_at(a_array, a_idx, a_object);
}

/**
 * @brief Insert array element into array at specified index
 */
int dap_json_array_insert_array(dap_json_t* a_array, size_t a_idx, dap_json_t* a_inner_array)
{
    return s_array_insert_at(a_array, a_idx, a_inner_array);
}



/**
 * @brief Delete array elements
 */
int dap_json_array_del_idx(dap_json_t* a_array, size_t a_idx, size_t a_count)
{
    if (!a_array) {
        return -1;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        return -1;
    }
    
    if (a_idx >= l_array->array.count || a_count == 0) {
        return -1;
    }
    
    // Limit count to available elements
    if (a_idx + a_count > l_array->array.count) {
        a_count = l_array->array.count - a_idx;
    }
    
    // Free elements
    for (size_t i = 0; i < a_count; i++) {
        dap_json_value_v2_free(l_array->array.elements[a_idx + i]);
    }
    
    // Shift remaining elements
    if (a_idx + a_count < l_array->array.count) {
        memmove(&l_array->array.elements[a_idx],
                &l_array->array.elements[a_idx + a_count],
                (l_array->array.count - a_idx - a_count) * sizeof(dap_json_value_t*));
    }
    
    l_array->array.count -= a_count;
    
    return 0;
}

/**
 * @brief Sort array
 */
void dap_json_array_sort(dap_json_t* a_array, dap_json_sort_fn_t a_sort_fn)
{
    if (!a_array || !a_sort_fn) {
        return;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    if (!l_array || l_array->type != DAP_JSON_TYPE_ARRAY) {
        return;
    }
    
    // TODO: Implement sorting using a_sort_fn
    // For now, not implemented
    log_it(L_WARNING, "dap_json_array_sort not yet implemented in native parser");
}

/* ========================================================================== */
/*                          OBJECT FIELD ADDITION                             */
/* ========================================================================== */

/**
 * @brief Add string field to object
 */
int dap_json_object_add_string(dap_json_t* a_json, const char* a_key, const char* a_value)
{
    if (!a_value) {
        return dap_json_object_add_null(a_json, a_key);
    }
    return dap_json_object_add_string_len(a_json, a_key, a_value, (int)strlen(a_value));
}

/**
 * @brief Add string field with length to object
 */
int dap_json_object_add_string_len(dap_json_t* a_json, const char* a_key, const char* a_value, const int a_len)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    if (a_len < 0) {
        log_it(L_ERROR, "Invalid length: %d", a_len);
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = a_value ? 
        dap_json_value_v2_create_string(a_value, (size_t)a_len) :
        dap_json_value_v2_create_null();
    
    if (!l_value) {
        log_it(L_ERROR, "Failed to create string value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add integer field to object
 */
int dap_json_object_add_int(dap_json_t* a_json, const char* a_key, int a_value)
{
    return dap_json_object_add_int64(a_json, a_key, (int64_t)a_value);
}

/**
 * @brief Add int64 field to object
 */
int dap_json_object_add_int64(dap_json_t* a_json, const char* a_key, int64_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = dap_json_value_v2_create_int(a_value);
    if (!l_value) {
        log_it(L_ERROR, "Failed to create int value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add uint64 field to object
 */
int dap_json_object_add_uint64(dap_json_t* a_json, const char* a_key, uint64_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate value");
        return -1;
    }
    
    // Store as int64 if fits (no cast needed for get operations)
    if (a_value <= INT64_MAX) {
        l_value->type = DAP_JSON_TYPE_INT;
        l_value->number.i = (int64_t)a_value;
        l_value->number.is_double = false;
    } else {
        // uint64 > INT64_MAX: use native uint64 type
        l_value->type = DAP_JSON_TYPE_UINT64;
        l_value->number.u64 = a_value;
        l_value->number.is_double = false;
    }
    
    if (!dap_json_object_v2_add(l_obj, a_key, l_value)) {
        dap_json_value_v2_free(l_value);
        log_it(L_ERROR, "Failed to add value to object");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Add uint256 field to object
 */
int dap_json_object_add_uint256(dap_json_t* a_json, const char* a_key, uint256_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate value");
        return -1;
    }
    
    l_value->type = DAP_JSON_TYPE_UINT256;
    l_value->number.u256 = a_value;
    l_value->number.is_double = false;
    
    if (!dap_json_object_v2_add(l_obj, a_key, l_value)) {
        dap_json_value_v2_free(l_value);
        log_it(L_ERROR, "Failed to add value to object");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Add double field to object
 */
int dap_json_object_add_double(dap_json_t* a_json, const char* a_key, double a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = dap_json_value_v2_create_double(a_value);
    if (!l_value) {
        log_it(L_ERROR, "Failed to create double value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add boolean field to object
 */
int dap_json_object_add_bool(dap_json_t* a_json, const char* a_key, bool a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = dap_json_value_v2_create_bool(a_value);
    if (!l_value) {
        log_it(L_ERROR, "Failed to create bool value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add nanotime field to object
 */
int dap_json_object_add_nanotime(dap_json_t* a_json, const char* a_key, dap_nanotime_t a_value)
{
    // Convert nanotime to uint64 and store
    return dap_json_object_add_uint64(a_json, a_key, (uint64_t)a_value);
}

/**
 * @brief Add time field to object
 */
int dap_json_object_add_time(dap_json_t* a_json, const char* a_key, dap_time_t a_value)
{
    // Convert time_t to int64 and store
    return dap_json_object_add_int64(a_json, a_key, (int64_t)a_value);
}

/**
 * @brief Add null field to object
 */
int dap_json_object_add_null(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "NULL object or key");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    dap_json_value_t *l_value = dap_json_value_v2_create_null();
    if (!l_value) {
        log_it(L_ERROR, "Failed to create null value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add object field to object
 */
int dap_json_object_add_object(dap_json_t* a_json, const char* a_key, dap_json_t* a_value)
{
    if (!a_json || !a_key || !a_value) {
        log_it(L_ERROR, "NULL object, key or value");
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    dap_json_value_t *l_value = s_unwrap_value(a_value);
    
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Not an object");
        return -1;
    }
    
    if (!l_value) {
        log_it(L_ERROR, "NULL value");
        return -1;
    }
    
    return dap_json_object_v2_add(l_obj, a_key, l_value) ? 0 : -1;
}

/**
 * @brief Add array field to object
 */
int dap_json_object_add_array(dap_json_t* a_json, const char* a_key, dap_json_t* a_array)
{
    return dap_json_object_add_object(a_json, a_key, a_array);
}

/* ========================================================================== */
/*                      OBJECT FIELD MODIFICATION (SET)                       */
/* ========================================================================== */

/**
 * @brief Set/update string field in object (replaces existing value)
 */
int dap_json_object_set_string(dap_json_t* a_json, const char* a_key, const char* a_value)
{
    // Delete existing key first, then add new value
    dap_json_object_del(a_json, a_key);
    return dap_json_object_add_string(a_json, a_key, a_value);
}

/**
 * @brief Set/update int field in object (replaces existing value)
 */
int dap_json_object_set_int(dap_json_t* a_json, const char* a_key, int a_value)
{
    dap_json_object_del(a_json, a_key);
    return dap_json_object_add_int(a_json, a_key, a_value);
}

/**
 * @brief Set/update double field in object (replaces existing value)
 */
int dap_json_object_set_double(dap_json_t* a_json, const char* a_key, double a_value)
{
    dap_json_object_del(a_json, a_key);
    return dap_json_object_add_double(a_json, a_key, a_value);
}

/**
 * @brief Set/update bool field in object (replaces existing value)
 */
int dap_json_object_set_bool(dap_json_t* a_json, const char* a_key, bool a_value)
{
    dap_json_object_del(a_json, a_key);
    return dap_json_object_add_bool(a_json, a_key, a_value);
}


/* ========================================================================== */
/*                          OBJECT FIELD ACCESS                               */
/* ========================================================================== */

/**
 * @brief Get string field from object
 */
const char* dap_json_object_get_string(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value || l_value->type != DAP_JSON_TYPE_STRING) {
        return NULL;
    }
    
    return l_value->string.data;
}

/**
 * @brief Get integer field from object
 */
int dap_json_object_get_int(dap_json_t* a_json, const char* a_key)
{
    return (int)dap_json_object_get_int64(a_json, a_key);
}

/**
 * @brief Get int64 field from object
 */
int64_t dap_json_object_get_int64(dap_json_t* a_json, const char* a_key)
{
    int64_t l_result = 0;
    dap_json_object_get_int64_ext(a_json, a_key, &l_result);
    return l_result;
}

/**
 * @brief Get uint64 field from object
 */
uint64_t dap_json_object_get_uint64(dap_json_t* a_json, const char* a_key)
{
    uint64_t l_result = 0;
    dap_json_object_get_uint64_ext(a_json, a_key, &l_result);
    return l_result;
}

/**
 * @brief Get int64 field with error checking
 */
bool dap_json_object_get_int64_ext(dap_json_t* a_json, const char* a_key, int64_t *a_out)
{
    if (!a_json || !a_key || !a_out) {
        return false;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return false;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value) {
        return false;
    }
    
    if (l_value->type == DAP_JSON_TYPE_INT) {
        *a_out = l_value->number.i;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        *a_out = (int64_t)l_value->number.d;
        return true;
    }
    
    return false;
}

/**
 * @brief Get uint64 field with error checking
 */
bool dap_json_object_get_uint64_ext(dap_json_t* a_json, const char* a_key, uint64_t *a_out)
{
    if (!a_json || !a_key || !a_out) {
        return false;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return false;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value) {
        return false;
    }
    
    if (l_value->type == DAP_JSON_TYPE_INT) {
        *a_out = (uint64_t)l_value->number.i;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_UINT64) {
        *a_out = l_value->number.u64;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_UINT128) {
        // Truncate to lower 64 bits
        *a_out = (uint64_t)l_value->number.u128;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_UINT256) {
        // Truncate to lower 64 bits
        *a_out = (uint64_t)l_value->number.u256.lo;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        *a_out = (uint64_t)l_value->number.d;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Parse string as uint64 (for legacy/compatibility)
        char *l_endptr = NULL;
        errno = 0;
        unsigned long long l_val = strtoull(l_value->string.data, &l_endptr, 10);
        
        if (errno == 0 && l_endptr != l_value->string.data && *l_endptr == '\0') {
            *a_out = (uint64_t)l_val;
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Get uint256 field from object
 */
/**
 * @brief Get uint256 field from object
 * @return 0 on success, -1 on failure
 */
int dap_json_object_get_uint256(dap_json_t* a_json, const char* a_key, uint256_t *a_out)
{
    if (!a_json || !a_key || !a_out) {
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return -1;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value) {
        return -1;
    }
    
    if (l_value->type == DAP_JSON_TYPE_UINT256) {
        *a_out = l_value->number.u256;
        return 0;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Fallback: parse hex string (for compatibility)
        *a_out = dap_uint256_scan_uninteger(l_value->string.data);
        return 0;
    }
    
    return -1;
}

/**
 * @brief Get double field from object
 */
double dap_json_object_get_double(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return 0.0;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return 0.0;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value) {
        return 0.0;
    }
    
    if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        return l_value->number.d;
    } else if (l_value->type == DAP_JSON_TYPE_INT) {
        return (double)l_value->number.i;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Check for special string values: "Infinity", "-Infinity", "NaN"
        const char *l_str = l_value->string.data;
        if (strcmp(l_str, "Infinity") == 0) {
            return INFINITY;
        } else if (strcmp(l_str, "-Infinity") == 0) {
            return -INFINITY;
        } else if (strcmp(l_str, "NaN") == 0) {
            return NAN;
        }
    }
    
    return 0.0;
}

/**
 * @brief Get boolean field from object
 */
bool dap_json_object_get_bool(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return false;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return false;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value || l_value->type != DAP_JSON_TYPE_BOOLEAN) {
        return false;
    }
    
    return l_value->boolean;
}

/**
 * @brief Get nanotime field from object
 */
dap_nanotime_t dap_json_object_get_nanotime(dap_json_t* a_json, const char* a_key)
{
    return (dap_nanotime_t)dap_json_object_get_uint64(a_json, a_key);
}

/**
 * @brief Get time field from object
 */
dap_time_t dap_json_object_get_time(dap_json_t* a_json, const char* a_key)
{
    return (dap_time_t)dap_json_object_get_int64(a_json, a_key);
}

/**
 * @brief Get object field from object
 */
dap_json_t* dap_json_object_get_object(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value || l_value->type != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    
    return s_wrap_value_borrowed(l_value, a_json); // Borrowed reference with parent tracking
}

/**
 * @brief Get array field from object
 */
dap_json_t* dap_json_object_get_array(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    
    dap_json_value_t *l_value = dap_json_object_v2_get(l_obj, a_key);
    if (!l_value || l_value->type != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    
    return s_wrap_value_borrowed(l_value, a_json); // Borrowed reference with parent tracking
}

/**
 * @brief Get object field with exists check
 * @return true if key exists and value retrieved, false otherwise
 */
bool dap_json_object_get_ex(dap_json_t* a_json, const char* a_key, dap_json_t** a_value)
{
    if (!a_json || !a_key || !a_value) {
        return false;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return false;
    }
    
    dap_json_value_t *l_val = dap_json_object_v2_get(l_obj, a_key);
    if (!l_val) {
        return false;
    }
    
    *a_value = s_wrap_value_borrowed(l_val, a_json); // Borrowed reference with parent tracking
    return true;
}

/**
 * @brief Delete key from object
 * @return 0 on success, -1 on failure
 * @note For Arena-based objects (from parsing), this only removes the key from the lookup
 *       but doesn't free memory (Arena handles that). For malloc-based objects (manually created),
 *       memory is freed.
 */
int dap_json_object_del(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return -1;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return -1;
    }
    
    // Find key
    for (size_t i = 0; i < l_obj->object.count; i++) {
        if (strcmp(l_obj->object.pairs[i].key, a_key) == 0) {
            // For malloc-based objects only, free value and key
            // Arena-based objects don't need individual freeing
            if (!a_json->stage2_parser) {
                // This is malloc-based, can free
                // But dap_json_value_v2_free_malloc is internal, so just mark as deleted
                // by shifting without freeing (memory leak, but safer than crash)
                // TODO: expose proper free function or implement reference counting
            }
            
            // Shift remaining pairs to remove the key
            for (size_t j = i; j < l_obj->object.count - 1; j++) {
                l_obj->object.pairs[j] = l_obj->object.pairs[j + 1];
            }
            l_obj->object.count--;
            return 0;
        }
    }
    
    return -1; // Key not found
}

/* ========================================================================== */
/*                          TYPE CHECKING                                     */
/* ========================================================================== */

/**
 * @brief Check if object has a key
 */
bool dap_json_object_has_key(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return false;
    }
    
    dap_json_value_t *l_obj = s_unwrap_value(a_json);
    if (!l_obj || l_obj->type != DAP_JSON_TYPE_OBJECT) {
        return false;
    }
    
    return dap_json_object_v2_get(l_obj, a_key) != NULL;
}

/**
 * @brief Check if value is array
 */
bool dap_json_is_array(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_ARRAY;
}

/**
 * @brief Check if value is object
 */
bool dap_json_is_object(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_OBJECT;
}

/**
 * @brief Check if JSON value is an integer
 */
bool dap_json_is_int(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_INT;
}

/**
 * @brief Check if JSON value is a string
 */
bool dap_json_is_string(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_STRING;
}

/**
 * @brief Check if JSON value is a double
 */
bool dap_json_is_double(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_DOUBLE;
}

/**
 * @brief Check if JSON value is a boolean
 */
bool dap_json_is_bool(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_BOOLEAN;
}

/**
 * @brief Check if JSON value is null
 */
bool dap_json_is_null(dap_json_t* a_json)
{
    if (!a_json) {
        return true; // NULL pointer treated as JSON null
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return l_value && l_value->type == DAP_JSON_TYPE_NULL;
}

/**
 * @brief Get string value from JSON
 * @return String value or NULL if not a string
 */
const char* dap_json_get_string(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value || l_value->type != DAP_JSON_TYPE_STRING) {
        return NULL;
    }
    
    return l_value->string.data;
}

/**
 * @brief Get int64 value from JSON
 * @return Integer value or 0 if not an integer
 */
int64_t dap_json_get_int64(dap_json_t* a_json)
{
    if (!a_json) {
        return 0;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value) {
        return 0;
    }
    
    if (l_value->type == DAP_JSON_TYPE_INT) {
        return l_value->number.i;
    }
    
    if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        return (int64_t)l_value->number.d;
    }
    
    return 0;
}

/**
 * @brief Get int value (32-bit wrapper for int64)
 */
int dap_json_get_int(dap_json_t* a_json)
{
    return (int)dap_json_get_int64(a_json);
}

/**
 * @brief Get boolean value from JSON
 * @return Boolean value or false if not a boolean
 */
bool dap_json_get_bool(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value || l_value->type != DAP_JSON_TYPE_BOOLEAN) {
        return false;
    }
    
    return l_value->boolean;
}

/**
 * @brief Get double value from JSON
 * @return Double value or 0.0 if not a number
 */
double dap_json_get_double(dap_json_t* a_json)
{
    if (!a_json) {
        return 0.0;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value) {
        return 0.0;
    }
    
    if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        return l_value->number.d;
    }
    
    if (l_value->type == DAP_JSON_TYPE_INT) {
        return (double)l_value->number.i;
    }
    
    return 0.0;
}

/**
 * @brief Get type of JSON value
 * @return Type enum value
 */
dap_json_type_t dap_json_get_type(dap_json_t* a_json)
{
    if (!a_json) {
        return DAP_JSON_TYPE_NULL;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value) {
        return DAP_JSON_TYPE_NULL;
    }
    
    return l_value->type;
}

/**
 * @brief Iterate over object key-value pairs
 * @param a_json JSON object
 * @param callback Callback function for each key-value pair
 * @param user_data User data passed to callback
 */
void dap_json_object_foreach(dap_json_t* a_json, dap_json_object_foreach_callback_t callback, void* user_data)
{
    if (!a_json || !callback) {
        return;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value || l_value->type != DAP_JSON_TYPE_OBJECT) {
        return;
    }
    
    // Iterate over all key-value pairs
    for (size_t i = 0; i < l_value->object.count; i++) {
        const char *l_key = l_value->object.pairs[i].key;
        dap_json_value_t *l_pair_value = l_value->object.pairs[i].value;
        
        // Wrap value for callback
        dap_json_t *l_wrapped = s_wrap_value(l_pair_value); // Borrowed reference
        if (l_wrapped) {
            callback(l_key, l_wrapped, user_data);
            // Don't free wrapped value - it's borrowed
        }
    }
}

/* ========================================================================== */
/* ========================================================================== */
/*                          JSON SERIALIZATION                                */
/* ========================================================================== */

#include "internal/dap_json_serialization.h"

/**
 * @brief Convert JSON object to string (compact format)
 */
char* dap_json_to_string(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return dap_json_value_serialize(l_value);
}

/**
 * @brief Convert JSON to pretty-printed string
 */
char* dap_json_to_string_pretty(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    return dap_json_value_serialize_pretty(l_value);
}

/**
 * @brief Parse JSON from file
 */
dap_json_t* dap_json_from_file(const char* a_file_path)
{
    if (!a_file_path) {
        log_it(L_ERROR, "NULL file path provided");
        return NULL;
    }
    
    // Read file content
    FILE *l_file = fopen(a_file_path, "r");
    if (!l_file) {
        log_it(L_ERROR, "Failed to open file: %s", a_file_path);
        return NULL;
    }
    
    // Get file size
    fseek(l_file, 0, SEEK_END);
    long l_file_size = ftell(l_file);
    fseek(l_file, 0, SEEK_SET);
    
    if (l_file_size <= 0 || l_file_size > (100 * 1024 * 1024)) { // 100MB limit
        log_it(L_ERROR, "Invalid file size: %ld", l_file_size);
        fclose(l_file);
        return NULL;
    }
    
    // Allocate buffer
    char *l_buffer = DAP_NEW_Z_SIZE(char, l_file_size + 1);
    if (!l_buffer) {
        log_it(L_ERROR, "Failed to allocate buffer for file");
        fclose(l_file);
        return NULL;
    }
    
    // Read file
    size_t l_read = fread(l_buffer, 1, l_file_size, l_file);
    fclose(l_file);
    
    if (l_read != (size_t)l_file_size) {
        log_it(L_ERROR, "Failed to read complete file");
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    l_buffer[l_file_size] = '\0';
    
    // Parse JSON
    dap_json_t *l_result = dap_json_parse_string(l_buffer);
    DAP_DELETE(l_buffer);
    
    return l_result;
}

/**
 * @brief Print JSON object to stream (debug)
 */
void dap_json_print_object(dap_json_t *a_json, FILE *a_stream, int a_indent_level)
{
    if (!a_json || !a_stream) {
        return;
    }
    
    // Simple debug print - TODO: improve formatting
    char *l_str = dap_json_to_string(a_json);
    if (l_str) {
        fprintf(a_stream, "%*s%s\n", a_indent_level * 2, "", l_str);
        // Note: dap_json_to_string returns malloc'd string from json-c, don't free here
    } else {
        fprintf(a_stream, "%*s<JSON stringify not implemented>\n", a_indent_level * 2, "");
    }
}

/* ========================================================================== */
/*                          LEGACY JSON-C API STUBS                           */
/* ========================================================================== */

/**
 * @brief Legacy json-c API: parse with verbose error reporting
 */
dap_json_t* dap_json_tokener_parse_verbose(const char *a_str, dap_json_tokener_error_t *a_error)
{
    dap_json_t *l_result = dap_json_parse_string(a_str);
    
    if (a_error) {
        *a_error = l_result ? DAP_JSON_TOKENER_SUCCESS : DAP_JSON_TOKENER_ERROR_PARSE_EOF;
    }
    
    return l_result;
}

/**
 * @brief Legacy json-c API: get error description
 */
const char* dap_json_tokener_error_desc(dap_json_tokener_error_t a_error)
{
    switch (a_error) {
        case DAP_JSON_TOKENER_SUCCESS: return "success";
        case DAP_JSON_TOKENER_ERROR_PARSE_EOF: return "unexpected end of data";
        case DAP_JSON_TOKENER_ERROR_PARSE_UNEXPECTED: return "unexpected character";
        case DAP_JSON_TOKENER_ERROR_PARSE_NULL: return "error parsing null";
        case DAP_JSON_TOKENER_ERROR_PARSE_BOOLEAN: return "error parsing boolean";
        case DAP_JSON_TOKENER_ERROR_PARSE_NUMBER: return "error parsing number";
        case DAP_JSON_TOKENER_ERROR_PARSE_ARRAY: return "error parsing array";
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_NAME: return "error parsing object key";
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_SEP: return "error parsing object key separator";
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_VALUE_SEP: return "error parsing object value separator";
        case DAP_JSON_TOKENER_ERROR_PARSE_STRING: return "error parsing string";
        case DAP_JSON_TOKENER_ERROR_PARSE_COMMENT: return "error parsing comment";
        default: return "unknown error";
    }
}
