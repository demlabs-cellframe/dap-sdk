/**
 * @file dap_global_db_cli_wrap.h
 * @brief Wrapper functions for global_db CLI commands (for mocking in tests)
 * 
 * This file declares wrapper functions that can be mocked using the DAP Mock Framework.
 * The wrappers are used to intercept calls to underlying global_db functions
 * during unit testing, allowing controlled test behavior.
 * 
 * @author Cellframe Team
 * @copyright DeM Labs Inc. 2025
 * @license GPL-3.0
 */

#pragma once

#include "dap_global_db.h"
#include "dap_global_db_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wrapper for dap_global_db_flush_sync
 * @return 0 on success, negative on error
 */
int dap_global_db_flush_sync_w(void);

/**
 * @brief Wrapper for dap_global_db_get_sync
 * @param a_group Group name
 * @param a_key Key name
 * @param a_value_len Output: value length
 * @param a_is_pinned Output: pinned status
 * @param a_ts Output: timestamp
 * @return Value data or NULL
 */
uint8_t* dap_global_db_get_sync_w(const char *a_group, const char *a_key, 
                                   size_t *a_value_len, bool *a_is_pinned, dap_nanotime_t *a_ts);

/**
 * @brief Wrapper for dap_global_db_set_sync
 * @param a_group Group name
 * @param a_key Key name
 * @param a_value Value data
 * @param a_value_len Value length
 * @param a_pin Pin record
 * @return 0 on success
 */
int dap_global_db_set_sync_w(const char *a_group, const char *a_key, 
                              const void *a_value, size_t a_value_len, bool a_pin);

/**
 * @brief Wrapper for dap_global_db_del_sync
 * @param a_group Group name
 * @param a_key Key name
 * @return 0 on success
 */
int dap_global_db_del_sync_w(const char *a_group, const char *a_key);

/**
 * @brief Wrapper for dap_global_db_driver_hash_count
 * @param a_group Group name
 * @return Number of records in group
 */
size_t dap_global_db_driver_hash_count_w(const char *a_group);

/**
 * @brief Wrapper for dap_global_db_get_all_sync
 * @param a_group Group name
 * @param a_count Output: number of objects
 * @return Array of objects or NULL
 */
dap_global_db_obj_t* dap_global_db_get_all_sync_w(const char *a_group, size_t *a_count);

#ifdef __cplusplus
}
#endif

