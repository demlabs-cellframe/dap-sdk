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
#include "internal/dap_json_string.h"
#include "internal/dap_json_number.h"
#include "dap_arena.h"
#include "dap_string_pool.h"

/* ========================================================================== */
/*                    INTERNAL STORAGE STRUCTURES                             */
/* ========================================================================== */

/**
 * @brief Phase 2.0.4: Internal storage for mutable arrays
 * @details Used only in MALLOC_MUTABLE mode
 */
typedef struct {
    dap_json_value_t **elements;  /**< Array of pointers to 8-byte values */
    size_t count;                  /**< Current number of elements */
    size_t capacity;               /**< Allocated capacity */
} dap_json_array_storage_t;

/**
 * @brief Phase 2.0.4: Internal storage for mutable objects
 * @details Used only in MALLOC_MUTABLE mode
 */
typedef struct {
    char **keys;                   /**< Array of key strings (malloc'd) */
    dap_json_value_t **values;     /**< Array of pointers to 8-byte values */
    size_t count;                  /**< Current number of pairs */
    size_t capacity;               /**< Allocated capacity */
} dap_json_object_storage_t;

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <locale.h>

#define LOG_TAG "dap_json_stage2_ref"

// Debug flag: detailed logs (below WARNING level)
static bool s_debug_more = false;

// ⭐ Thread-local refcounted arena for JSON parsing
// Reused across multiple parse calls in the same thread for efficiency
static _Thread_local dap_arena_t *s_thread_json_arena = NULL;

// ⭐ Thread-local string pool for object keys
// Reused across multiple parse calls in the same thread for efficiency
static _Thread_local dap_string_pool_t *s_thread_string_pool = NULL;

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
 * @brief Add ref to Stage 2 refs array (Phase 2.0.3)
 * @details Adds an 8-byte value ref to the flat array
 * @return Index of added ref, or -1 on error
 */
static int32_t s_stage2_add_ref(
    dap_json_stage2_t *a_stage2,
    const dap_json_value_t *a_ref
)
{
    if (!a_stage2 || !a_ref) {
        return -1;
    }
    
    // Check if need to grow
    if (a_stage2->values_count >= a_stage2->values_capacity) {
        size_t new_capacity = a_stage2->values_capacity * 2;
        dap_json_value_t *new_refs = (dap_json_value_t*)dap_arena_alloc(
            a_stage2->arena,
            sizeof(dap_json_value_t) * new_capacity
        );
        
        if (!new_refs) {
            log_it(L_ERROR, "Failed to grow refs array to %zu refs", new_capacity);
            return -1;
        }
        
        // Copy existing refs
        memcpy(new_refs, a_stage2->values, 
               sizeof(dap_json_value_t) * a_stage2->values_count);
        
        a_stage2->values = new_refs;
        a_stage2->values_capacity = new_capacity;
        
        debug_if(s_debug_more, L_DEBUG, "Grew refs array to %zu capacity", new_capacity);
    }
    
    // Add ref
    uint32_t index = a_stage2->values_count;
    a_stage2->values[index] = *a_ref;
    a_stage2->values_count++;
    
    return (int32_t)index;
}

/* ========================================================================== */
/*                         VALUE CREATION                                     */
/* ========================================================================== */

/**
 * @brief Create value using Arena allocator (internal, high-performance)
 */
static inline dap_json_value_t *s_create_value_arena(dap_arena_t *a_arena)
{
    // ⭐ Use extended allocation to get page handle for refcounting
    dap_arena_alloc_ex_t l_alloc_result;
    if (!dap_arena_alloc_ex(a_arena, sizeof(dap_json_value_t), &l_alloc_result)) {
        log_it(L_ERROR, "Arena allocation failed for value");
        return NULL;
    }
    
    // Zero-initialize the value
    dap_json_value_t *l_value = (dap_json_value_t *)l_alloc_result.ptr;
    memset(l_value, 0, sizeof(dap_json_value_t));
    
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
    l_value->offset = a_value ? 1 : 0; // Store in offset
    return l_value;
}

/**
 * @brief Create number value (integer)
 */
/**
 * @brief Phase 2.0.4: Create number value (int64)
 * @details For small ints (<2^31): store directly in offset
 *          For large ints: allocate separately, pointer in offset
 */
dap_json_value_t *dap_json_value_v2_create_int(int64_t a_value)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte int value");
        return NULL;
    }
    
    l_value->type = DAP_JSON_TYPE_INT;
    l_value->flags = 0;
    
    // Check if fits in 32-bit offset
    if (a_value >= INT32_MIN && a_value <= INT32_MAX) {
        // Store directly in offset (no allocation needed!)
        l_value->offset = (uint32_t)(int32_t)a_value;
        l_value->length = 0; // Flag: stored inline
    } else {
        // Large int: allocate separately
        int64_t *l_allocated = DAP_NEW(int64_t);
        if (!l_allocated) {
            DAP_DELETE(l_value);
            log_it(L_ERROR, "Failed to allocate int64 storage");
            return NULL;
        }
        *l_allocated = a_value;
        l_value->offset = (uint32_t)(uintptr_t)l_allocated;
        l_value->length = 1; // Flag: allocated
    }
    
    return l_value;
}

/**
 * @brief Phase 2.0.4: Create number value (double)
 * @details Always allocates separately (doubles are 64-bit)
 */
dap_json_value_t *dap_json_value_v2_create_double(double a_value)
{
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte double value");
        return NULL;
    }
    
    // Allocate double separately
    double *l_allocated = DAP_NEW(double);
    if (!l_allocated) {
        DAP_DELETE(l_value);
        log_it(L_ERROR, "Failed to allocate double storage");
        return NULL;
    }
    
    *l_allocated = a_value;
    
    l_value->type = DAP_JSON_TYPE_DOUBLE;
    l_value->flags = 0;
    l_value->length = 1; // Flag: allocated
    l_value->offset = (uint32_t)(uintptr_t)l_allocated;
    
    return l_value;
}

/**
 * @brief Phase 2.0.4: Create string value (MALLOC_MUTABLE mode)
 * 
 * This version COPIES the string. Use only for manually created JSON objects.
 * For parsing, Stage 2 uses refs directly!
 */
dap_json_value_t *dap_json_value_v2_create_string(const char *a_data, size_t a_length)
{
    if (!a_data) {
        log_it(L_ERROR, "NULL string data");
        return NULL;
    }
    
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte string value");
        return NULL;
    }
    
    // Allocate string copy (null-terminated)
    char *l_str_copy = DAP_NEW_Z_COUNT(char, a_length + 1);
    if (!l_str_copy) {
        log_it(L_ERROR, "Failed to allocate string data (%zu bytes)", a_length + 1);
        DAP_DELETE(l_value);
        return NULL;
    }
    
    memcpy(l_str_copy, a_data, a_length);
    l_str_copy[a_length] = '\0';
    
    // Setup 8-byte value
    l_value->type = DAP_JSON_TYPE_STRING;
    l_value->flags = 0; // Not escaped
    l_value->length = (uint16_t)a_length;
    l_value->offset = (uint32_t)(uintptr_t)l_str_copy; // Pointer to allocated string
    
    return l_value;
}

/**
 * @brief Phase 2.0.4: Create array value (MALLOC_MUTABLE mode)
 * @details Creates 8-byte value + separate storage structure
 */
dap_json_value_t *dap_json_value_v2_create_array(void)
{
    // Allocate 8-byte value
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte array value");
        return NULL;
    }
    
    // Allocate storage structure
    dap_json_array_storage_t *l_storage = DAP_NEW_Z(dap_json_array_storage_t);
    if (!l_storage) {
        log_it(L_ERROR, "Failed to allocate array storage");
        DAP_DELETE(l_value);
        return NULL;
    }
    
    // Initialize storage
    l_storage->capacity = INITIAL_ARRAY_CAPACITY;
    l_storage->count = 0;
    l_storage->elements = DAP_NEW_Z_COUNT(dap_json_value_t*, INITIAL_ARRAY_CAPACITY);
    
    if (!l_storage->elements) {
        log_it(L_ERROR, "Failed to allocate array elements");
        DAP_DELETE(l_storage);
        DAP_DELETE(l_value);
        return NULL;
    }
    
    // Setup 8-byte value
    l_value->type = DAP_JSON_TYPE_ARRAY;
    l_value->flags = 0;
    l_value->length = 0; // Will be updated on add
    l_value->offset = (uint32_t)(uintptr_t)l_storage; // Pointer as offset!
    
    return l_value;
}

/**
 * @brief Phase 2.0.4: Create object value (MALLOC_MUTABLE mode)
 * @details Creates 8-byte value + separate storage structure
 */
dap_json_value_t *dap_json_value_v2_create_object(void)
{
    // Allocate 8-byte value
    dap_json_value_t *l_value = DAP_NEW_Z(dap_json_value_t);
    if (!l_value) {
        log_it(L_ERROR, "Failed to allocate 8-byte object value");
        return NULL;
    }
    
    // Allocate storage structure
    dap_json_object_storage_t *l_storage = DAP_NEW_Z(dap_json_object_storage_t);
    if (!l_storage) {
        log_it(L_ERROR, "Failed to allocate object storage");
        DAP_DELETE(l_value);
        return NULL;
    }
    
    // Initialize storage
    l_storage->capacity = INITIAL_OBJECT_CAPACITY;
    l_storage->count = 0;
    l_storage->keys = DAP_NEW_Z_COUNT(char*, INITIAL_OBJECT_CAPACITY);
    l_storage->values = DAP_NEW_Z_COUNT(dap_json_value_t*, INITIAL_OBJECT_CAPACITY);
    
    if (!l_storage->keys || !l_storage->values) {
        log_it(L_ERROR, "Failed to allocate object storage arrays");
        if (l_storage->keys) DAP_DELETE(l_storage->keys);
        if (l_storage->values) DAP_DELETE(l_storage->values);
        DAP_DELETE(l_storage);
        DAP_DELETE(l_value);
        return NULL;
    }
    
    // Setup 8-byte value
    l_value->type = DAP_JSON_TYPE_OBJECT;
    l_value->flags = 0;
    l_value->length = 0; // Will be updated on add
    l_value->offset = (uint32_t)(uintptr_t)l_storage; // Pointer as offset!
    
    return l_value;
}

/**
 * @brief Phase 2.0.4: Free JSON value recursively (MALLOC_MUTABLE mode)
 * @details Frees 8-byte value + associated storage structure
 * 
 * Used for manually created JSON objects (dap_json_object_new, etc.)
 * that were NOT allocated from Arena.
 */
void dap_json_value_v2_free(dap_json_value_t *a_value)
{
    if (!a_value) {
        return;
    }
    
    switch (a_value->type) {
        case DAP_JSON_TYPE_INT:
        case DAP_JSON_TYPE_UINT64: {
            // Check if allocated (length==1) or inline (length==0)
            if (a_value->length == 1 && a_value->offset) {
                // Allocated int64/uint64
                void *l_ptr = (void*)(uintptr_t)a_value->offset;
                DAP_DELETE(l_ptr);
            }
            // Inline ints (length==0): nothing to free
            break;
        }
        
        case DAP_JSON_TYPE_UINT256: {
            // uint256 always allocated
            if (a_value->offset) {
                uint256_t *l_ptr = (uint256_t*)(uintptr_t)a_value->offset;
                DAP_DELETE(l_ptr);
            }
            break;
        }
        
        case DAP_JSON_TYPE_DOUBLE: {
            // Doubles are always allocated (length==1)
            if (a_value->offset) {
                double *l_ptr = (double*)(uintptr_t)a_value->offset;
                DAP_DELETE(l_ptr);
            }
            break;
        }
        
        case DAP_JSON_TYPE_STRING: {
            // For MALLOC strings, offset points to malloc'd string data
            if (a_value->offset) {
                char *l_str = (char*)(uintptr_t)a_value->offset;
                DAP_DELETE(l_str);
            }
            break;
        }
        
        case DAP_JSON_TYPE_ARRAY: {
            // offset → array_storage
            dap_json_array_storage_t *l_storage = (dap_json_array_storage_t*)(uintptr_t)a_value->offset;
            if (l_storage) {
                // Recursively free all elements
                for (size_t i = 0; i < l_storage->count; i++) {
                    dap_json_value_v2_free(l_storage->elements[i]);
                }
                DAP_DELETE(l_storage->elements);
                DAP_DELETE(l_storage);
            }
            break;
        }
        
        case DAP_JSON_TYPE_OBJECT: {
            // offset → object_storage
            dap_json_object_storage_t *l_storage = (dap_json_object_storage_t*)(uintptr_t)a_value->offset;
            if (l_storage) {
                // Free all keys and values
                for (size_t i = 0; i < l_storage->count; i++) {
                    DAP_DELETE(l_storage->keys[i]);
                    dap_json_value_v2_free(l_storage->values[i]);
                }
                DAP_DELETE(l_storage->keys);
                DAP_DELETE(l_storage->values);
                DAP_DELETE(l_storage);
            }
            break;
        }
        
        default:
            // NULL, BOOL, NUMBER - no cleanup needed (all data in 8 bytes)
            break;
    }
    
    // Free the 8-byte value itself
    DAP_DELETE(a_value);
}

/* ========================================================================== */
/*                         ARRAY/OBJECT OPERATIONS                            */
/* ========================================================================== */

/**
 * @brief Phase 2.0.4: Add element to array (MALLOC_MUTABLE mode)
 * @details Works with storage structure
 */
bool dap_json_array_v2_add(dap_json_value_t *a_array, dap_json_value_t *a_element)
{
    if (!a_array || a_array->type != DAP_JSON_TYPE_ARRAY) {
        log_it(L_ERROR, "Invalid array");
        return false;
    }
    
    if (!a_element) {
        log_it(L_ERROR, "NULL element");
        return false;
    }
    
    // Get storage
    dap_json_array_storage_t *l_storage = (dap_json_array_storage_t*)(uintptr_t)a_array->offset;
    if (!l_storage) {
        log_it(L_ERROR, "Array has no storage");
        return false;
    }
    
    // Grow if needed
    if (l_storage->count >= l_storage->capacity) {
        size_t l_new_capacity = l_storage->capacity * ARRAY_GROWTH_FACTOR;
        dap_json_value_t **l_new_elements = DAP_REALLOC(
            l_storage->elements,
            l_new_capacity * sizeof(dap_json_value_t*)
        );
        
        if (!l_new_elements) {
            log_it(L_ERROR, "Failed to grow array to %zu elements", l_new_capacity);
            return false;
        }
        
        l_storage->elements = l_new_elements;
        l_storage->capacity = l_new_capacity;
    }
    
    // Add element
    l_storage->elements[l_storage->count++] = a_element;
    
    // Update length in 8-byte value
    a_array->length = (uint16_t)l_storage->count;
    
    return true;
}

/**
 * @brief Phase 2.0.4: Add key-value pair to object (MALLOC_MUTABLE mode)
 * @details Works with storage structure
 */
bool dap_json_object_v2_add(dap_json_value_t *a_object, const char *a_key, dap_json_value_t *a_value)
{
    if (!a_object || a_object->type != DAP_JSON_TYPE_OBJECT) {
        log_it(L_ERROR, "Invalid object");
        return false;
    }
    
    if (!a_key || !a_value) {
        log_it(L_ERROR, "NULL key or value");
        return false;
    }
    
    // Get storage
    dap_json_object_storage_t *l_storage = (dap_json_object_storage_t*)(uintptr_t)a_object->offset;
    if (!l_storage) {
        log_it(L_ERROR, "Object has no storage");
        return false;
    }
    
    // Check for duplicate key
    for (size_t i = 0; i < l_storage->count; i++) {
        if (strcmp(l_storage->keys[i], a_key) == 0) {
            log_it(L_WARNING, "Duplicate key: %s", a_key);
            return false;
        }
    }
    
    // Grow if needed
    if (l_storage->count >= l_storage->capacity) {
        size_t l_new_capacity = l_storage->capacity * OBJECT_GROWTH_FACTOR;
        
        char **l_new_keys = DAP_REALLOC(l_storage->keys, l_new_capacity * sizeof(char*));
        dap_json_value_t **l_new_values = DAP_REALLOC(l_storage->values, l_new_capacity * sizeof(dap_json_value_t*));
        
        if (!l_new_keys || !l_new_values) {
            log_it(L_ERROR, "Failed to grow object to %zu pairs", l_new_capacity);
            return false;
        }
        
        l_storage->keys = l_new_keys;
        l_storage->values = l_new_values;
        l_storage->capacity = l_new_capacity;
    }
    
    // Copy key
    size_t l_key_len = strlen(a_key);
    char *l_key_copy = DAP_NEW_Z_COUNT(char, l_key_len + 1);
    
    if (!l_key_copy) {
        log_it(L_ERROR, "Failed to allocate key copy");
        return false;
    }
    
    memcpy(l_key_copy, a_key, l_key_len + 1);
    
    // Add pair
    l_storage->keys[l_storage->count] = l_key_copy;
    l_storage->values[l_storage->count] = a_value;
    l_storage->count++;
    
    // Update length in 8-byte value
    a_object->length = (uint16_t)l_storage->count;
    
    return true;
}

/**
 * @brief Phase 2.0.4: REMOVED - Use dap_json_array_get_idx() with wrapper instead
 */
dap_json_value_t *dap_json_array_v2_get(const dap_json_value_t *a_array, size_t a_index)
{
    (void)a_array;
    (void)a_index;
    log_it(L_ERROR, "dap_json_array_v2_get: REMOVED in Phase 2.0.4 - use dap_json_array_get_idx() with wrapper");
    return NULL;
}

/**
 * @brief Phase 2.0.4: REMOVED - Use dap_json_object_get() with wrapper instead  
 */
dap_json_value_t *dap_json_object_v2_get(const dap_json_value_t *a_object, const char *a_key)
{
    (void)a_object;
    (void)a_key;
    log_it(L_ERROR, "dap_json_object_v2_get: REMOVED in Phase 2.0.4 - use dap_json_object_get() with wrapper");
    return NULL;
}

/* ========================================================================== */
/*                         VALUE PARSING HELPERS                              */
/* ========================================================================== */

/**
 * @brief Locale-independent strtod wrapper
 * @details JSON always uses '.' as decimal separator, regardless of system locale
 * @param a_str String to parse
 * @param a_endptr End pointer (like strtod)
 * @return Parsed double value
 */
static double s_strtod_c_locale(const char *a_str, char **a_endptr)
{
    // Save current locale
    char *l_old_locale = setlocale(LC_NUMERIC, NULL);
    if (l_old_locale) {
        l_old_locale = strdup(l_old_locale);
    }
    
    // Switch to "C" locale for parsing (uses '.' as decimal separator)
    setlocale(LC_NUMERIC, "C");
    
    // Parse with standard strtod
    double l_result = strtod(a_str, a_endptr);
    
    // Restore original locale
    if (l_old_locale) {
        setlocale(LC_NUMERIC, l_old_locale);
        free(l_old_locale);
    }
    
    return l_result;
}

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
/**
 * @brief Parse JSON number with FAST INTEGER PATH
 * @details Uses optimized multiply-add loop for integers (~5-10ns vs 100-200ns strtoll)
 *          Falls back to strtod for doubles (TODO: Eisel-Lemire for 3-5x speedup)
 * 
 * Performance:
 *   - Integers: ~10-20x faster than strtoll
 *   - Doubles: Same as before (strtod fallback)
 * 
 * Expected impact: +500-800% on number-heavy JSON
 * 
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_number(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    uint32_t a_end
)
{
    const uint8_t *a_input = a_stage2->input;
    
    if(!a_input || a_start >= a_end) {
        return -1;
    }
    
    // Work directly on input buffer (no string copy!)
    const char *l_num_str = (const char*)(a_input + a_start);
    size_t l_len = a_end - a_start;
    
    if(l_len >= 256) {
        log_it(L_ERROR, "Number too long: %zu bytes", l_len);
        return -1;
    }
    
    // Create number ref (lazy parsing!)
    dap_json_value_t l_number_ref = dap_json_from_number(
        (const char*)a_input,
        a_start,
        l_len,
        false  // is_integer - lazy determination
    );
    
    int32_t ref_index = s_stage2_add_ref(a_stage2, &l_number_ref);
    if (ref_index < 0) {
        log_it(L_ERROR, "Failed to add number ref");
        return -1;
    }
    
    a_stage2->numbers_created++;
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "Phase 2.0.3: Number ref[%d] - offset=%u, length=%zu (LAZY parse)",
             ref_index, a_start, l_len);
    
    return ref_index;
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
                    
                    // Check for surrogate pairs (U+D800..U+DFFF)
                    if(l_codepoint >= 0xD800 && l_codepoint <= 0xDBFF) {
                        // High surrogate - must be followed by low surrogate
                        if(i + 6 >= a_length || a_input[i + 1] != '\\' || a_input[i + 2] != 'u') {
                            log_it(L_ERROR, "Unpaired high surrogate U+%04X", l_codepoint);
                            DAP_DELETE(l_output);
                            return false;
                        }
                        
                        // Parse low surrogate
                        i += 2; // Skip \u
                        uint32_t l_low_surrogate = 0;
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
                                log_it(L_ERROR, "Invalid hex digit in low surrogate: %c", l_c);
                                DAP_DELETE(l_output);
                                return false;
                            }
                            
                            l_low_surrogate = (l_low_surrogate << 4) | l_digit;
                        }
                        
                        i += 4; // Skip hex digits
                        
                        // Validate low surrogate range
                        if(l_low_surrogate < 0xDC00 || l_low_surrogate > 0xDFFF) {
                            log_it(L_ERROR, "Invalid low surrogate U+%04X (expected U+DC00..U+DFFF)", l_low_surrogate);
                            DAP_DELETE(l_output);
                            return false;
                        }
                        
                        // Decode surrogate pair to codepoint
                        // Formula: (high - 0xD800) * 0x400 + (low - 0xDC00) + 0x10000
                        l_codepoint = ((l_codepoint - 0xD800) << 10) + (l_low_surrogate - 0xDC00) + 0x10000;
                    }
                    else if(l_codepoint >= 0xDC00 && l_codepoint <= 0xDFFF) {
                        // Lone low surrogate - invalid
                        log_it(L_ERROR, "Unpaired low surrogate U+%04X", l_codepoint);
                        DAP_DELETE(l_output);
                        return false;
                    }
                    // else: regular codepoint (not surrogate)
                    
                    // Encode codepoint as UTF-8
                    if(l_codepoint <= 0x7F) {
                        // 1-byte UTF-8: 0xxxxxxx
                        l_output[l_out_pos++] = (char)l_codepoint;
                    }
                    else if(l_codepoint <= 0x7FF) {
                        // 2-byte UTF-8: 110xxxxx 10xxxxxx
                        l_output[l_out_pos++] = (char)(0xC0 | (l_codepoint >> 6));
                        l_output[l_out_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    else if(l_codepoint <= 0xFFFF) {
                        // 3-byte UTF-8: 1110xxxx 10xxxxxx 10xxxxxx
                        l_output[l_out_pos++] = (char)(0xE0 | (l_codepoint >> 12));
                        l_output[l_out_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_out_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    else if(l_codepoint <= 0x10FFFF) {
                        // 4-byte UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                        l_output[l_out_pos++] = (char)(0xF0 | (l_codepoint >> 18));
                        l_output[l_out_pos++] = (char)(0x80 | ((l_codepoint >> 12) & 0x3F));
                        l_output[l_out_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_out_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    }
                    else {
                        // Invalid codepoint (> U+10FFFF)
                        log_it(L_ERROR, "Invalid Unicode codepoint U+%06X", l_codepoint);
                        DAP_DELETE(l_output);
                        return false;
                    }
                    
                    break;
                }
                
                default:
                    log_it(L_ERROR, "Invalid escape sequence: \\%c", a_input[i]);
                    DAP_DELETE(l_output);
                    return false;
            }
        }
        else {
            // Check for unescaped control characters (U+0000..U+001F)
            // RFC 8259: control characters MUST be escaped
            if(a_input[i] < 0x20) {
                log_it(L_ERROR, "Unescaped control character 0x%02X in string", (unsigned char)a_input[i]);
                DAP_DELETE(l_output);
                return false;
            }
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
/**
 * @brief Parse JSON string using ZERO-COPY SIMD scanner
 * @details Uses dap_json_string_scan() with CPU dispatch (SSE2/AVX2/AVX-512/NEON)
 *          - Strings without escapes: zero-copy (just pointer into JSON buffer)
 *          - Strings with escapes: lazy unescaping on first access
 * 
 * Performance: ~16-64x faster than character-by-character (depending on CPU)
 * 
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_string(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    uint32_t *a_out_end_offset
)
{
    const uint8_t *a_input = a_stage2->input;
    size_t a_input_len = a_stage2->input_len;
    
    if(!a_input || !a_out_end_offset) {
        return -1;
    }
    
    if(a_start >= a_input_len || a_input[a_start] != '"') {
        log_it(L_ERROR, "Expected opening quote at offset %u", a_start);
        return -1;
    }
    
    // ZERO-COPY SIMD STRING SCANNER (CPU dispatch: AVX2/SSE2/NEON/Reference)
    dap_json_string_zc_t l_scanned_string;
    uint32_t l_end_offset = 0;
    
    if (!dap_json_string_scan_ref(
        a_input + a_start,
        a_input_len - a_start,
        &l_scanned_string,
        &l_end_offset
    )) {
        log_it(L_ERROR, "Failed to scan string at offset %u", a_start);
        return -1;
    }
    
    // Calculate offset to string data (after opening quote)
    ptrdiff_t data_offset = l_scanned_string.data - (const char*)a_input;
    
    if (data_offset < 0 || (size_t)data_offset >= a_input_len) {
        log_it(L_ERROR, "String data outside input buffer");
        return -1;
    }
    
    // Create string ref (8 bytes, zero-copy!)
    dap_json_value_t l_string_ref = dap_json_from_string(
        (const char*)a_input,
        (uint32_t)data_offset,
        l_scanned_string.length,
        l_scanned_string.needs_unescape
    );
    
    // Add ref to Stage 2 refs array
    int32_t ref_index = s_stage2_add_ref(a_stage2, &l_string_ref);
    if (ref_index < 0) {
        log_it(L_ERROR, "Failed to add string ref");
        return -1;
    }
    
    a_stage2->strings_created++;
    *a_out_end_offset = a_start + l_end_offset;
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "Phase 2.0.3: String ref[%d] - offset=%u, length=%zu, escapes=%d",
             ref_index, (uint32_t)data_offset, l_scanned_string.length, 
             l_scanned_string.needs_unescape);
    
    return ref_index;
}

/**
 * @brief Parse literal (true, false, null)
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_literal(
    dap_json_stage2_t *a_stage2,
    uint32_t a_start,
    uint32_t *a_out_end_offset
)
{
    const uint8_t *a_input = a_stage2->input;
    size_t a_input_len = a_stage2->input_len;
    
    if(!a_input || !a_out_end_offset) {
        return -1;
    }
    
    if(a_start >= a_input_len) {
        return -1;
    }
    
    // Check for "true"
    if(a_start + 4 <= a_input_len &&
       memcmp(a_input + a_start, "true", 4) == 0) {
        dap_json_value_t l_bool_ref = dap_json_from_boolean(
            (const char*)a_input,
            a_start,
            true
        );
        int32_t ref_index = s_stage2_add_ref(a_stage2, &l_bool_ref);
        *a_out_end_offset = a_start + 4;
        return ref_index;
    }
    
    // Check for "false"
    if(a_start + 5 <= a_input_len &&
       memcmp(a_input + a_start, "false", 5) == 0) {
        dap_json_value_t l_bool_ref = dap_json_from_boolean(
            (const char*)a_input,
            a_start,
            false
        );
        int32_t ref_index = s_stage2_add_ref(a_stage2, &l_bool_ref);
        *a_out_end_offset = a_start + 5;
        return ref_index;
    }
    
    // Check for "null"
    if(a_start + 4 <= a_input_len &&
       memcmp(a_input + a_start, "null", 4) == 0) {
        dap_json_value_t l_null_ref = dap_json_from_null(
            (const char*)a_input,
            a_start
        );
        int32_t ref_index = s_stage2_add_ref(a_stage2, &l_null_ref);
        *a_out_end_offset = a_start + 4;
        return ref_index;
    }
    
    log_it(L_ERROR, "Invalid literal at offset %u", a_start);
    return -1;
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
    
    // Phase 2.1: Predictive pre-allocation based on Stage 1 token counts
    // Get token counts from Stage 1 for accurate memory sizing
    size_t l_string_count = 0, l_number_count = 0, l_literal_count = 0;
    size_t l_array_count = 0, l_object_count = 0;
    
    dap_json_stage1_get_token_counts(a_stage1,
                                      &l_string_count,
                                      &l_number_count,
                                      &l_literal_count,
                                      &l_array_count,
                                      &l_object_count);
    
    // Calculate Arena size based on actual token types:
    // Phase 2.0.3: Updated for 8-byte refs (vs old 80-byte values)
    // - Each value:  sizeof(dap_json_value_t) = 8 bytes (vs 80)
    // - Each array:  + capacity * 8 bytes (refs, not pointers)
    // - Each object: + capacity * 16 bytes (ref pairs)
    size_t l_total_values = l_string_count + l_number_count + l_literal_count + 
                            l_array_count + l_object_count;
    
    size_t l_estimated_size = 
        (l_total_values * 8) +                               // Value refs (8 bytes each!)
        (l_array_count * INITIAL_ARRAY_CAPACITY * 8) +      // Array storage (refs)
        (l_object_count * INITIAL_OBJECT_CAPACITY * 16);    // Object storage (ref pairs)
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "Phase 2.0.3: Pre-allocation (8-byte refs) BEFORE cap - strings:%zu numbers:%zu literals:%zu arrays:%zu objects:%zu → arena:%zu bytes",
             l_string_count, l_number_count, l_literal_count, l_array_count, l_object_count,
             l_estimated_size);
    
    // ⚡ ОПТИМИЗАЦИЯ: Более агрессивный cap для экономии памяти
    // Для JSON <1KB: максимум 16KB арены
    // Для JSON <100KB: максимум 512KB арены
    // Для JSON >100KB: cap на 4x от размера JSON
    const size_t MAX_PREALLOC_SMALL = 16 * 1024;   // 16 KB для маленьких JSON
    const size_t MAX_PREALLOC_MEDIUM = 512 * 1024; // 512 KB для средних JSON
    const size_t MAX_PREALLOC_LARGE = a_stage1->input_len * 4; // 4x от размера JSON
    
    if (a_stage1->input_len < 1024 && l_estimated_size > MAX_PREALLOC_SMALL) {
        log_it(L_DEBUG, "Pre-allocation capped (small JSON): requested=%zu KB, capped to=%zu KB", 
               l_estimated_size / 1024, MAX_PREALLOC_SMALL / 1024);
        l_estimated_size = MAX_PREALLOC_SMALL;
    } else if (a_stage1->input_len < 102400 && l_estimated_size > MAX_PREALLOC_MEDIUM) {
        log_it(L_DEBUG, "Pre-allocation capped (medium JSON): requested=%zu KB, capped to=%zu KB", 
               l_estimated_size / 1024, MAX_PREALLOC_MEDIUM / 1024);
        l_estimated_size = MAX_PREALLOC_MEDIUM;
    } else if (l_estimated_size > MAX_PREALLOC_LARGE) {
        log_it(L_DEBUG, "Pre-allocation capped (large JSON): requested=%zu MB, capped to=%zu MB", 
               l_estimated_size / (1024*1024), MAX_PREALLOC_LARGE / (1024*1024));
        l_estimated_size = MAX_PREALLOC_LARGE;
    }
    
    // Minimum 4KB, round up to 4KB boundary for efficiency
    if (l_estimated_size < 4096) {
        l_estimated_size = 4096;
    } else {
        l_estimated_size = ((l_estimated_size + 4095) / 4096) * 4096;
    }
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "Phase 2.1: Pre-allocation - strings:%zu numbers:%zu literals:%zu arrays:%zu objects:%zu → arena:%zu bytes",
             l_string_count, l_number_count, l_literal_count, l_array_count, l_object_count,
             l_estimated_size);
    
    // ⭐ Reuse thread-local refcounted arena (create if first time)
    if (!s_thread_json_arena) {
        s_thread_json_arena = dap_arena_new_opt((dap_arena_opt_t){
            .use_refcount = true,
            .initial_size = l_estimated_size,
            .max_page_size = l_estimated_size * 2,  // ⚠️ Cap page growth
            .thread_local = true
        });
        if (!s_thread_json_arena) {
            log_it(L_ERROR, "Failed to create thread-local refcounted Arena");
            DAP_DELETE(l_stage2);
            return NULL;
        }
        debug_if(s_debug_more, L_DEBUG, "Created thread-local refcounted arena (size: %zu)", l_estimated_size);
    }
    // ⚠️ DON'T reset arena here - string_pool_clear will do it!
    
    // ⭐ Reuse thread-local string pool (create if first time)
    size_t l_string_pool_capacity = a_stage1->indices_count / 4;
    if (l_string_pool_capacity < 32) {
        l_string_pool_capacity = 32;
    }
    
    if (!s_thread_string_pool) {
        s_thread_string_pool = dap_string_pool_new(s_thread_json_arena, l_string_pool_capacity);
        if (!s_thread_string_pool) {
            log_it(L_ERROR, "Failed to create thread-local String Pool");
            DAP_DELETE(l_stage2);
            return NULL;
        }
        debug_if(s_debug_more, L_DEBUG, "Created thread-local string pool (capacity: %zu)", l_string_pool_capacity);
    } else {
        // Clear string pool for reuse (это также сбросит арену!)
        dap_string_pool_clear(s_thread_string_pool);
        debug_if(s_debug_more, L_DEBUG, "Reusing thread-local string pool (cleared, arena reset)");
    }
    
    // ⭐ Stage2 теперь просто ссылается на thread-local ресурсы, не владеет ими
    l_stage2->arena = s_thread_json_arena;
    l_stage2->string_pool = s_thread_string_pool;
    
    // ⭐ Phase 2.0.3: Initialize flat refs array for 8-byte values
    l_stage2->values_capacity = l_total_values * 2;  // 2x buffer for safety
    if (l_stage2->values_capacity < 64) {
        l_stage2->values_capacity = 64;  // Minimum capacity
    }
    
    l_stage2->values = (dap_json_value_t*)dap_arena_alloc(
        l_stage2->arena,
        sizeof(dap_json_value_t) * l_stage2->values_capacity
    );
    
    if (!l_stage2->values) {
        log_it(L_ERROR, "Failed to allocate refs array (%zu refs = %zu bytes)",
               l_stage2->values_capacity, 
               sizeof(dap_json_value_t) * l_stage2->values_capacity);
        DAP_DELETE(l_stage2);
        return NULL;
    }
    
    l_stage2->values_count = 0;
    l_stage2->root_value_index = 0;
    
    debug_if(s_debug_more, L_DEBUG, 
             "Phase 2.0.3: Allocated refs array - capacity:%zu refs (%zu bytes)",
             l_stage2->values_capacity,
             sizeof(dap_json_value_t) * l_stage2->values_capacity);
    
    debug_if(s_debug_more, L_DEBUG, "Stage 2 initialized with %zu indices, Arena: %zu bytes, String Pool: %zu capacity", 
           l_stage2->indices_count, l_estimated_size, l_string_pool_capacity);
    
    return l_stage2;
}

/**
 * @brief Materialize ref into old-style value (ONLY for PUBLIC API!)
 * 
 * Phase 2.0.3: This is the ONLY place where we convert internal refs
 * back to old-style dap_json_value_t for API compatibility.
 * 
 * This is a MINIMAL wrapper - just type and ref_index, NO data copy!
 */
static dap_json_value_t *s_materialize_ref_to_value(
    dap_json_stage2_t *a_stage2,
    uint32_t ref_index
)
{
    if (ref_index >= a_stage2->values_count) {
        log_it(L_ERROR, "Invalid ref_index: %u (max: %zu)", ref_index, a_stage2->values_count);
        return NULL;
    }
    
    dap_json_value_t *ref = &a_stage2->values[ref_index];
    
    // Create minimal wrapper
    dap_json_value_t *value = s_create_value_arena(a_stage2->arena);
    if (!value) {
        return NULL;
    }
    
    value->type = (dap_json_type_t)ref->type;
    
    // NO data initialization! Everything stays in the ref!
    // API layer will access data through ref when needed
    
    return value;
}

/**
 * @brief Get root value
 * 
 * Phase 2.0.3: Creates a wrapper from root ref for PUBLIC API
 */
dap_json_value_t *dap_json_stage2_get_root(const dap_json_stage2_t *a_stage2)
{
    if(!a_stage2) {
        return NULL;
    }
    
    // Phase 2.0.3: Materialize root ref into wrapper
    // This is the ONLY conversion point from refs to old API!
    if (a_stage2->root_value_index < a_stage2->values_count) {
        return s_materialize_ref_to_value(
            (dap_json_stage2_t*)a_stage2,  // Cast away const
            a_stage2->root_value_index
        );
    }
    
    return NULL;
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
    
    debug_if(s_debug_more, L_DEBUG, "Stage 2 free: start");
    
    // Free transcoded buffer (if any)
    if (a_stage2->transcoded_buffer) {
        debug_if(s_debug_more, L_DEBUG, "Stage 2 free: freeing transcoded buffer at %p", a_stage2->transcoded_buffer);
        DAP_DELETE(a_stage2->transcoded_buffer);
        debug_if(s_debug_more, L_DEBUG, "Stage 2 free: transcoded buffer freed");
    }
    
    // ⭐ DO NOT free thread-local arena (it's reused across parses)
    // Arena will be reset on next parse via dap_arena_reset()
    // String Pool also uses arena memory, so no need to free it separately
    debug_if(s_debug_more, L_DEBUG, "Stage 2 free: arena is thread-local, not freed (will be reused)");
    
    // NOTE: root pointer and all values remain valid until next parse or thread exit
    // Caller should keep ref'd borrowed references alive via dap_arena_page_ref()
    DAP_DELETE(a_stage2);
    debug_if(s_debug_more, L_DEBUG, "Stage 2 free: complete");
}

/* Forward declaration для recursive parsing */
static int32_t s_parse_value(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
);

/**
 * @brief Parse array value recursively
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_array(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    // Check depth
    if(a_stage2->current_depth >= a_stage2->max_depth) {
        snprintf(a_stage2->error_message, sizeof(a_stage2->error_message),
                 "Maximum nesting depth (%zu) exceeded", a_stage2->max_depth);
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
        return -1;
    }
    
    a_stage2->current_depth++;
    
    // Track array elements as ref indices
    uint32_t *element_ref_indices = NULL;
    size_t element_count = 0;
    size_t element_capacity = INITIAL_ARRAY_CAPACITY;
    
    // Pre-allocate element indices array in Arena
    element_ref_indices = (uint32_t*)dap_arena_alloc(
        a_stage2->arena,
        sizeof(uint32_t) * element_capacity
    );
    
    if (!element_ref_indices) {
        a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
        a_stage2->current_depth--;
        return -1;
    }
    
    a_stage2->arrays_created++;
    (*a_idx)++; // Skip '['
    
    // Check for empty array
    if(*a_idx < a_stage2->indices_count && 
       a_stage2->indices[*a_idx].character == ']') {
        (*a_idx)++; // Skip ']'
        a_stage2->current_depth--;
        
        // Create empty array ref
        dap_json_value_t l_array_ref = dap_json_array_create(NULL, 0);
        return s_stage2_add_ref(a_stage2, &l_array_ref);
    }
    
    // Parse elements
    while(*a_idx < a_stage2->indices_count) {
        // Parse value - returns ref_index!
        int32_t element_ref_idx = s_parse_value(a_stage2, a_idx);
        if(element_ref_idx < 0) {
            a_stage2->current_depth--;
            return -1;
        }
        
        // Grow if needed
        if (element_count >= element_capacity) {
            size_t new_capacity = element_capacity * 2;
            uint32_t *new_indices = (uint32_t*)dap_arena_alloc(
                a_stage2->arena,
                sizeof(uint32_t) * new_capacity
            );
            
            if (!new_indices) {
                a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
                a_stage2->current_depth--;
                return -1;
            }
            
            memcpy(new_indices, element_ref_indices, sizeof(uint32_t) * element_count);
            element_ref_indices = new_indices;
            element_capacity = new_capacity;
        }
        
        element_ref_indices[element_count++] = (uint32_t)element_ref_idx;
        
        // Check next: ',' or ']'
        if(*a_idx >= a_stage2->indices_count) {
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
            a_stage2->current_depth--;
            return -1;
        }
        
        uint8_t l_next = a_stage2->indices[*a_idx].character;
        
        if(l_next == ']') {
            (*a_idx)++; // Skip ']'
            a_stage2->current_depth--;
            
            // Create array ref with flat ref indices
            dap_json_value_t l_array_ref = dap_json_array_create(
                element_ref_indices,
                element_count
            );
            
            int32_t array_ref_idx = s_stage2_add_ref(a_stage2, &l_array_ref);
            
            debug_if(dap_json_get_debug(), L_DEBUG,
                     "Phase 2.0.3: Array ref[%d] with %zu elements",
                     array_ref_idx, element_count);
            
            return array_ref_idx;
        }
        else if(l_next == ',') {
            (*a_idx)++; // Skip ','
            continue;
        }
        else {
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->current_depth--;
            return -1;
        }
    }
    
    a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
    a_stage2->current_depth--;
    return -1;
}

/**
 * @brief Parse object value recursively
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_object(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    // Check depth
    if(a_stage2->current_depth >= a_stage2->max_depth) {
        snprintf(a_stage2->error_message, sizeof(a_stage2->error_message),
                 "Maximum nesting depth (%zu) exceeded", a_stage2->max_depth);
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
        return -1;
    }
    
    a_stage2->current_depth++;
    
    // Track object pairs as ref indices [key, val, key, val, ...]
    uint32_t *pair_ref_indices = NULL;
    size_t pair_count = 0;
    size_t pair_capacity = INITIAL_OBJECT_CAPACITY;
    
    // Pre-allocate pair indices array in Arena (2 indices per pair)
    pair_ref_indices = (uint32_t*)dap_arena_alloc(
        a_stage2->arena,
        sizeof(uint32_t) * pair_capacity * 2
    );
    
    if (!pair_ref_indices) {
        a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
        a_stage2->current_depth--;
        return -1;
    }
    
    a_stage2->objects_created++;
    (*a_idx)++; // Skip '{'
    
    // Check for empty object
    if(*a_idx < a_stage2->indices_count && 
       a_stage2->indices[*a_idx].character == '}') {
        (*a_idx)++; // Skip '}'
        a_stage2->current_depth--;
        
        // Create empty object ref
        dap_json_value_t l_object_ref = dap_json_object_create(NULL, 0);
        return s_stage2_add_ref(a_stage2, &l_object_ref);
    }
    
    // Parse key-value pairs
    while(*a_idx < a_stage2->indices_count) {
        // Parse key (must be string)
        uint32_t l_key_offset = a_stage2->indices[*a_idx].position;
        
        if(a_stage2->input[l_key_offset] != '"') {
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->error_position = l_key_offset;
            a_stage2->current_depth--;
            return -1;
        }
        
        uint32_t l_key_end = 0;
        
        int32_t key_ref_idx = s_parse_string(a_stage2, l_key_offset, &l_key_end);
        if(key_ref_idx < 0) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_STRING;
            a_stage2->error_position = l_key_offset;
            a_stage2->current_depth--;
            return -1;
        }
        
        (*a_idx)++; // Skip key string index
        
        // Expect ':'
        if(*a_idx >= a_stage2->indices_count ||
           a_stage2->indices[*a_idx].character != ':') {
            a_stage2->error_code = STAGE2_ERROR_MISSING_COLON;
            a_stage2->current_depth--;
            return -1;
        }
        
        (*a_idx)++; // Skip ':'
        
        // Parse value
        int32_t value_ref_idx = s_parse_value(a_stage2, a_idx);
        if(value_ref_idx < 0) {
            a_stage2->current_depth--;
            return -1;
        }
        
        // Grow if needed
        if (pair_count >= pair_capacity) {
            size_t new_capacity = pair_capacity * 2;
            uint32_t *new_indices = (uint32_t*)dap_arena_alloc(
                a_stage2->arena,
                sizeof(uint32_t) * new_capacity * 2
            );
            
            if (!new_indices) {
                a_stage2->error_code = STAGE2_ERROR_OUT_OF_MEMORY;
                a_stage2->current_depth--;
                return -1;
            }
            
            memcpy(new_indices, pair_ref_indices, sizeof(uint32_t) * pair_count * 2);
            pair_ref_indices = new_indices;
            pair_capacity = new_capacity;
        }
        
        // Store [key_ref, value_ref] pair
        pair_ref_indices[pair_count * 2 + 0] = (uint32_t)key_ref_idx;
        pair_ref_indices[pair_count * 2 + 1] = (uint32_t)value_ref_idx;
        pair_count++;
        
        // Check next: ',' or '}'
        if(*a_idx >= a_stage2->indices_count) {
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
            a_stage2->current_depth--;
            return -1;
        }
        
        uint8_t l_next = a_stage2->indices[*a_idx].character;
        
        if(l_next == '}') {
            (*a_idx)++; // Skip '}'
            a_stage2->current_depth--;
            
            // Create object ref with flat ref pairs
            dap_json_value_t l_object_ref = dap_json_object_create(
                pair_ref_indices,
                pair_count
            );
            
            int32_t object_ref_idx = s_stage2_add_ref(a_stage2, &l_object_ref);
            
            debug_if(dap_json_get_debug(), L_DEBUG,
                     "Phase 2.0.3: Object ref[%d] with %zu pairs",
                     object_ref_idx, pair_count);
            
            return object_ref_idx;
        }
        else if(l_next == ',') {
            (*a_idx)++; // Skip ','
            continue;
        }
        else {
            a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
            a_stage2->current_depth--;
            return -1;
        }
    }
    
    a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
    a_stage2->current_depth--;
    return -1;
}

/**
 * @brief Parse value (any JSON value)
 * @return ref_index in Stage 2 refs array, or -1 on error
 */
static int32_t s_parse_value(
    dap_json_stage2_t *a_stage2,
    size_t *a_idx
)
{
    if(*a_idx >= a_stage2->indices_count) {
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_END;
        return -1;
    }
    
    uint32_t l_offset = a_stage2->indices[*a_idx].position;
    uint8_t l_type = a_stage2->indices[*a_idx].character;
    uint8_t l_char = a_stage2->input[l_offset];
    
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
        int32_t ref_idx = s_parse_string(a_stage2, l_offset, &l_end_offset);
        if(ref_idx < 0) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_STRING;
            a_stage2->error_position = l_offset;
            return -1;
        }
        (*a_idx)++;
        return ref_idx;
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
        
        int32_t ref_idx = s_parse_number(a_stage2, l_offset, l_number_end);
        if(ref_idx < 0) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_NUMBER;
            a_stage2->error_position = l_offset;
            return -1;
        }
        (*a_idx)++;
        return ref_idx;
    }
    
    // Literal (true, false, null)
    else {
        int32_t ref_idx = s_parse_literal(a_stage2, l_offset, &l_end_offset);
        if(ref_idx < 0) {
            a_stage2->error_code = STAGE2_ERROR_INVALID_LITERAL;
            a_stage2->error_position = l_offset;
            return -1;
        }
        (*a_idx)++;
        return ref_idx;
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
    
    debug_if(s_debug_more, L_DEBUG, "Starting Stage 2 DOM building...");
    
    size_t l_idx = 0;
    int32_t root_ref_idx = s_parse_value(a_stage2, &l_idx);
    
    if(root_ref_idx < 0) {
        log_it(L_ERROR, "Stage 2 parsing failed: %s (position %zu)",
               dap_json_stage2_error_to_string(a_stage2->error_code),
               a_stage2->error_position);
        return a_stage2->error_code;
    }
    
    // ⭐ Phase 2.0.3: Store root ref index
    a_stage2->root_value_index = (uint32_t)root_ref_idx;
    a_stage2->root = NULL;  // No old-style root! Only ref_index!
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "Phase 2.0.3: Root ref_index = %u", a_stage2->root_value_index);
    
    // Check for trailing garbage
    if(l_idx < a_stage2->indices_count) {
        log_it(L_ERROR, "Trailing garbage detected: %zu unused structural indices (used %zu of %zu)",
               a_stage2->indices_count - l_idx, l_idx, a_stage2->indices_count);
        a_stage2->error_code = STAGE2_ERROR_UNEXPECTED_TOKEN;
        a_stage2->error_position = (l_idx < a_stage2->indices_count) ? 
                                   a_stage2->indices[l_idx].position : a_stage2->input_len;
        return a_stage2->error_code;
    }
    
    debug_if(s_debug_more, L_INFO, "Stage 2 completed: objects=%zu arrays=%zu strings=%zu numbers=%zu, refs=%zu",
           a_stage2->objects_created, a_stage2->arrays_created,
           a_stage2->strings_created, a_stage2->numbers_created, a_stage2->values_count);
    
    a_stage2->error_code = STAGE2_SUCCESS;
    return STAGE2_SUCCESS;
}

