#ifndef PYTHON_DAP_H
#define PYTHON_DAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Plugin-compatible DAP SDK initialization API
// ============================================================================

/**
 * @brief Initialize DAP SDK with custom parameters for plugin use
 * @param a_app_name Application name (NULL for default "dap")
 * @param a_working_dir Working directory (NULL for default "/opt/dap") 
 * @param a_config_dir Config directory (NULL for default working_dir/etc)
 * @param a_temp_dir Temp directory (NULL for default working_dir/tmp)
 * @param a_log_file Log file path (NULL for default working_dir/var/log/app_name.log)
 * @param a_events_threads Events threads count (0 for default 1)
 * @param a_events_timeout Events timeout (0 for default 10000)
 * @param a_debug_mode Debug mode flag
 * @return 0 on success, negative error code on failure
 */
int dap_sdk_init(const char* a_app_name, 
                 const char* a_working_dir,
                 const char* a_config_dir,
                 const char* a_temp_dir,
                 const char* a_log_file,
                 uint32_t a_events_threads,
                 uint32_t a_events_timeout,
                 bool a_debug_mode);

/**
 * @brief Deinitialize DAP SDK (for plugin cleanup)
 */
void dap_sdk_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // PYTHON_DAP_H
