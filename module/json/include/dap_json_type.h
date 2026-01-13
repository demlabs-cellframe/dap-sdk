/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2026
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
    along with DAP SDK.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_json_type.h
 * @brief JSON Type Definitions
 * @details Common type definitions used across JSON module (public API, Stage 1, Stage 2)
 * @date 2025-01-08
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // for size_t
#include "dap_math_ops.h"  // for uint128_t, uint256_t

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           JSON VALUE TYPES                                 */
/* ========================================================================== */

/**
 * @brief JSON value types
 * @details Used both in public API and internal Stage 2 implementation
 */
typedef enum {
    DAP_JSON_TYPE_NULL = 0,      /**< null */
    DAP_JSON_TYPE_BOOLEAN,       /**< true or false */
    DAP_JSON_TYPE_DOUBLE,        /**< floating point number */
    DAP_JSON_TYPE_INT,           /**< int64_t integer */
    DAP_JSON_TYPE_UINT64,        /**< uint64_t unsigned integer */
    DAP_JSON_TYPE_UINT128,       /**< uint128_t large unsigned integer */
    DAP_JSON_TYPE_UINT256,       /**< uint256_t very large unsigned integer */
    DAP_JSON_TYPE_OBJECT,        /**< object (key-value pairs) */
    DAP_JSON_TYPE_ARRAY,         /**< array (ordered list) */
    DAP_JSON_TYPE_STRING         /**< string */
} dap_json_type_t;

/**
 * @brief JSON number representation (Phase 2.1: Optimized layout)
 * @details Supports int64, uint64, uint128, uint256, and double
 * 
 * Layout optimization:
 * - Hot field is_double first (frequently checked)
 * - Proper alignment for uint256_t (32 bytes)
 * Total size: 40 bytes (1 byte flag + 7 padding + 32 byte union)
 */
typedef struct {
    bool is_double;       /**< true if double, false otherwise (check type) - HOT FIELD */
    uint8_t _pad[7];      /**< Padding for 8-byte alignment of union */
    union {
        int64_t i;        /**< Signed 64-bit integer */
        uint64_t u64;     /**< Unsigned 64-bit integer */
        uint128_t u128;   /**< Unsigned 128-bit integer */
        uint256_t u256;   /**< Unsigned 256-bit integer */
        double d;         /**< IEEE 754 double precision */
    };
} dap_json_number_t;

/**
 * @brief JSON string representation
 * @details Supports both zero-copy and materialized strings
 *          - data: points to original JSON buffer (zero-copy) OR materialized copy
 *          - length: string length (for zero-copy access)
 *          - data_materialized: cached null-terminated copy (lazy allocation)
 *          - is_zero_copy: true if data points to original buffer (not null-terminated)
 */
typedef struct {
    const char *data;              /**< String data (zero-copy: NOT null-terminated) */
    size_t length;                 /**< String length (excluding null terminator) */
    char *data_materialized;       /**< Cached null-terminated copy (NULL if not yet materialized) */
    bool is_zero_copy;             /**< true if data is zero-copy (not null-terminated) */
    bool needs_free;               /**< true if data_materialized needs to be freed */
} dap_json_string_t;

/* Forward declarations for recursive types */
typedef struct dap_json_value dap_json_value_t;
typedef struct dap_json dap_json_t;

/**
 * @brief JSON array representation
 */
typedef struct {
    dap_json_value_t **elements;  /**< Array of value pointers */
    size_t count;                  /**< Number of elements */
    size_t capacity;               /**< Allocated capacity */
    dap_json_t **wrappers;         /**< Cached wrappers for borrowed refs (json-c compatible) */
} dap_json_array_t;

/**
 * @brief JSON object key-value pair
 */
typedef struct {
    char *key;                     /**< Key string (null-terminated) */
    dap_json_value_t *value;       /**< Value pointer */
} dap_json_object_pair_t;

/**
 * @brief JSON object representation
 */
typedef struct {
    dap_json_object_pair_t *pairs; /**< Array of key-value pairs */
    size_t count;                   /**< Number of pairs */
    size_t capacity;                /**< Allocated capacity */
    dap_json_t **wrappers;         /**< Cached wrappers for borrowed refs (json-c compatible) */
} dap_json_object_t;

/**
 * @brief JSON value (unified type for all JSON values)
 * @details Phase 2.1: Optimized struct layout for cache efficiency
 * 
 * Layout optimization:
 * - type as uint8_t (1 byte instead of 4) - HOT FIELD first
 * - Explicit padding for alignment
 * - Union aligned to 8 bytes
 * Total size: 48 bytes (1+7 pad + 40 union)
 */
struct dap_json_value {
    uint8_t type;                  /**< Value type (dap_json_type_t) - HOT FIELD */
    uint8_t _pad[7];               /**< Padding for 8-byte alignment */
    union {
        bool boolean;              /**< Boolean value (for TYPE_BOOLEAN) */
        dap_json_number_t number;  /**< Number value (for TYPE_INT/TYPE_DOUBLE) */
        dap_json_string_t string;  /**< String value (for TYPE_STRING) */
        dap_json_array_t array;    /**< Array value (for TYPE_ARRAY) */
        dap_json_object_t object;  /**< Object value (for TYPE_OBJECT) */
    };
    
    // ⭐ Arena refcounting support
    void *arena_page_handle;       /**< Page handle from refcounted arena (for borrowed refs) */
};

#ifdef __cplusplus
}
#endif

