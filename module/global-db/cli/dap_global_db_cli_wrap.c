/**
 * @file dap_global_db_cli_wrap.c
 * @brief Wrapper function implementations for global_db CLI commands
 * 
 * These wrapper functions delegate to the real implementations and can be
 * intercepted by the DAP Mock Framework during unit testing.
 * 
 * @author Cellframe Team
 * @copyright DeM Labs Inc. 2025
 * @license GPL-3.0
 */

#include "dap_global_db_cli_wrap.h"
#include "dap_global_db.h"
#include "dap_global_db_driver.h"

/**
 * @brief Wrapper for dap_global_db_flush_sync
 */
int dap_global_db_flush_sync_w(void)
{
    return dap_global_db_flush_sync();
}

/**
 * @brief Wrapper for dap_global_db_get_sync
 */
uint8_t* dap_global_db_get_sync_w(const char *a_group, const char *a_key, 
                                   size_t *a_value_len, bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    return dap_global_db_get_sync(a_group, a_key, a_value_len, a_is_pinned, a_ts);
}

/**
 * @brief Wrapper for dap_global_db_set_sync
 */
int dap_global_db_set_sync_w(const char *a_group, const char *a_key, 
                              const void *a_value, size_t a_value_len, bool a_pin)
{
    return dap_global_db_set_sync(a_group, a_key, a_value, a_value_len, a_pin);
}

/**
 * @brief Wrapper for dap_global_db_del_sync
 */
int dap_global_db_del_sync_w(const char *a_group, const char *a_key)
{
    return dap_global_db_del_sync(a_group, a_key);
}

/**
 * @brief Wrapper for dap_global_db_driver_count
 */
size_t dap_global_db_driver_hash_count_w(const char *a_group)
{
    dap_global_db_driver_hash_t l_hash_null = {0};
    return dap_global_db_driver_count(a_group, l_hash_null, false);
}

/**
 * @brief Wrapper for dap_global_db_get_all_sync
 */
dap_global_db_obj_t* dap_global_db_get_all_sync_w(const char *a_group, size_t *a_count)
{
    return dap_global_db_get_all_sync(a_group, a_count);
}

