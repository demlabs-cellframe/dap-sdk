/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP JSON Native Implementation Team
 * Copyright  (c) 2026
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
 * @file dap_json_stage2.h
 * @brief Stage 2: DOM Building - Internal API
 * 
 * Stage 2 использует structural indices из Stage 1 для построения DOM дерева.
 * Reference implementation (pure C) выполняет sequential traversal индексов.
 * 
 * Алгоритм:
 * 1. Sequential walk по structural indices из Stage 1
 * 2. Value parsing (strings, numbers, true/false/null)
 * 3. DOM node creation (objects, arrays)
 * 4. Tree assembly
 * 
 * Performance target (reference C): 0.8-1.2 GB/s (correctness baseline)
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2025-01-07
 */

#ifndef DAP_JSON_STAGE2_H
#define DAP_JSON_STAGE2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../dap_json_type.h"
#include "dap_json_stage1.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct dap_json_stage2 dap_json_stage2_t;
/* dap_json_value_t is defined in dap_json_type.h */

/* ========================================================================== */
/*                           JSON VALUE TYPES                                 */
/* ========================================================================== */

// JSON types and structures are defined in dap_json_type.h
// (included via dap_json_stage1.h -> dap_json.h)

/* ========================================================================== */
/*                           ERROR CODES                                      */
/* ========================================================================== */

/**
 * @brief Stage 2 error codes
 */
typedef enum {
    STAGE2_SUCCESS = 0,                    /**< No error */
    STAGE2_ERROR_INVALID_INPUT = 1,        /**< NULL input or invalid parameters */
    STAGE2_ERROR_OUT_OF_MEMORY = 2,        /**< Memory allocation failed */
    STAGE2_ERROR_UNEXPECTED_TOKEN = 3,     /**< Unexpected structural character */
    STAGE2_ERROR_INVALID_NUMBER = 4,       /**< Invalid number format */
    STAGE2_ERROR_INVALID_STRING = 5,       /**< Invalid string (e.g. invalid escape) */
    STAGE2_ERROR_INVALID_LITERAL = 6,      /**< Invalid true/false/null */
    STAGE2_ERROR_UNEXPECTED_END = 7,       /**< Unexpected end of structural indices */
    STAGE2_ERROR_MISMATCHED_BRACKETS = 8,  /**< Mismatched { } or [ ] */
    STAGE2_ERROR_MISSING_VALUE = 9,        /**< Missing value after : or , */
    STAGE2_ERROR_MISSING_COLON = 10,       /**< Missing : after object key */
    STAGE2_ERROR_DUPLICATE_KEY = 11        /**< Duplicate key in object */
} dap_json_stage2_error_t;

/* ========================================================================== */
/*                           STAGE 2 PARSER STATE                             */
/* ========================================================================== */

/**
 * @brief Stage 2 parser state
 * @details Использует structural indices из Stage 1 для построения DOM
 */
struct dap_json_stage2 {
    /* Input from Stage 1 */
    const uint8_t *input;                    /**< Original JSON input buffer */
    size_t input_len;                        /**< Input buffer length */
    const dap_json_struct_index_t *indices;  /**< Structural indices from Stage 1 */
    size_t indices_count;                    /**< Number of structural indices */
    
    /* Parser state */
    size_t current_index;                    /**< Current position in indices array */
    size_t current_depth;                    /**< Current nesting depth */
    size_t max_depth;                        /**< Maximum nesting depth allowed */
    
    /* Output */
    dap_json_value_t *root;                  /**< Root value (result of parsing) */
    
    /* Error handling */
    dap_json_stage2_error_t error_code;      /**< Last error code */
    size_t error_position;                   /**< Position of error in input */
    char error_message[256];                 /**< Error description */
    
    /* Statistics */
    size_t objects_created;                  /**< Number of objects created */
    size_t arrays_created;                   /**< Number of arrays created */
    size_t strings_created;                  /**< Number of strings created */
    size_t numbers_created;                  /**< Number of numbers created */
};

/* ========================================================================== */
/*                           PUBLIC API                                       */
/* ========================================================================== */

/**
 * @brief Initialize Stage 2 parser
 * @details Создаёт Stage 2 parser на основе результатов Stage 1.
 *          Must be freed with dap_json_stage2_free().
 * 
 * @param[in] a_stage1 Stage 1 parser (must have been run successfully)
 * @return Initialized Stage 2 parser, or NULL on error
 */
dap_json_stage2_t *dap_json_stage2_init(const dap_json_stage1_t *a_stage1);

/**
 * @brief Run Stage 2 DOM building
 * @details Строит DOM дерево из structural indices.
 *          После успешного выполнения root содержит результат.
 * 
 * @param[in,out] a_stage2 Initialized Stage 2 parser
 * @return STAGE2_SUCCESS on success, error code otherwise
 */
dap_json_stage2_error_t dap_json_stage2_run(dap_json_stage2_t *a_stage2);

/**
 * @brief Get root value
 * @details Возвращает корневое значение после successful parse.
 *          Value остаётся valid до dap_json_stage2_free() или dap_json_value_v2_free().
 * 
 * @param[in] a_stage2 Stage 2 parser
 * @return Root value, or NULL if not run yet or error occurred
 */
dap_json_value_t *dap_json_stage2_get_root(const dap_json_stage2_t *a_stage2);

/**
 * @brief Get Stage 2 statistics
 * @details Возвращает статистику создания объектов для profiling.
 * 
 * @param[in] a_stage2 Stage 2 parser
 * @param[out] a_out_objects Output: number of objects created (can be NULL)
 * @param[out] a_out_arrays Output: number of arrays created (can be NULL)
 * @param[out] a_out_strings Output: number of strings created (can be NULL)
 * @param[out] a_out_numbers Output: number of numbers created (can be NULL)
 */
void dap_json_stage2_get_stats(
    const dap_json_stage2_t *a_stage2,
    size_t *a_out_objects,
    size_t *a_out_arrays,
    size_t *a_out_strings,
    size_t *a_out_numbers
);

/**
 * @brief Free Stage 2 parser
 * @details Освобождает Stage 2 parser НО НЕ освобождает root value.
 *          Root value должен быть освобождён отдельно с dap_json_value_v2_free().
 * 
 * @param[in] a_stage2 Stage 2 parser to free (can be NULL)
 */
void dap_json_stage2_free(dap_json_stage2_t *a_stage2);

/* ========================================================================== */
/*                           VALUE API                                        */
/* ========================================================================== */

/**
 * @brief Create null value
 * @return New null value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_null(void);

/**
 * @brief Create boolean value
 * @param[in] a_value Boolean value
 * @return New boolean value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_bool(bool a_value);

/**
 * @brief Create number value (integer)
 * @param[in] a_value Integer value
 * @return New number value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_int(int64_t a_value);

/**
 * @brief Create number value (double)
 * @param[in] a_value Double value
 * @return New number value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_double(double a_value);

/**
 * @brief Create string value
 * @param[in] a_data String data (will be copied)
 * @param[in] a_length String length
 * @return New string value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_string(const char *a_data, size_t a_length);

/**
 * @brief Create array value
 * @return New empty array value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_array(void);

/**
 * @brief Create object value
 * @return New empty object value, or NULL on allocation failure
 */
dap_json_value_t *dap_json_value_v2_create_object(void);

/**
 * @brief Free JSON value recursively
 * @param[in] a_value Value to free (can be NULL)
 */
void dap_json_value_v2_free(dap_json_value_t *a_value);

/**
 * @brief Add element to array
 * @param[in,out] a_array Array value
 * @param[in] a_element Element to add (ownership transferred)
 * @return true on success, false on allocation failure
 */
bool dap_json_array_v2_add(dap_json_value_t *a_array, dap_json_value_t *a_element);

/**
 * @brief Add key-value pair to object
 * @param[in,out] a_object Object value
 * @param[in] a_key Key string (will be copied)
 * @param[in] a_value Value (ownership transferred)
 * @return true on success, false on allocation failure or duplicate key
 */
bool dap_json_object_v2_add(dap_json_value_t *a_object, const char *a_key, dap_json_value_t *a_value);

/**
 * @brief Get array element by index
 * @param[in] a_array Array value
 * @param[in] a_index Element index
 * @return Element value, or NULL if index out of bounds
 */
dap_json_value_t *dap_json_array_v2_get(const dap_json_value_t *a_array, size_t a_index);

/**
 * @brief Get object value by key
 * @param[in] a_object Object value
 * @param[in] a_key Key string
 * @return Value for key, or NULL if key not found
 */
dap_json_value_t *dap_json_object_v2_get(const dap_json_value_t *a_object, const char *a_key);

/**
 * @brief Get error string for error code
 * @param[in] a_error Error code
 * @return Human-readable error string
 */
const char *dap_json_stage2_error_to_string(dap_json_stage2_error_t a_error);

#ifdef __cplusplus
}
#endif

#endif /* DAP_JSON_STAGE2_H */

