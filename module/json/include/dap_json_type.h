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

/* Forward declaration for recursive type */
typedef struct dap_json dap_json_t;

/**
 * @brief JSON value (compact 8-byte zero-copy representation)
 * @details Phase 2.0.4: UNIFIED structure - THE ONLY internal representation
 * 
 * This structure achieves:
 * - 7x memory reduction (56 bytes → 8 bytes per value)
 * - Zero-copy string storage (reference source buffer)
 * - Lazy number parsing
 * - Flat array storage for containers
 * 
 * **Memory Layout (8 bytes total, packed):**
 * ```
 * Byte 0: type     (dap_json_type_t)
 * Byte 1: flags    (optimization hints)
 * Byte 2-3: length (value length in source, 0-64KB)
 * Byte 4-7: offset (start position in source, 0-4GB)
 * ```
 * 
 * **Zero-Copy Semantics:**
 * - Strings: offset points to '"' character in source, length excludes quotes
 * - Numbers: offset points to first digit, length includes all digits/symbols
 * - Objects/Arrays: offset stores pointer to indices array (cast to uint32_t)
 * - Booleans/Null: offset points to literal in source
 * 
 * **Source Buffer Lifetime:**
 * ⚠️ WARNING: The source buffer MUST remain valid for the entire lifetime
 * of any dap_json_t object. Freeing the source buffer while values exist
 * will cause crashes.
 * 
 * @see dap_json_value.h for extended formats and utility functions
 */
typedef struct dap_json_value {
    uint8_t type;       /**< Value type (TYPE_STRING, TYPE_NUMBER, etc) */
    uint8_t flags;      /**< Optimization flags (escaped, cached, etc) */
    uint16_t length;    /**< Length in source buffer (0-64KB) */
    uint32_t offset;    /**< Start offset in source buffer (0-4GB) */
} dap_json_value_t;

// Compile-time assertion: ensure 8 bytes
#ifdef __cplusplus
static_assert(sizeof(dap_json_value_t) == 8, 
              "dap_json_value_t must be exactly 8 bytes");
#else
_Static_assert(sizeof(dap_json_value_t) == 8, 
               "dap_json_value_t must be exactly 8 bytes");
#endif

#ifdef __cplusplus
}
#endif

