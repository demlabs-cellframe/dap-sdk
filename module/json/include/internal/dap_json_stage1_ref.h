/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
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
 * @file dap_json_stage1_ref.h
 * @brief Stage 1 JSON tokenization - Reference C implementation
 * @details Portable baseline implementation for correctness verification
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include main header for types and structures
#include "dap_json_stage1.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run Stage 1 structural indexing (reference C implementation)
 * @param[in,out] a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
extern int dap_json_stage1_run_ref(dap_json_stage1_t *a_stage1);

/**
 * @brief Add a token to the Stage 1 output array
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_position Token position in input buffer
 * @param[in] a_length Token length (0 for structural)
 * @param[in] a_type Token type (structural/string/number/literal)
 * @param[in] a_character_or_subtype Structural character or literal subtype
 * @return true on success, false on allocation failure
 */
extern bool dap_json_stage1_add_token(
    dap_json_stage1_t *a_stage1,
    uint32_t a_position,
    uint32_t a_length,
    dap_json_token_type_t a_type,
    uint8_t a_character_or_subtype
);

/**
 * @brief Scan string from current position (reference implementation)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of opening quote
 * @return Position after closing quote on success, original position on error
 */
extern size_t dap_json_stage1_scan_string_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

/**
 * @brief Scan number from current position (reference implementation)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of first digit/minus
 * @return Position after last number character on success, original position on error
 */
extern size_t dap_json_stage1_scan_number_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

/**
 * @brief Scan literal from current position (reference implementation)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of first character (t/f/n)
 * @return Position after literal on success, original position if not a literal
 */
extern size_t dap_json_stage1_scan_literal_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

#ifdef __cplusplus
}
#endif

