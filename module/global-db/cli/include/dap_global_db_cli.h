/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe       https://cellframe.net
 * Copyright  (c) 2019-2025
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 * DAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "dap_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Error codes for global_db CLI command
 */
enum {
    DAP_GLOBAL_DB_CLI_OK = 0,
    DAP_GLOBAL_DB_CLI_PARAM_ERR,
    DAP_GLOBAL_DB_CLI_COMMAND_ERR,
    DAP_GLOBAL_DB_CLI_CANT_OPEN_DIR,
    DAP_GLOBAL_DB_CLI_CANT_INIT_DB,
    DAP_GLOBAL_DB_CLI_CANT_INIT_SQL,
    DAP_GLOBAL_DB_CLI_CANT_COMMIT_TO_DISK,
    DAP_GLOBAL_DB_CLI_RECORD_NOT_FOUND,
    DAP_GLOBAL_DB_CLI_RECORD_NOT_PINNED,
    DAP_GLOBAL_DB_CLI_RECORD_NOT_UNPINNED,
    DAP_GLOBAL_DB_CLI_WRITING_FAILED,
    DAP_GLOBAL_DB_CLI_TIME_NO_VALUE,
    DAP_GLOBAL_DB_CLI_NO_KEY_PROVIDED,
    DAP_GLOBAL_DB_CLI_NO_DATA_IN_GROUP,
    DAP_GLOBAL_DB_CLI_DELETE_FAILED,
    DAP_GLOBAL_DB_CLI_DROP_FAILED,
    DAP_GLOBAL_DB_CLI_MEMORY_ERR
};

/**
 * @brief Main global_db CLI command handler
 * @details Implements 'global_db' CLI command with subcommands:
 *          - flush: Flush database cache to disk
 *          - record get/pin/unpin: Work with records
 *          - write: Write data to database
 *          - read: Read data from database
 *          - delete: Delete record
 *          - drop_table: Drop entire group
 *          - get_keys: Get all keys from group
 *          - group_list: List all groups
 *          - clear: Clear group(s)
 * 
 * @param a_argc Argument count
 * @param a_argv Argument values
 * @param a_json_arr_reply JSON array for reply
 * @param a_version API version
 * @return 0 on success, error code on failure
 */
int com_global_db(int a_argc, char **a_argv, dap_json_t *a_json_arr_reply, int a_version);

/**
 * @brief Initialize global_db CLI module
 * @details Registers 'global_db' command with CLI server
 * @return 0 on success, negative error code on failure
 */
int dap_global_db_cli_init(void);

/**
 * @brief Deinitialize global_db CLI module
 */
void dap_global_db_cli_deinit(void);

#ifdef __cplusplus
}
#endif
