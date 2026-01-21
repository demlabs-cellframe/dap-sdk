/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
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

#pragma once

#include "dap_common.h"
#include "dap_math_ops.h"
#include "dap_math_convert.h"
#include "dap_time.h"
#include "dap_cpu_arch.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize dap_json module
 * @details Must be called once at application startup before using any JSON functions.
 *          Initializes CPU feature detection for optimal SIMD performance.
 */
void dap_json_init(void);

/**
 * @brief Manually set SIMD architecture for JSON module
 * @details Overrides automatic CPU detection. Affects all JSON components (Stage 1, Stage 2, etc).
 *          Useful for testing, benchmarking, and edge cases where auto-detection may not work.
 *          Must be called AFTER dap_json_init().
 * @param a_arch Desired architecture (use DAP_CPU_ARCH_* constants from dap_cpu_arch.h)
 *               DAP_CPU_ARCH_AUTO = automatic (default behavior)
 *               DAP_CPU_ARCH_REFERENCE = pure C reference
 *               DAP_CPU_ARCH_X86_SSE2 = SSE2 (x86)
 *               DAP_CPU_ARCH_X86_AVX2 = AVX2 (x86)
 *               DAP_CPU_ARCH_X86_AVX512 = AVX-512 (x86)
 *               DAP_CPU_ARCH_ARM_NEON = NEON (ARM)
 * @return 0 on success, -1 if requested architecture is not available/compiled
 * @note If requested architecture is not available, the call fails and current selection remains unchanged
 */
int dap_json_set_arch(dap_cpu_arch_t a_arch);

/**
 * @brief Get currently selected SIMD architecture for JSON module
 * @return Current architecture (DAP_CPU_ARCH_* constant)
 */
dap_cpu_arch_t dap_json_get_arch(void);

/**
 * @brief Get human-readable name of SIMD architecture
 * @param a_arch Architecture enum value
 * @return String name (e.g. "SSE2", "NEON", "Reference C")
 * @note Wrapper around dap_cpu_arch_get_name() for convenience
 */
const char* dap_json_get_arch_name(dap_cpu_arch_t a_arch);

/**
 * @brief Opaque DAP JSON type - hides internal json-c implementation
 * Can represent both JSON objects and arrays internally
 */
typedef struct dap_json dap_json_t;

/**
 * @brief JSON operation mode
 * @details Determines parsing vs creation semantics
 */
typedef enum {
    DAP_JSON_MODE_IMMUTABLE,   /**< Parsed JSON (tape-based, read-only, fast) */
    DAP_JSON_MODE_MUTABLE      /**< Created JSON (DOM-based, mutable, full API) */
} dap_json_mode_t;

typedef int (*dap_json_sort_fn_t)(const dap_json_t *a, const dap_json_t *b);

// Object creation and destruction
dap_json_t* dap_json_object_new(void);
dap_json_t* dap_json_parse_string(const char* a_json_string);
void dap_json_object_free(dap_json_t* a_json);  // Works for both owned and borrowed
void dap_json_cleanup_thread_arena(void);        // Clean up thread-local arena (call at thread end)
// Use dap_json_object_free() for arrays too - it handles both types
#define dap_json_array_free(a_array) dap_json_object_free(a_array)
dap_json_t* dap_json_object_ref(dap_json_t* a_json);

// Mode queries
bool dap_json_is_mutable(const dap_json_t* a_json);
bool dap_json_is_immutable(const dap_json_t* a_json);
dap_json_mode_t dap_json_get_mode(const dap_json_t* a_json);
// Value object creation (for simple types)
dap_json_t* dap_json_object_new_int(int a_value);
dap_json_t* dap_json_object_new_string(const char* a_value);
dap_json_t* dap_json_object_new_string_len(const char* a_value, int a_len);
dap_json_t* dap_json_object_new_double(double a_value);
dap_json_t* dap_json_object_new_bool(bool a_value);

// Array creation and manipulation
dap_json_t* dap_json_array_new(void);

int dap_json_array_add(dap_json_t* a_array, dap_json_t* a_item);
int dap_json_array_del_idx(dap_json_t* a_array, size_t a_idx, size_t a_count);
size_t dap_json_array_length(dap_json_t* a_array);
dap_json_t* dap_json_array_get_idx(dap_json_t* a_array, size_t a_idx);
void dap_json_array_sort(dap_json_t* a_array, dap_json_sort_fn_t a_sort_fn);

// Array element insertion (typed convenience functions)
int dap_json_array_insert_string(dap_json_t* a_array, size_t a_idx, const char* a_value);
int dap_json_array_insert_int(dap_json_t* a_array, size_t a_idx, int a_value);
int dap_json_array_insert_double(dap_json_t* a_array, size_t a_idx, double a_value);
int dap_json_array_insert_bool(dap_json_t* a_array, size_t a_idx, bool a_value);
int dap_json_array_insert_object(dap_json_t* a_array, size_t a_idx, dap_json_t* a_object);
int dap_json_array_insert_array(dap_json_t* a_array, size_t a_idx, dap_json_t* a_inner_array);

// Typed array element access
const char* dap_json_array_get_string(dap_json_t* a_array, size_t a_idx);
const char* dap_json_array_get_string_n(dap_json_t* a_array, size_t a_idx, size_t *a_out_length);  // Zero-copy: returns (data, length)
int dap_json_array_get_int(dap_json_t* a_array, size_t a_idx);
int64_t dap_json_array_get_int64(dap_json_t* a_array, size_t a_idx);
double dap_json_array_get_double(dap_json_t* a_array, size_t a_idx);
bool dap_json_array_get_bool(dap_json_t* a_array, size_t a_idx);
dap_json_t* dap_json_array_get_object(dap_json_t* a_array, size_t a_idx);
dap_json_t* dap_json_array_get_array(dap_json_t* a_array, size_t a_idx);

// Object field manipulation
int dap_json_object_add_string(dap_json_t* a_json, const char* a_key, const char* a_value);
int dap_json_object_add_string_len(dap_json_t* a_json, const char* a_key, const char* a_value, const int a_len);
int dap_json_object_add_int(dap_json_t* a_json, const char* a_key, int a_value);
int dap_json_object_add_int64(dap_json_t* a_json, const char* a_key, int64_t a_value);
int dap_json_object_add_uint64(dap_json_t* a_json, const char* a_key, uint64_t a_value);
int dap_json_object_add_uint256(dap_json_t* a_json, const char* a_key, uint256_t a_value);
int dap_json_object_add_double(dap_json_t* a_json, const char* a_key, double a_value);
int dap_json_object_add_bool(dap_json_t* a_json, const char* a_key, bool a_value);
int dap_json_object_add_nanotime(dap_json_t* a_json, const char* a_key, dap_nanotime_t a_value);
int dap_json_object_add_time(dap_json_t* a_json, const char* a_key, dap_time_t a_value);
int dap_json_object_add_null(dap_json_t* a_json, const char* a_key);
int dap_json_object_add_object(dap_json_t* a_json, const char* a_key, dap_json_t* a_value);
int dap_json_object_add_array(dap_json_t* a_json, const char* a_key, dap_json_t* a_array);

// Object field modification (update existing keys)
int dap_json_object_set_string(dap_json_t* a_json, const char* a_key, const char* a_value);
int dap_json_object_set_string_n(dap_json_t* a_json, const char* a_key, const char* a_value, size_t a_length);  // Zero-copy: accepts (data, length)
int dap_json_object_set_int(dap_json_t* a_json, const char* a_key, int a_value);
int dap_json_object_set_double(dap_json_t* a_json, const char* a_key, double a_value);
int dap_json_object_set_bool(dap_json_t* a_json, const char* a_key, bool a_value);

// Object field access
const char* dap_json_object_get_string(dap_json_t* a_json, const char* a_key);
const char* dap_json_object_get_string_n(dap_json_t* a_json, const char* a_key, size_t *a_out_length);  // Zero-copy: returns (data, length)
int dap_json_object_get_int(dap_json_t* a_json, const char* a_key);
int64_t dap_json_object_get_int64(dap_json_t* a_json, const char* a_key);
uint64_t dap_json_object_get_uint64(dap_json_t* a_json, const char* a_key);
bool dap_json_object_get_int64_ext(dap_json_t* a_json, const char* a_key, int64_t *a_out);
bool dap_json_object_get_uint64_ext(dap_json_t* a_json, const char* a_key, uint64_t *a_out);
int dap_json_object_get_uint256(dap_json_t* a_json, const char* a_key, uint256_t *a_out);
double dap_json_object_get_double(dap_json_t* a_json, const char* a_key);
bool dap_json_object_get_bool(dap_json_t* a_json, const char* a_key);
dap_json_t* dap_json_object_get_object(dap_json_t* a_json, const char* a_key);
dap_json_t* dap_json_object_get_array(dap_json_t* a_json, const char* a_key);

// String conversion
char* dap_json_to_string(dap_json_t* a_json);
char* dap_json_to_string_pretty(dap_json_t* a_json);

// Advanced object manipulation
dap_json_t* dap_json_from_file(const char* a_file_path);
int dap_json_to_file(const char* a_file_path, dap_json_t* a_json);
bool dap_json_object_get_ex(dap_json_t* a_json, const char* a_key, dap_json_t** a_value);
bool dap_json_object_has_key(dap_json_t* a_json, const char* a_key);  // Convenience: check if key exists
int dap_json_object_del(dap_json_t* a_json, const char* a_key);

// Extended value getters with default
const char* dap_json_object_get_string_default(dap_json_t* a_json, const char* a_key, const char* a_default);
int dap_json_object_get_int_default(dap_json_t* a_json, const char* a_key, int a_default);
int64_t dap_json_object_get_int64(dap_json_t* a_json, const char* a_key);
int64_t dap_json_object_get_int64_default(dap_json_t* a_json, const char* a_key, int64_t a_default);

// JSON type definitions (shared across all JSON components)
#include "dap_json_type.h"

// Debug control API (for benchmarks)
void dap_json_set_debug(bool a_enable);
bool dap_json_get_debug(void);

// Type checking
bool dap_json_is_null(dap_json_t* a_json);
bool dap_json_is_string(dap_json_t* a_json);
bool dap_json_is_int(dap_json_t* a_json);
bool dap_json_is_double(dap_json_t* a_json);
bool dap_json_is_bool(dap_json_t* a_json);
bool dap_json_is_object(dap_json_t* a_json);
bool dap_json_is_array(dap_json_t* a_json);

// Printing functions
void dap_json_print_object(dap_json_t *a_json, FILE *a_stream, int a_indent_level);
void dap_json_print_value(dap_json_t *a_json, const char *a_key, FILE *a_stream, int a_indent_level, bool a_print_separator);
dap_json_type_t dap_json_get_type(dap_json_t* a_json);

// Tokener functions for parsing with error handling
typedef enum {
    DAP_JSON_TOKENER_SUCCESS,
    DAP_JSON_TOKENER_ERROR_DEPTH,
    DAP_JSON_TOKENER_ERROR_PARSE_EOF,
    DAP_JSON_TOKENER_ERROR_PARSE_UNEXPECTED,
    DAP_JSON_TOKENER_ERROR_PARSE_NULL,
    DAP_JSON_TOKENER_ERROR_PARSE_BOOLEAN,
    DAP_JSON_TOKENER_ERROR_PARSE_NUMBER,
    DAP_JSON_TOKENER_ERROR_PARSE_ARRAY,
    DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_NAME,
    DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_SEP,
    DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_VALUE_SEP,
    DAP_JSON_TOKENER_ERROR_PARSE_STRING,
    DAP_JSON_TOKENER_ERROR_PARSE_COMMENT,
    DAP_JSON_TOKENER_ERROR_SIZE
} dap_json_tokener_error_t;

dap_json_t* dap_json_tokener_parse_verbose(const char* a_str, dap_json_tokener_error_t* a_error);
const char* dap_json_tokener_error_desc(dap_json_tokener_error_t a_jerr);

/**
 * @brief Parse JSON from binary buffer with explicit length
 * @details Supports UTF-8, UTF-16LE/BE, UTF-32LE/BE (auto-detection via BOM or heuristics)
 * @param[in] a_json_buffer Buffer containing JSON data
 * @param[in] a_buffer_len Buffer length in bytes
 * @return Parsed JSON object, or NULL on error
 */
dap_json_t* dap_json_parse_buffer(const char *a_json_buffer, size_t a_buffer_len);

// Reference counting (important for json-c compatibility)
dap_json_t* dap_json_object_get_ref(dap_json_t* a_json);

// Value object creation (for simple types)
dap_json_t* dap_json_object_new_int(int a_value);
dap_json_t* dap_json_object_new_int64(int64_t a_value);
dap_json_t* dap_json_object_new_uint64(uint64_t a_value);
dap_json_t* dap_json_object_new_uint256(uint256_t a_value);
dap_json_t* dap_json_object_new_string(const char* a_value);
dap_json_t* dap_json_object_new_double(double a_value);
dap_json_t* dap_json_object_new_bool(bool a_value);

// Object iteration API
/**
 * @brief Callback function type for dap_json_object_foreach
 * @param key The key name (string)
 * @param value Stack-allocated wrapper for the value (TEMPORARY - see notes below)
 * @param user_data User-defined data passed to foreach
 * 
 * CRITICAL NOTES:
 * - The 'value' pointer is TEMPORARY and BORROWED
 * - It is only valid during the callback execution
 * - DO NOT save this pointer for later use
 * - DO NOT call dap_json_object_free() on this wrapper
 * - If you need to keep the value, use dap_json_object_get_object/get_array to get a new wrapper
 */
typedef void (*dap_json_object_foreach_callback_t)(const char* key, dap_json_t* value, void* user_data);

/**
 * @brief Iterate over all key-value pairs in a JSON object
 * @param a_json JSON object to iterate
 * @param callback Function to call for each key-value pair
 * @param user_data User data to pass to callback
 * 
 * The callback receives temporary stack-allocated wrappers for values.
 * See dap_json_object_foreach_callback_t for important usage notes.
 */
void dap_json_object_foreach(dap_json_t* a_json, dap_json_object_foreach_callback_t callback, void* user_data);

// Extended value access API
const char* dap_json_get_string(dap_json_t* a_json);
const char* dap_json_get_string_n(dap_json_t* a_json, size_t *a_out_length);  // Zero-copy: returns (data, length)
int64_t dap_json_get_int64(dap_json_t* a_json);
int dap_json_get_int(dap_json_t* a_json); // Wrapper for int64 that returns int
double dap_json_get_double(dap_json_t* a_json);
bool dap_json_get_bool(dap_json_t* a_json);
uint64_t dap_json_get_uint64(dap_json_t* a_json);
dap_nanotime_t dap_json_get_nanotime(dap_json_t* a_json);
size_t dap_json_object_length(dap_json_t* a_json);

/* ========================================================================== */
/*                   SIMD ARCHITECTURE SELECTION API                          */
/* ========================================================================== */

/**
 * @brief SIMD Architecture Selection for Stage 1 Tokenization
 * @details Manual override for testing, benchmarking, and debugging purposes
 * 
 * @note Uses universal dap_cpu_arch_t from core module for SDK-wide consistency
 */

// Import universal CPU architecture definitions from core
#include "dap_cpu_arch.h"

/**
 * @brief Get human-readable name of SIMD architecture
 * @param a_arch Architecture enum value
 * @return String name (e.g. "SSE2", "NEON", "Reference C")
 * @note Wrapper around dap_cpu_arch_get_name() for convenience
 */
const char* dap_json_get_arch_name(dap_cpu_arch_t a_arch);

#ifdef __cplusplus
}
#endif
