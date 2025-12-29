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

#include "dap_cli_error_codes.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include <pthread.h>

#define LOG_TAG "cli_error_codes"

// Global registry of error codes
static dap_cli_error_code_t *s_error_codes_by_name = NULL;
static dap_cli_error_code_t *s_error_codes_by_code = NULL;
static pthread_rwlock_t s_error_codes_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief Initialize CLI error codes system
 */
int dap_cli_error_codes_init(void)
{
    pthread_rwlock_init(&s_error_codes_rwlock, NULL);
    
    // Register generic error codes
    dap_cli_error_code_register("SUCCESS", DAP_CLI_ERROR_CODE_SUCCESS, "Operation completed successfully");
    dap_cli_error_code_register("GENERIC_ERROR", DAP_CLI_ERROR_CODE_GENERIC_ERROR, "Generic error");
    dap_cli_error_code_register("INVALID_PARAMS", DAP_CLI_ERROR_CODE_INVALID_PARAMS, "Invalid parameters");
    dap_cli_error_code_register("NOT_FOUND", DAP_CLI_ERROR_CODE_NOT_FOUND, "Resource not found");
    dap_cli_error_code_register("ALREADY_EXISTS", DAP_CLI_ERROR_CODE_ALREADY_EXISTS, "Resource already exists");
    dap_cli_error_code_register("PERMISSION_DENIED", DAP_CLI_ERROR_CODE_PERMISSION_DENIED, "Permission denied");
    dap_cli_error_code_register("INTERNAL_ERROR", DAP_CLI_ERROR_CODE_INTERNAL_ERROR, "Internal error");
    
    log_it(L_INFO, "CLI error codes system initialized");
    return 0;
}

/**
 * @brief Deinitialize and free all registered error codes
 */
void dap_cli_error_codes_deinit(void)
{
    pthread_rwlock_wrlock(&s_error_codes_rwlock);
    
    dap_cli_error_code_t *l_item, *l_tmp;
    HASH_ITER(hh, s_error_codes_by_name, l_item, l_tmp) {
        HASH_DELETE(hh, s_error_codes_by_name, l_item);
        HASH_DELETE(hh_code, s_error_codes_by_code, l_item);
        DAP_DEL_Z(l_item->name);
        DAP_DEL_Z(l_item->description);
        DAP_DELETE(l_item);
    }
    
    pthread_rwlock_unlock(&s_error_codes_rwlock);
    pthread_rwlock_destroy(&s_error_codes_rwlock);
    
    log_it(L_INFO, "CLI error codes system deinitialized");
}

/**
 * @brief Register a new CLI error code
 */
int dap_cli_error_code_register(const char *a_name, int a_code, const char *a_description)
{
    if (!a_name) {
        log_it(L_ERROR, "Cannot register error code: name is NULL");
        return -1;
    }
    
    pthread_rwlock_wrlock(&s_error_codes_rwlock);
    
    // Check if name already exists
    dap_cli_error_code_t *l_existing = NULL;
    HASH_FIND_STR(s_error_codes_by_name, a_name, l_existing);
    if (l_existing) {
        pthread_rwlock_unlock(&s_error_codes_rwlock);
        log_it(L_WARNING, "Error code '%s' already registered with code %d", a_name, l_existing->code);
        return -2;
    }
    
    // Check if code already exists
    HASH_FIND(hh_code, s_error_codes_by_code, &a_code, sizeof(int), l_existing);
    if (l_existing) {
        pthread_rwlock_unlock(&s_error_codes_rwlock);
        log_it(L_WARNING, "Error code %d already registered with name '%s'", a_code, l_existing->name);
        return -3;
    }
    
    // Create new error code entry
    dap_cli_error_code_t *l_new = DAP_NEW_Z(dap_cli_error_code_t);
    if (!l_new) {
        pthread_rwlock_unlock(&s_error_codes_rwlock);
        log_it(L_ERROR, "Memory allocation failed for error code");
        return -4;
    }
    
    l_new->name = dap_strdup(a_name);
    l_new->code = a_code;
    l_new->description = a_description ? dap_strdup(a_description) : NULL;
    
    // Add to both hash tables
    HASH_ADD_STR(s_error_codes_by_name, name, l_new);
    HASH_ADD(hh_code, s_error_codes_by_code, code, sizeof(int), l_new);
    
    pthread_rwlock_unlock(&s_error_codes_rwlock);
    
    log_it(L_DEBUG, "Registered error code: %s = %d (%s)", a_name, a_code, 
           a_description ? a_description : "no description");
    return 0;
}

/**
 * @brief Get error code by name
 */
int dap_cli_error_code_get(const char *a_name)
{
    if (!a_name)
        return DAP_CLI_ERROR_CODE_GENERIC_ERROR;
    
    pthread_rwlock_rdlock(&s_error_codes_rwlock);
    
    dap_cli_error_code_t *l_item = NULL;
    HASH_FIND_STR(s_error_codes_by_name, a_name, l_item);
    
    int l_code = l_item ? l_item->code : DAP_CLI_ERROR_CODE_GENERIC_ERROR;
    
    pthread_rwlock_unlock(&s_error_codes_rwlock);
    
    return l_code;
}

/**
 * @brief Get error code description
 */
const char *dap_cli_error_code_get_description(int a_code)
{
    pthread_rwlock_rdlock(&s_error_codes_rwlock);
    
    dap_cli_error_code_t *l_item = NULL;
    HASH_FIND(hh_code, s_error_codes_by_code, &a_code, sizeof(int), l_item);
    
    const char *l_desc = l_item ? l_item->description : NULL;
    
    pthread_rwlock_unlock(&s_error_codes_rwlock);
    
    return l_desc;
}

/**
 * @brief Get error code name
 */
const char *dap_cli_error_code_get_name(int a_code)
{
    pthread_rwlock_rdlock(&s_error_codes_rwlock);
    
    dap_cli_error_code_t *l_item = NULL;
    HASH_FIND(hh_code, s_error_codes_by_code, &a_code, sizeof(int), l_item);
    
    const char *l_name = l_item ? l_item->name : NULL;
    
    pthread_rwlock_unlock(&s_error_codes_rwlock);
    
    return l_name;
}

