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
#include "dap_json_iterator.h"
#include "dap_arena.h"
#include "internal/dap_json_internal.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage2.h"
#include "internal/dap_json_tape.h"
#include "internal/dap_json_encoding.h"
#include "internal/dap_json_float.h"
#include "internal/dap_json_transcode.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>

#define LOG_TAG "dap_json"

/* Global debug flag for detailed logging (for benchmarks) */
static bool s_debug_more = false;

/* Thread-local static arena for parsed JSON values */
/**
 * @brief Thread-local arena list for multiple parsed JSON objects
 * @details Each parse creates a new arena. Old arenas are kept alive until:
 *          1. User explicitly frees all JSON objects from that arena
 *          2. User calls dap_json_cleanup_thread()
 *          3. Thread exits
 */
typedef struct dap_json_arena_node {
    dap_json_stage2_t *stage2;
    struct dap_json_arena_node *next;
} dap_json_arena_node_t;

static _Thread_local dap_json_arena_node_t *s_arena_list = NULL;

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
 * @brief  Internal storage for mutable arrays
 * @details Used only in MUTABLE mode. Immutable arrays use tape indices.
 *          This is a PRIVATE structure - never exposed in public API.
 */
typedef struct {
    dap_json_value_t **elements;  /**< Array of pointers to 8-byte values */
    size_t count;                  /**< Current number of elements */
    size_t capacity;               /**< Allocated capacity */
} dap_json_array_storage_t;

/**
 * @brief  Internal storage for mutable objects
 * @details Used only in MUTABLE mode. Immutable objects use tape key-value pairs.
 *          This is a PRIVATE structure - never exposed in public API.
 */
typedef struct {
    char **keys;                   /**< Array of key strings (malloc'd) */
    dap_json_value_t **values;     /**< Array of pointers to 8-byte values */
    size_t count;                  /**< Current number of pairs */
    size_t capacity;               /**< Allocated capacity */
} dap_json_object_storage_t;

// struct dap_json is defined in internal/dap_json_internal.h

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
    
    // Initialize float parsing (power-of-5 table generation)
    dap_json_float_init();
    
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
    if (!a_value) {
        return NULL;
    }
    
    dap_json_t *l_json = DAP_NEW_Z(dap_json_t);
    if (!l_json) {
        log_it(L_ERROR, "Failed to allocate dap_json_t wrapper");
        return NULL;
    }
    
    l_json->ref_count = 1;
    l_json->mode = DAP_JSON_MODE_MUTABLE;
    l_json->mode_data.mutable.value = a_value;
    l_json->mode_data.mutable.stage2 = NULL;
    
    return l_json;
}

/**
 * @brief Create borrowed reference (NOT IMPLEMENTED for hybrid mode yet)
 * @note Will be implemented when DOM mode is fully restored
 */
static inline dap_json_t* s_wrap_value_borrowed(dap_json_value_t *a_value, dap_json_t *a_parent)
{
    (void)a_value;
    (void)a_parent;
    log_it(L_ERROR, "Borrowed references not yet implemented in hybrid mode");
    return NULL;
}

/**
 * @brief Unwrap dap_json_t to get dap_json_value_t
 */
static inline dap_json_value_t* s_unwrap_value(dap_json_t *a_json)
{
    return a_json && a_json->mode == DAP_JSON_MODE_MUTABLE ? a_json->mode_data.mutable.value : NULL;
}

/**
 * @brief Get stage2 from wrapper
 * @details stage2 is stored only in MUTABLE mode
 */
static inline dap_json_stage2_t* s_get_stage2(dap_json_t *a_json)
{
    if (!a_json || a_json->mode != DAP_JSON_MODE_MUTABLE) {
        return NULL;
    }
    
    return (dap_json_stage2_t*)a_json->mode_data.mutable.stage2;
}

/**
 * @brief  Get array storage from value
 * @details Works ONLY for MALLOC_MUTABLE arrays
 */
static inline dap_json_array_storage_t* s_get_array_storage(dap_json_value_t *a_value)
{
    if (!a_value || a_value->type != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    return (dap_json_array_storage_t*)dap_json_get_storage_ptr(a_value);
}

/**
 * @brief  Get object storage from value
 * @details Works ONLY for MUTABLE objects
 */
static inline dap_json_object_storage_t* s_get_object_storage(dap_json_value_t *a_value)
{
    if (!a_value || a_value->type != DAP_JSON_TYPE_OBJECT) {
        return NULL;
    }
    return (dap_json_object_storage_t*)dap_json_get_storage_ptr(a_value);
}

/**
 * @brief Get type from value (wrapper for value->type)
 */
static inline dap_json_type_t dap_json_get_type_value(const dap_json_value_t *a_value)
{
    return a_value ? a_value->type : DAP_JSON_TYPE_NULL;
}

/**
 * @brief Get array length from value
 */
static inline size_t dap_json_get_array_len(const dap_json_value_t *a_value)
{
    if (!a_value || a_value->type != DAP_JSON_TYPE_ARRAY) {
        return 0;
    }
    return a_value->length;
}

/**
 * @brief Get string from value (for MUTABLE mode)
 * @note Returns pointer to null-terminated string
 */
static inline const char* dap_json_get_string_value(const dap_json_value_t *a_value)
{
    if (!a_value || a_value->type != DAP_JSON_TYPE_STRING) {
        return NULL;
    }
    // In MUTABLE mode, strings are stored as malloc'd pointers
    return (const char*)(uintptr_t)a_value->offset;
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
    
    // Build tape from Stage 1 output
    dap_json_tape_entry_t *l_tape = NULL;
    size_t l_tape_count = 0;
    
    if (!dap_json_build_tape(l_stage1, &l_tape, &l_tape_count)) {
        log_it(L_ERROR, "Failed to build tape");
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    // Create wrapper with tape (NO DOM!)
    dap_json_t *l_result = DAP_NEW_Z(dap_json_t);
    if (!l_result) {
        log_it(L_ERROR, "Failed to allocate JSON wrapper");
        dap_json_stage1_free(l_stage1);
        if (l_transcoded) DAP_DELETE(l_transcoded);
        return NULL;
    }
    
    l_result->ref_count = 1;
    l_result->mode = DAP_JSON_MODE_IMMUTABLE;
    l_result->mode_data.immutable.input_buffer = (const char*)l_parse_input;
    l_result->mode_data.immutable.input_len = l_parse_len;
    l_result->mode_data.immutable.tape = l_tape;
    l_result->mode_data.immutable.tape_count = l_tape_count;
    
    // Tape and input_buffer managed by arenas
    
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
 * @note Values > INT64_MAX will be represented as negative numbers
 * This is a known limitation of the current stage2 implementation
 */
dap_json_t* dap_json_object_new_uint64(uint64_t a_value)
{
    // Store as int64_t - if value > INT64_MAX, will wrap to negative
    // This matches JSON spec behavior (numbers are always signed in JSON)
    if (a_value > INT64_MAX) {
        log_it(L_WARNING, "uint64 value %lu > INT64_MAX, will be stored as negative", a_value);
    }
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
    
    // For IMMUTABLE mode (tape): arena is NOT reset here!
    // Reason: Other parsed objects may still use the same arena
    // Arena grows naturally and is reset manually via dap_json_tape_arena_reset()
    // or freed at thread exit via dap_json_cleanup_thread_arena()
    
    // For MUTABLE mode (DOM), free the value
    if (a_json->mode == DAP_JSON_MODE_MUTABLE && a_json->mode_data.mutable.value) {
        dap_json_value_v2_free(a_json->mode_data.mutable.value);
        
        // Free stage2 if present
        if (a_json->mode_data.mutable.stage2) {
            dap_json_stage2_free((dap_json_stage2_t*)a_json->mode_data.mutable.stage2);
        }
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

/**
 * @brief Clean up all thread-local arenas
 * @details Call this at the end of thread to free ALL parsed JSON values
 * This frees all arenas in the thread-local list
 */
void dap_json_cleanup_thread_arena(void)
{
    while (s_arena_list) {
        dap_json_arena_node_t *l_next = s_arena_list->next;
        dap_json_stage2_free(s_arena_list->stage2);
        DAP_DELETE(s_arena_list);
        s_arena_list = l_next;
    }
}

/* ========================================================================== */
/*                          MODE QUERIES                                      */
/* ========================================================================== */

/**
 * @brief Check if JSON is mutable (malloc-based)
 */
bool dap_json_is_mutable(const dap_json_t* a_json)
{
    return a_json && a_json->mode == DAP_JSON_MODE_MUTABLE;
}

/**
 * @brief Check if JSON is immutable (arena-based)
 */
bool dap_json_is_immutable(const dap_json_t* a_json)
{
    return a_json && a_json->mode == DAP_JSON_MODE_IMMUTABLE;
}

/**
 * @brief Get JSON operation mode
 */
dap_json_mode_t dap_json_get_mode(const dap_json_t* a_json)
{
    return a_json ? a_json->mode : DAP_JSON_MODE_MUTABLE; // Default to mutable if NULL
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
 * @details Works for both modes:
 *          - ARENA_IMMUTABLE: count elements in tape
 *          - MALLOC_MUTABLE: use DOM storage
 */
size_t dap_json_array_length(dap_json_t* a_array)
{
    if (!a_array) {
        return 0;
    }
    
    if (a_array->mode == DAP_JSON_MODE_IMMUTABLE) {
        // Tape mode
        if (!a_array->mode_data.immutable.tape || a_array->mode_data.immutable.tape_count == 0) {
            return 0;
        }
        
        // Check if root is array
        uint8_t l_type = dap_tape_get_type(a_array->mode_data.immutable.tape[0]);
        if (l_type != TAPE_TYPE_ARRAY_START) {
            return 0;
        }
        
        // Use jump pointer to get close position
        uint64_t l_close_idx = dap_tape_get_payload(a_array->mode_data.immutable.tape[0]);
        
        // Count elements between start and close
        size_t l_count = 0;
        size_t l_pos = 1;  // Skip array start
        
        while (l_pos < l_close_idx && l_pos < a_array->mode_data.immutable.tape_count) {
            l_count++;
            l_pos = dap_tape_next(a_array->mode_data.immutable.tape, a_array->mode_data.immutable.tape_count, l_pos);
        }
        
        return l_count;
    } else {
        // MALLOC_MUTABLE mode - use DOM
        dap_json_value_t *l_value = a_array->mode_data.mutable.value;
        if (!l_value || dap_json_get_type_value(l_value) != DAP_JSON_TYPE_ARRAY) {
            return 0;
        }
        
        return dap_json_get_array_len(l_value);
    }
}

/* ========================================================================== */
/*                          ARRAY ELEMENT ACCESSORS                           */
/* ========================================================================== */

/**
 * @brief Get array element by index
 * @details Works for MUTABLE mode only (DOM-based arrays)
 * @note For IMMUTABLE mode (tape), use iterator API instead
 */
dap_json_t* dap_json_array_get_idx(dap_json_t* a_array, size_t a_idx)
{
    if (!a_array) {
        return NULL;
    }
    
    // Only works for MUTABLE mode
    if (a_array->mode != DAP_JSON_MODE_MUTABLE) {
        log_it(L_ERROR, "array_get_idx only works for MUTABLE mode. Use iterator for IMMUTABLE (tape) mode.");
        return NULL;
    }
    
    dap_json_value_t *l_array_value = s_unwrap_value(a_array);
    if (!l_array_value || l_array_value->type != DAP_JSON_TYPE_ARRAY) {
        return NULL;
    }
    
    // MUTABLE mode: get storage
    dap_json_array_storage_t *l_storage = (dap_json_array_storage_t*)dap_json_get_storage_ptr(l_array_value);
    if (!l_storage || a_idx >= l_storage->count) {
        return NULL;
    }
    
    dap_json_value_t *l_element = l_storage->elements[a_idx];
    if (!l_element) {
        return NULL;
    }
    
    // Create borrowed wrapper
    return s_wrap_value_borrowed(l_element, a_array);
}

/**
 * @brief Get string element from array by index (with length for zero-copy)
 * @param[in] a_array Array object
 * @param[in] a_idx Element index
 * @param[out] a_out_length String length (optional, can be NULL)
 * @return Pointer to string data, or NULL if not found or wrong type
 */
const char* dap_json_array_get_string_n(dap_json_t* a_array, size_t a_idx, size_t *a_out_length)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_array, a_idx);
    return l_elem ? dap_json_get_string_n(l_elem, a_out_length) : NULL;
}

/**
 * @brief Get string element from array by index (null-terminated C string)
 * @details Lazy materialization: creates null-terminated copy only on first access
 * @param[in] a_array Array object
 * @param[in] a_idx Element index
 * @return Pointer to null-terminated string, or NULL if not found or wrong type
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
 * @brief  Internal helper: insert element into array at specified position
 * @details Works ONLY for MALLOC_MUTABLE arrays
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
    
    dap_json_array_storage_t *l_storage = s_get_array_storage(l_array);
    if (!l_storage) {
        log_it(L_ERROR, "Invalid array storage");
        return -1;
    }
    
    size_t l_count = l_storage->count;
    
    // If index is at or past the end, just append
    if (a_idx >= l_count) {
        return dap_json_array_v2_add(l_array, l_elem) ? 0 : -1;
    }
    
    // Grow array if needed
    if (l_count >= l_storage->capacity) {
        size_t l_new_capacity = l_storage->capacity * 2;
        if (l_new_capacity < 8) {
            l_new_capacity = 8;
        }
        
        dap_json_value_t **l_new_elements = DAP_REALLOC(
            l_storage->elements,
            l_new_capacity * sizeof(dap_json_value_t*)
        );
        
        if (!l_new_elements) {
            log_it(L_ERROR, "Failed to grow array to %zu elements", l_new_capacity);
            return -1;
        }
        
        l_storage->elements = l_new_elements;
        l_storage->capacity = l_new_capacity;
    }
    
    // Shift elements right from insertion point
    memmove(&l_storage->elements[a_idx + 1],
            &l_storage->elements[a_idx],
            (l_count - a_idx) * sizeof(dap_json_value_t*));
    
    // Insert new element
    l_storage->elements[a_idx] = l_elem;
    l_storage->count++;
    l_array->length = (uint16_t)l_storage->count;
    
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
 * @brief  Delete array elements (MALLOC_MUTABLE only)
 * @details ARENA arrays are immutable - cannot delete
 */
int dap_json_array_del_idx(dap_json_t* a_array, size_t a_idx, size_t a_count)
{
    if (!a_array) {
        return -1;
    }
    
    //  Check mode
    if (a_array->mode == DAP_JSON_MODE_IMMUTABLE) {
        log_it(L_ERROR, "Cannot delete from ARENA (immutable) array");
        return -1;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    dap_json_array_storage_t *l_storage = s_get_array_storage(l_array);
    
    if (!l_storage) {
        log_it(L_ERROR, "Invalid array storage");
        return -1;
    }
    
    if (a_idx >= l_storage->count || a_count == 0) {
        return -1;
    }
    
    // Limit count to available elements
    if (a_idx + a_count > l_storage->count) {
        a_count = l_storage->count - a_idx;
    }
    
    // Free elements (MALLOC mode owns them)
    for (size_t i = 0; i < a_count; i++) {
        dap_json_value_v2_free(l_storage->elements[a_idx + i]);
    }
    
    // Shift remaining elements
    if (a_idx + a_count < l_storage->count) {
        memmove(&l_storage->elements[a_idx],
                &l_storage->elements[a_idx + a_count],
                (l_storage->count - a_idx - a_count) * sizeof(dap_json_value_t*));
    }
    
    l_storage->count -= a_count;
    l_array->length = (uint16_t)l_storage->count;
    
    return 0;
}

/**
 * @brief  Sort array (MALLOC_MUTABLE only)
 * @details ARENA arrays are immutable - cannot sort
 */
void dap_json_array_sort(dap_json_t* a_array, dap_json_sort_fn_t a_sort_fn)
{
    if (!a_array || !a_sort_fn) {
        return;
    }
    
    //  Check mode
    if (a_array->mode == DAP_JSON_MODE_IMMUTABLE) {
        log_it(L_ERROR, "Cannot sort ARENA (immutable) array");
        return;
    }
    
    dap_json_value_t *l_array = s_unwrap_value(a_array);
    dap_json_array_storage_t *l_storage = s_get_array_storage(l_array);
    
    if (!l_storage) {
        log_it(L_ERROR, "Invalid array storage");
        return;
    }
    
    size_t l_count = l_storage->count;
    if (l_count <= 1) {
        return; // Nothing to sort
    }
    
    // Create temporary array of wrapped values for sorting
    dap_json_t **l_wrappers = DAP_NEW_Z_COUNT(dap_json_t*, l_count);
    if (!l_wrappers) {
        log_it(L_ERROR, "Failed to allocate memory for sorting");
        return;
    }
    
    // Wrap all elements
    for (size_t i = 0; i < l_count; i++) {
        l_wrappers[i] = s_wrap_value_borrowed(l_storage->elements[i], a_array);
    }
    
    // Create index array for sorting
    size_t *l_indices = DAP_NEW_Z_COUNT(size_t, l_count);
    for (size_t i = 0; i < l_count; i++) {
        l_indices[i] = i;
    }
    
    // Simple selection sort (O(n^2) but stable and in-place)
    for (size_t i = 0; i < l_count - 1; i++) {
        size_t l_min_idx = i;
        for (size_t j = i + 1; j < l_count; j++) {
            if (a_sort_fn(l_wrappers[l_indices[j]], l_wrappers[l_indices[l_min_idx]]) < 0) {
                l_min_idx = j;
            }
        }
        if (l_min_idx != i) {
            // Swap indices
            size_t l_temp_idx = l_indices[i];
            l_indices[i] = l_indices[l_min_idx];
            l_indices[l_min_idx] = l_temp_idx;
        }
    }
    
    // Apply permutation to underlying storage array
    dap_json_value_t **l_temp_elements = DAP_NEW_Z_COUNT(dap_json_value_t*, l_count);
    for (size_t i = 0; i < l_count; i++) {
        l_temp_elements[i] = l_storage->elements[l_indices[i]];
    }
    for (size_t i = 0; i < l_count; i++) {
        l_storage->elements[i] = l_temp_elements[i];
    }
    DAP_DELETE(l_temp_elements);
    DAP_DELETE(l_indices);
    
    // Free wrappers (but not values - they stay in array)
    for (size_t i = 0; i < l_count; i++) {
        DAP_DELETE(l_wrappers[i]);
    }
    DAP_DELETE(l_wrappers);
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
 * @brief  Add uint64 field to object
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
    
    // For uint64 > INT64_MAX, we need special handling
    dap_json_value_t *l_value;
    if (a_value <= INT64_MAX) {
        // Fits in int64: use create_int
        l_value = dap_json_value_v2_create_int((int64_t)a_value);
    } else {
        // uint64 > INT64_MAX: allocate uint64 separately
        l_value = DAP_NEW_Z(dap_json_value_t);
        if (!l_value) {
            log_it(L_ERROR, "Failed to allocate value");
            return -1;
        }
        
        uint64_t *l_allocated = DAP_NEW(uint64_t);
        if (!l_allocated) {
            DAP_DELETE(l_value);
            log_it(L_ERROR, "Failed to allocate uint64 storage");
            return -1;
        }
        *l_allocated = a_value;
        
        l_value->type = DAP_JSON_TYPE_UINT64;
        l_value->flags = 0;
        l_value->length = 1; // Flag: allocated
        l_value->offset = (uint32_t)(uintptr_t)l_allocated;
    }
    
    if (!l_value) {
        log_it(L_ERROR, "Failed to create uint64 value");
        return -1;
    }
    
    if (!dap_json_object_v2_add(l_obj, a_key, l_value)) {
        dap_json_value_v2_free(l_value);
        log_it(L_ERROR, "Failed to add value to object");
        return -1;
    }
    
    return 0;
}

/**
 * @brief  Add uint256 field to object
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
    
    // uint256 always allocated separately (32 bytes)
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte value");
        return -1;
    }
    
    uint256_t *l_allocated = DAP_NEW(uint256_t);
    if (!l_allocated) {
        DAP_DELETE(l_value);
        log_it(L_ERROR, "Failed to allocate uint256 storage");
        return -1;
    }
    *l_allocated = a_value;
    
    l_value->type = DAP_JSON_TYPE_UINT256;
    l_value->flags = 0;
    l_value->length = 1; // Flag: allocated
    l_value->offset = (uint32_t)(uintptr_t)l_allocated;
    
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
/**
 * @brief Set/update string field in object (replaces existing value) - zero-copy version
 * @param[in] a_json JSON object
 * @param[in] a_key Object key
 * @param[in] a_value String value (data pointer)
 * @param[in] a_length String length
 * @return 0 on success, -1 on error
 */
int dap_json_object_set_string_n(dap_json_t* a_json, const char* a_key, const char* a_value, size_t a_length)
{
    // Delete existing key first, then add new value
    dap_json_object_del(a_json, a_key);
    return dap_json_object_add_string_len(a_json, a_key, a_value, (int)a_length);
}

/**
 * @brief Set/update string field in object (replaces existing value)
 * @param[in] a_json JSON object
 * @param[in] a_key Object key
 * @param[in] a_value String value (null-terminated)
 * @return 0 on success, -1 on error
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
/**
 * @brief  Materialize zero-copy string to null-terminated copy
 * @details For ARENA_IMMUTABLE: creates null-terminated copy from source buffer
 *          For MALLOC_MUTABLE: strings already null-terminated, just return pointer
 * @param[in] a_json JSON wrapper (needed to access source buffer for ARENA mode)
 * @param[in,out] a_string_value String value to materialize
 * @return Pointer to null-terminated string, or NULL on error
 */
static const char* s_materialize_string(dap_json_t* a_json, dap_json_value_t *a_string_value)
{
    if (!a_string_value || a_string_value->type != DAP_JSON_TYPE_STRING) {
        return NULL;
    }
    
    // Check mode
    if (a_json->mode == DAP_JSON_MODE_MUTABLE) {
        // MALLOC strings: offset → malloc'd null-terminated string
        return (const char*)(uintptr_t)a_string_value->offset;
    }
    
    // ARENA_IMMUTABLE: offset → position in source buffer
    // Need to check if we have cached materialized copy
    // For now, we'll create a temporary null-terminated copy
    // TODO  Add string pool for caching materialized strings
    
    dap_json_stage2_t *l_stage2 = s_get_stage2(a_json);
    if (!l_stage2 || !l_stage2->input) {
        log_it(L_ERROR, "No source buffer for ARENA string");
        return NULL;
    }
    
    const char *l_source_ptr = (const char*)l_stage2->input + a_string_value->offset;
    size_t l_length = a_string_value->length;
    
    // Allocate null-terminated copy
    char *l_copy = DAP_NEW_Z_COUNT(char, l_length + 1);
    if (!l_copy) {
        log_it(L_ERROR, "Failed to allocate string materialization (%zu bytes)", l_length + 1);
        return NULL;
    }
    
    memcpy(l_copy, l_source_ptr, l_length);
    l_copy[l_length] = '\0';
    
    // ⚠️ WARNING: This is a memory leak for now!
    // Phase 2.1 will add string pool to track and free these
    
    return l_copy;
}

/**
 * @brief Get string field from object (with length for zero-copy)
 * @details Returns pointer to string data and its length
 *          String is guaranteed to be null-terminated (via String Pool)
 * @param[in] a_json JSON object
 * @param[in] a_key Object key
 * @param[out] a_out_length String length (optional, can be NULL)
 * @return Pointer to string data, or NULL if not found/not a string
 */
const char* dap_json_object_get_string_n(dap_json_t* a_json, const char* a_key, size_t *a_out_length)
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
    
    if (a_out_length) {
        *a_out_length = l_value->length;
    }
    
    //  Zero-copy string access via offset
    return dap_json_get_ptr(l_value, a_json->mode_data.immutable.input_buffer);
}

/**
 * @brief Get string field from object (null-terminated C string)
 * @details Lazy materialization: creates null-terminated copy only on first access
 * @param[in] a_json JSON object
 * @param[in] a_key Object key
 * @return Pointer to null-terminated string, or NULL if not found/not a string
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
    
    // Materialize if needed (lazy null-termination)
    return s_materialize_string(a_json, l_value);
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
 * @brief  Get int64 field with error checking
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
        // Check if inline (length==0) or allocated (length==1)
        if (l_value->length == 0) {
            // Inline: stored in offset as int32
            *a_out = (int64_t)(int32_t)l_value->offset;
        } else {
            // Allocated: offset → pointer to int64
            int64_t *l_ptr = (int64_t*)(uintptr_t)l_value->offset;
            *a_out = *l_ptr;
        }
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        // offset → pointer to double
        double *l_ptr = (double*)(uintptr_t)l_value->offset;
        *a_out = (int64_t)(*l_ptr);
        return true;
    }
    
    return false;
}

/**
 * @brief  Get uint64 field with error checking
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
        // Extract int64
        int64_t l_int_val;
        if (l_value->length == 0) {
            // Inline int32
            l_int_val = (int64_t)(int32_t)l_value->offset;
        } else {
            // Allocated int64
            int64_t *l_ptr = (int64_t*)(uintptr_t)l_value->offset;
            l_int_val = *l_ptr;
        }
        *a_out = (uint64_t)l_int_val;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_UINT64) {
        // offset → pointer to uint64
        uint64_t *l_ptr = (uint64_t*)(uintptr_t)l_value->offset;
        *a_out = *l_ptr;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_UINT256) {
        // offset → pointer to uint256, truncate to lower 64 bits
        uint256_t *l_ptr = (uint256_t*)(uintptr_t)l_value->offset;
        *a_out = (uint64_t)l_ptr->lo;
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        // offset → pointer to double
        double *l_ptr = (double*)(uintptr_t)l_value->offset;
        *a_out = (uint64_t)(*l_ptr);
        return true;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Parse string as uint64 (for legacy/compatibility)
        // Get string pointer (mode-aware)
        dap_json_stage2_t *l_stage2 = s_get_stage2(a_json);
        const char *l_str_ptr;
        
        if (a_json->mode == DAP_JSON_MODE_MUTABLE) {
            l_str_ptr = (const char*)(uintptr_t)l_value->offset;
        } else {
            l_str_ptr = (const char*)l_stage2->input + l_value->offset;
        }
        
        char *l_endptr = NULL;
        errno = 0;
        unsigned long long l_val = strtoull(l_str_ptr, &l_endptr, 10);
        
        if (errno == 0 && l_endptr != l_str_ptr && *l_endptr == '\0') {
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
        // offset → pointer to uint256
        uint256_t *l_ptr = (uint256_t*)(uintptr_t)l_value->offset;
        *a_out = *l_ptr;
        return 0;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Fallback: parse hex string (for compatibility)
        // Get string pointer
        const char *l_str_ptr = s_materialize_string(a_json, l_value);
        *a_out = dap_uint256_scan_uninteger(l_str_ptr);
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
        // offset → pointer to double
        double *l_ptr = (double*)(uintptr_t)l_value->offset;
        return *l_ptr;
    } else if (l_value->type == DAP_JSON_TYPE_INT) {
        // Extract int64
        int64_t l_int_val;
        if (l_value->length == 0) {
            // Inline int32
            l_int_val = (int64_t)(int32_t)l_value->offset;
        } else {
            // Allocated int64
            int64_t *l_ptr = (int64_t*)(uintptr_t)l_value->offset;
            l_int_val = *l_ptr;
        }
        return (double)l_int_val;
    } else if (l_value->type == DAP_JSON_TYPE_STRING) {
        // Check for special string values: "Infinity", "-Infinity", "NaN"
        const char *l_str = s_materialize_string(a_json, l_value);
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
    
    //  boolean stored in offset (0=false, 1=true)
    return (l_value->offset != 0);
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
/**
 * @brief Get object field from object (with caching)
 * @note Returns borrowed reference (cached wrapper) - do NOT free manually!
 */
dap_json_t* dap_json_object_get_object(dap_json_t* a_json, const char* a_key)
{
    dap_json_t *l_result = NULL;
    if (!dap_json_object_get_ex(a_json, a_key, &l_result)) {
        return NULL;
    }
    
    // Verify it's an object
    if (!l_result || !dap_json_is_object(l_result)) {
        return NULL;
    }
    
    return l_result; // Cached borrowed reference
}

/**
 * @brief Get array field from object (with caching)
 * @note Returns borrowed reference (cached wrapper) - do NOT free manually!
 */
dap_json_t* dap_json_object_get_array(dap_json_t* a_json, const char* a_key)
{
    dap_json_t *l_result = NULL;
    if (!dap_json_object_get_ex(a_json, a_key, &l_result)) {
        return NULL;
    }
    
    // Verify it's an array
    if (!l_result || !dap_json_is_array(l_result)) {
        return NULL;
    }
    
    return l_result; // Cached borrowed reference
}

/**
 * @brief Get object field with exists check
 * @return true if key exists and value retrieved, false otherwise
 */
/**
 * @brief  Get wrapper for value at key (JSON-C compatible)
 * @details Creates wrapper for value, cached within parent wrapper
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
    
    // Get value using v2 API
    dap_json_value_t *l_val = dap_json_object_v2_get(l_obj, a_key);
    if (!l_val) {
        debug_if(s_debug_more, L_DEBUG, "Key '%s' not found in object", a_key);
        return false;
    }
    
    //  Create borrowed wrapper (no caching for now)
    // TODO  Add wrapper cache to dap_json wrapper struct
    *a_value = s_wrap_value_borrowed(l_val, a_json);
    if (!*a_value) {
        log_it(L_ERROR, "Failed to create wrapper for key '%s'", a_key);
        return false;
    }
    
    return true;
}

/**
 * @brief  Delete key from object
 * @return 0 on success, -1 on failure
 * @note For ARENA_IMMUTABLE objects: NOT SUPPORTED (returns -1)
 *       For MALLOC_MUTABLE objects: removes key and frees value
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
    
    // Check mode
    if (a_json->mode != DAP_JSON_MODE_MUTABLE) {
        log_it(L_ERROR, "Cannot delete from ARENA_IMMUTABLE object");
        return -1;
    }
    
    // Get storage
    dap_json_object_storage_t *l_storage = s_get_object_storage(l_obj);
    if (!l_storage) {
        log_it(L_ERROR, "Invalid object storage");
        return -1;
    }
    
    // Find key
    for (size_t i = 0; i < l_storage->count; i++) {
        if (strcmp(l_storage->keys[i], a_key) == 0) {
            // Free key and value
            DAP_DELETE(l_storage->keys[i]);
            dap_json_value_v2_free(l_storage->values[i]);
            
            // Shift remaining pairs
            for (size_t j = i; j < l_storage->count - 1; j++) {
                l_storage->keys[j] = l_storage->keys[j + 1];
                l_storage->values[j] = l_storage->values[j + 1];
            }
            l_storage->count--;
            l_obj->length = (uint16_t)l_storage->count;
            
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
/**
 * @brief Get string value from JSON (with length for zero-copy)
 * @param[in] a_json JSON value
 * @param[out] a_out_length String length (optional, can be NULL)
 * @return Pointer to string data, or NULL if not a string
 */
const char* dap_json_get_string_n(dap_json_t* a_json, size_t *a_out_length)
{
    if (!a_json) {
        return NULL;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value || l_value->type != DAP_JSON_TYPE_STRING) {
        return NULL;
    }
    
    if (a_out_length) {
        *a_out_length = l_value->length;
    }
    
    //  Zero-copy string access via offset
    return dap_json_get_ptr(l_value, a_json->mode_data.immutable.input_buffer);
}

/**
 * @brief Get string value from JSON (null-terminated C string)
 * @details Lazy materialization: creates null-terminated copy only on first access
 * @param[in] a_json JSON value
 * @return Pointer to null-terminated string, or NULL if not a string
 */
const char* dap_json_get_string(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    // For MUTABLE mode, use DOM
    if (a_json->mode == DAP_JSON_MODE_MUTABLE) {
        dap_json_value_t *l_value = s_unwrap_value(a_json);
        if (!l_value) {
            return NULL;
        }
        // Return pointer from value (already null-terminated in DOM mode)
        return dap_json_get_string_value(l_value);
    }
    
    // For IMMUTABLE mode, use iterator
    dap_json_iterator_t *l_iter = dap_json_iterator_new(a_json);
    if (!l_iter) {
        return NULL;
    }
    
    // Get string via iterator (handles escapes properly)
    char *l_result = dap_json_iterator_get_string_dup(l_iter);
    
    dap_json_iterator_free(l_iter);
    
    // WARNING: Caller must free() this string!
    return l_result;
}

/**
 * @brief  Get int64 value from JSON
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
        // Check if inline or allocated
        if (l_value->length == 0) {
            // Inline int32
            return (int64_t)(int32_t)l_value->offset;
        } else {
            // Allocated int64
            int64_t *l_ptr = (int64_t*)(uintptr_t)l_value->offset;
            return *l_ptr;
        }
    }
    
    if (l_value->type == DAP_JSON_TYPE_DOUBLE) {
        // offset → pointer to double
        double *l_ptr = (double*)(uintptr_t)l_value->offset;
        return (int64_t)(*l_ptr);
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
 * @brief  Get boolean value from JSON
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
    
    //  boolean stored in offset (0=false, 1=true)
    return (l_value->offset != 0);
}

/**
 * @brief  Get double value from JSON
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
        // offset → pointer to double
        double *l_ptr = (double*)(uintptr_t)l_value->offset;
        return *l_ptr;
    }
    
    if (l_value->type == DAP_JSON_TYPE_INT) {
        // Extract int64
        int64_t l_int_val;
        if (l_value->length == 0) {
            // Inline int32
            l_int_val = (int64_t)(int32_t)l_value->offset;
        } else {
            // Allocated int64
            int64_t *l_ptr = (int64_t*)(uintptr_t)l_value->offset;
            l_int_val = *l_ptr;
        }
        return (double)l_int_val;
    }
    
    return 0.0;
}

/**
 * @brief Get uint64_t value from JSON value
 * @param a_json JSON value (must be an integer)
 * @return uint64_t value, or 0 if not an integer
 */
uint64_t dap_json_get_uint64(dap_json_t* a_json)
{
    int64_t l_val = dap_json_get_int64(a_json);
    return (uint64_t)l_val;
}

/**
 * @brief Get nanotime value from JSON value
 * @param a_json JSON value (must be an integer representing nanoseconds)
 * @return dap_nanotime_t value, or 0 if not an integer
 */
dap_nanotime_t dap_json_get_nanotime(dap_json_t* a_json)
{
    int64_t l_val = dap_json_get_int64(a_json);
    return (dap_nanotime_t)l_val;
}

/**
 * @brief Get number of keys in JSON object
 * @param a_json JSON object
 * @return Number of keys, or 0 if not an object
 */
size_t dap_json_object_length(dap_json_t* a_json)
{
    if (!a_json) {
        return 0;
    }
    
    dap_json_value_t *l_value = s_unwrap_value(a_json);
    if (!l_value || l_value->type != DAP_JSON_TYPE_OBJECT) {
        return 0;
    }
    
    //  count stored in length field
    return l_value->length;
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
    
    if (a_json->mode == DAP_JSON_MODE_IMMUTABLE) {
        // Tape mode
        if (!a_json->mode_data.immutable.tape || a_json->mode_data.immutable.tape_count == 0) {
            return DAP_JSON_TYPE_NULL;
        }
        
        // Root is always first tape entry
        uint8_t l_type = dap_tape_get_type(a_json->mode_data.immutable.tape[0]);
        
        switch (l_type) {
            case TAPE_TYPE_OBJECT_START:  return DAP_JSON_TYPE_OBJECT;
            case TAPE_TYPE_ARRAY_START:   return DAP_JSON_TYPE_ARRAY;
            case TAPE_TYPE_STRING:        return DAP_JSON_TYPE_STRING;
            case TAPE_TYPE_NUMBER:        return DAP_JSON_TYPE_INT;
            case TAPE_TYPE_TRUE:
            case TAPE_TYPE_FALSE:         return DAP_JSON_TYPE_BOOLEAN;
            case TAPE_TYPE_NULL:          return DAP_JSON_TYPE_NULL;
            default:                      return DAP_JSON_TYPE_NULL;
        }
    } else {
        // MUTABLE mode - use DOM
        dap_json_value_t *l_value = s_unwrap_value(a_json);
        return l_value ? dap_json_get_type_value(l_value) : DAP_JSON_TYPE_NULL;
    }
}

/**
 * @brief  Iterate over object key-value pairs
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
    
    // Mode-aware iteration
    if (a_json->mode == DAP_JSON_MODE_IMMUTABLE) {
        // ARENA: iterate using offset-based pairs in stage2
        dap_json_stage2_t *l_stage2 = s_get_stage2(a_json);
        if (!l_stage2) {
            log_it(L_ERROR, "No stage2 for ARENA object");
            return;
        }
        
        // offset → start of pair_indices in stage2->arena
        size_t l_count = l_value->length;
        uint32_t *l_pair_indices = (uint32_t*)((uint8_t*)l_stage2->arena + l_value->offset);
        
        for (size_t i = 0; i < l_count; i++) {
            uint32_t l_key_idx = l_pair_indices[i * 2];
            uint32_t l_val_idx = l_pair_indices[i * 2 + 1];
            
            dap_json_value_t *l_key_value = &l_stage2->values[l_key_idx];
            dap_json_value_t *l_pair_value = &l_stage2->values[l_val_idx];
            
            // Get key string (zero-copy from source buffer)
            const char *l_key = (const char*)l_stage2->input + l_key_value->offset;
            
            // Wrap value for callback
            dap_json_t *l_wrapped = s_wrap_value_borrowed(l_pair_value, a_json);
            if (l_wrapped) {
                callback(l_key, l_wrapped, user_data);
            }
        }
    } else {
        // MALLOC_MUTABLE: iterate using storage
        dap_json_object_storage_t *l_storage = s_get_object_storage(l_value);
        if (!l_storage) {
            log_it(L_ERROR, "Invalid object storage");
            return;
        }
        
        for (size_t i = 0; i < l_storage->count; i++) {
            const char *l_key = l_storage->keys[i];
            dap_json_value_t *l_pair_value = l_storage->values[i];
            
            // Wrap value for callback
            dap_json_t *l_wrapped = s_wrap_value_borrowed(l_pair_value, a_json);
            if (l_wrapped) {
                callback(l_key, l_wrapped, user_data);
            }
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
 * @brief Write JSON object to file
 * @param a_file_path File path to write to
 * @param a_json JSON object to write
 * @return 0 on success, -1 on error
 */
int dap_json_to_file(const char* a_file_path, dap_json_t* a_json)
{
    if (!a_file_path || !a_json) {
        log_it(L_ERROR, "NULL parameter provided");
        return -1;
    }
    
    // Serialize JSON to string
    const char *l_json_str = dap_json_to_string(a_json);
    if (!l_json_str) {
        log_it(L_ERROR, "Failed to serialize JSON to string");
        return -1;
    }
    
    // Open file for writing
    FILE *l_file = fopen(a_file_path, "w");
    if (!l_file) {
        log_it(L_ERROR, "Failed to open file for writing: %s", a_file_path);
        return -1;
    }
    
    // Write to file
    size_t l_len = strlen(l_json_str);
    size_t l_written = fwrite(l_json_str, 1, l_len, l_file);
    fclose(l_file);
    
    if (l_written != l_len) {
        log_it(L_ERROR, "Failed to write complete JSON to file");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Print JSON object to stream (debug)
 */
void dap_json_print_object(dap_json_t *a_json, FILE *a_stream, int a_indent_level)
{
    if (!a_json || !a_stream) {
        return;
    }
    
    // Use compact format for now (pretty-print not yet implemented)
    char *l_str = dap_json_to_string(a_json);
    
    if (l_str) {
        fprintf(a_stream, "%*s%s\n", a_indent_level * 2, "", l_str);
        DAP_DELETE(l_str); // dap_json_to_string returns malloc'd string
    } else {
        fprintf(a_stream, "%*s<JSON stringify failed>\n", a_indent_level * 2, "");
    }
}

/* ========================================================================== */
/*                          LEGACY JSON-C API                                 */
/* ========================================================================== */

/**
 * @brief Legacy json-c API: parse with verbose error reporting
 */
dap_json_t* dap_json_tokener_parse_verbose(const char* a_str, dap_json_tokener_error_t* a_error)
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
        case DAP_JSON_TOKENER_ERROR_DEPTH: return "maximum nesting depth exceeded";
        default: return "unknown error";
    }
}

// End of file
