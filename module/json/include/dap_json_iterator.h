/**
 * @file dap_json_iterator.h
 * @brief DAP JSON Iterator API - Zero-copy tape traversal
 * @details  High-performance iterator for tape format
 * 
 * This API is the key to achieving top-tier JSON parsing performance!
 * 
 * Key Features:
 * - **Zero-copy strings**: Return (pointer, length) into input buffer
 * - **Lazy parsing**: Numbers parsed only when accessed
 * - **O(1) skip**: Jump over containers without traversing
 * - **Cache-friendly**: Sequential tape access
 * - **Arena-based**: Zero malloc overhead
 * 
 * Usage Example:
 * ```c
 * dap_json_t *json = dap_json_parse_buffer(input, len);
 * dap_json_iterator_t *iter = dap_json_iterator_new(json);
 * 
 * if (dap_json_iterator_enter(iter)) {  // Enter root object
 *     while (dap_json_iterator_next(iter)) {
 *         const char *key;
 *         size_t key_len;
 *         dap_json_iterator_get_string(iter, &key, &key_len);
 *         
 *         dap_json_iterator_next(iter);  // Move to value
 *         // ... process value ...
 *     }
 * }
 * 
 * dap_json_iterator_free(iter);
 * ```
 * 
 * @author DAP SDK Team
 * @date 2026-01-20
 */

#ifndef DAP_JSON_ITERATOR_H
#define DAP_JSON_ITERATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "dap_json_type.h"

// Forward declarations
typedef struct dap_json dap_json_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           ITERATOR STRUCTURE                               */
/* ========================================================================== */

/**
 * @brief Forward declaration
 */
typedef struct dap_json_iterator dap_json_iterator_t;

/* ========================================================================== */
/*                         ITERATOR LIFECYCLE                                 */
/* ========================================================================== */

/**
 * @brief Create iterator from parsed JSON
 * @param[in] json Parsed JSON object (must have tape)
 * @return Iterator, or NULL on error
 */
dap_json_iterator_t* dap_json_iterator_new(dap_json_t *json);

/**
 * @brief Free iterator
 * @param[in] iter Iterator to free
 */
void dap_json_iterator_free(dap_json_iterator_t *iter);

/**
 * @brief Reset iterator to beginning
 * @param[in] iter Iterator
 */
void dap_json_iterator_reset(dap_json_iterator_t *iter);

/* ========================================================================== */
/*                         ITERATOR NAVIGATION                                */
/* ========================================================================== */

/**
 * @brief Get current value type
 * @param[in] iter Iterator
 * @return Current value type
 */
dap_json_type_t dap_json_iterator_type(const dap_json_iterator_t *iter);

/**
 * @brief Move to next value
 * @details Moves to next value at current depth.
 *          For containers, moves to NEXT sibling (not into container).
 *          Use enter() to descend into containers.
 * 
 * @param[in] iter Iterator
 * @return true if moved, false if at end
 */
bool dap_json_iterator_next(dap_json_iterator_t *iter);

/**
 * @brief Skip current value (O(1) for containers)
 * @details Uses jump pointers to skip entire containers without traversal
 * 
 * @param[in] iter Iterator
 * @return true if skipped, false if at end
 */
bool dap_json_iterator_skip(dap_json_iterator_t *iter);

/**
 * @brief Enter container (object/array)
 * @details Moves iterator inside container to first element
 * 
 * @param[in] iter Iterator
 * @return true if entered, false if current value is not a container
 */
bool dap_json_iterator_enter(dap_json_iterator_t *iter);

/**
 * @brief Exit current container
 * @details Moves iterator to position after container close
 * 
 * @param[in] iter Iterator
 * @return true if exited, false if at root level
 */
bool dap_json_iterator_exit(dap_json_iterator_t *iter);

/**
 * @brief Check if at end of current container/document
 * @param[in] iter Iterator
 * @return true if at end
 */
bool dap_json_iterator_at_end(const dap_json_iterator_t *iter);

/* ========================================================================== */
/*                      VALUE ACCESSORS (ZERO-COPY)                           */
/* ========================================================================== */

/**
 * @brief Get string value as (pointer, length) - ZERO COPY!
 * @details THE killer feature! No allocation, no copy, just pointer math.
 * 
 * Returned pointer points DIRECTLY into input buffer.
 * Valid until input buffer is freed.
 * 
 * @param[in] iter Iterator
 * @param[out] out_str Pointer to receive string pointer
 * @param[out] out_len Pointer to receive string length
 * @return true if current value is string
 */
bool dap_json_iterator_get_string(
    const dap_json_iterator_t *iter,
    const char **out_str,
    size_t *out_len
);

/**
 * @brief Get string value (materialized copy)
 * @details For backward compatibility. Allocates and copies string.
 *          Caller must free() the result.
 * 
 * @param[in] iter Iterator
 * @return Allocated string, or NULL if not a string
 */
char* dap_json_iterator_get_string_dup(const dap_json_iterator_t *iter);

/**
 * @brief Get int64 value - LAZY PARSE!
 * @details Number is parsed from input buffer on-demand.
 *          No allocation, no pre-parsing overhead.
 * 
 * @param[in] iter Iterator
 * @param[out] out Pointer to receive int64 value
 * @return true if current value is number and parsed successfully
 */
bool dap_json_iterator_get_int64(const dap_json_iterator_t *iter, int64_t *out);

/**
 * @brief Get uint64 value - LAZY PARSE!
 * @param[in] iter Iterator
 * @param[out] out Pointer to receive uint64 value
 * @return true if current value is number and parsed successfully
 */
bool dap_json_iterator_get_uint64(const dap_json_iterator_t *iter, uint64_t *out);

/**
 * @brief Get double value - LAZY PARSE!
 * @param[in] iter Iterator
 * @param[out] out Pointer to receive double value
 * @return true if current value is number and parsed successfully
 */
bool dap_json_iterator_get_double(const dap_json_iterator_t *iter, double *out);

/**
 * @brief Get boolean value
 * @param[in] iter Iterator
 * @param[out] out Pointer to receive boolean value
 * @return true if current value is boolean
 */
bool dap_json_iterator_get_bool(const dap_json_iterator_t *iter, bool *out);

/**
 * @brief Check if current value is null
 * @param[in] iter Iterator
 * @return true if current value is null
 */
bool dap_json_iterator_is_null(const dap_json_iterator_t *iter);

/* ========================================================================== */
/*                      OBJECT/ARRAY HELPERS                                  */
/* ========================================================================== */

/**
 * @brief Find object key (case-sensitive)
 * @details Scans current object for matching key.
 *          Moves iterator to key's value if found.
 * 
 * Performance: O(n) but cache-friendly (sequential tape scan)
 * 
 * @param[in] iter Iterator (must be inside object)
 * @param[in] key Key to find
 * @param[in] key_len Key length
 * @return true if found (iterator moved to value), false otherwise
 */
bool dap_json_iterator_find_key(
    dap_json_iterator_t *iter,
    const char *key,
    size_t key_len
);

/**
 * @brief Find object key (null-terminated convenience wrapper)
 * @param[in] iter Iterator
 * @param[in] key Null-terminated key
 * @return true if found
 */
static inline bool dap_json_iterator_find_key_str(
    dap_json_iterator_t *iter,
    const char *key
)
{
    return dap_json_iterator_find_key(iter, key, strlen(key));
}

/**
 * @brief Get array length
 * @details Counts elements in current array (O(n) scan)
 * 
 * @param[in] iter Iterator (must be at array)
 * @return Array length, or 0 if not an array
 */
size_t dap_json_iterator_array_length(const dap_json_iterator_t *iter);

/**
 * @brief Get object size (number of key-value pairs)
 * @details Counts pairs in current object (O(n) scan)
 * 
 * @param[in] iter Iterator (must be at object)
 * @return Object size, or 0 if not an object
 */
size_t dap_json_iterator_object_size(const dap_json_iterator_t *iter);

#ifdef __cplusplus
}
#endif

#endif // DAP_JSON_ITERATOR_H
