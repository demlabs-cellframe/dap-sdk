/*
 * Authors:
 * Cellframe Team
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2024
 * All rights reserved.

 This file is part of CellFrame SDK the open source project

    CellFrame SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CellFrame SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any CellFrame SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include "uthash.h"

/**
 * @brief Dynamic CLI error codes registry
 * 
 * This system allows modules to register their CLI error codes dynamically
 * instead of using static enums which create dependencies.
 * 
 * Each module registers its error codes during initialization using
 * dap_cli_error_code_register().
 */

typedef struct dap_cli_error_code {
    char *name;              // Error code name (e.g. "LEDGER_PARAM_ERR")
    int code;                // Numeric error code
    char *description;       // Human-readable description
    UT_hash_handle hh;       // Hash table handle (by name)
    UT_hash_handle hh_code;  // Hash table handle (by code)
} dap_cli_error_code_t;

/**
 * @brief Initialize CLI error codes system
 * @return 0 on success, negative on error
 */
int dap_cli_error_codes_init(void);

/**
 * @brief Deinitialize and free all registered error codes
 */
void dap_cli_error_codes_deinit(void);

/**
 * @brief Register a new CLI error code
 * @param a_name Error code name (will be prefixed with module name)
 * @param a_code Numeric error code (should be unique)
 * @param a_description Human-readable description
 * @return 0 on success, negative on error
 */
int dap_cli_error_code_register(const char *a_name, int a_code, const char *a_description);

/**
 * @brief Get error code by name
 * @param a_name Error code name
 * @return Error code or 0 if not found
 */
int dap_cli_error_code_get(const char *a_name);

/**
 * @brief Get error code description
 * @param a_code Error code
 * @return Description string or NULL if not found
 */
const char *dap_cli_error_code_get_description(int a_code);

/**
 * @brief Get error code name
 * @param a_code Error code
 * @return Name string or NULL if not found
 */
const char *dap_cli_error_code_get_name(int a_code);

// Generic error codes (always available)
#define DAP_CLI_ERROR_CODE_SUCCESS              0
#define DAP_CLI_ERROR_CODE_GENERIC_ERROR        -1
#define DAP_CLI_ERROR_CODE_INVALID_PARAMS       -2
#define DAP_CLI_ERROR_CODE_NOT_FOUND            -3
#define DAP_CLI_ERROR_CODE_ALREADY_EXISTS       -4
#define DAP_CLI_ERROR_CODE_PERMISSION_DENIED    -5
#define DAP_CLI_ERROR_CODE_INTERNAL_ERROR       -6

