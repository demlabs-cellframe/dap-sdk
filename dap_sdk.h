/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
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
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dap_link_manager_callbacks;

typedef enum dap_sdk_modules {
    DAP_SDK_MODULE_CORE         = 0x00000001,
    DAP_SDK_MODULE_CRYPTO       = 0x00000002,
    DAP_SDK_MODULE_IO           = 0x00000004,
    DAP_SDK_MODULE_GLOBAL_DB    = 0x00000008,

    DAP_SDK_MODULE_NET_CLIENT   = 0x00000010,
    DAP_SDK_MODULE_NET_SERVER   = 0x00000020,
    DAP_SDK_MODULE_NET_HTTP     = 0x00000040,
    DAP_SDK_MODULE_NET_STREAM   = 0x00000080,
    DAP_SDK_MODULE_NET_DNS      = 0x00000100,
    DAP_SDK_MODULE_NET_ENC      = 0x00000200,
    DAP_SDK_MODULE_NET_NOTIFY   = 0x00000400,
    DAP_SDK_MODULE_NET_LINK_MGR = 0x00000800,

    DAP_SDK_MODULE_CLI_SERVER   = 0x00001000,
    DAP_SDK_MODULE_APP_CLI      = 0x00002000,
    DAP_SDK_MODULE_JSON_RPC     = 0x00004000,

    DAP_SDK_MODULE_PLUGIN       = 0x00008000,
    DAP_SDK_MODULE_NET_CLUSTER  = 0x00010000,
    DAP_SDK_MODULE_TEST         = 0x00020000,

    DAP_SDK_MODULE_MINIMAL      = DAP_SDK_MODULE_CORE,
    DAP_SDK_MODULE_BASIC        = DAP_SDK_MODULE_CORE | DAP_SDK_MODULE_CRYPTO,
    DAP_SDK_MODULE_NETWORK_BASE = DAP_SDK_MODULE_CORE | DAP_SDK_MODULE_CRYPTO |
                                  DAP_SDK_MODULE_IO | DAP_SDK_MODULE_NET_CLIENT,
    DAP_SDK_MODULE_FULL_NET     = DAP_SDK_MODULE_NETWORK_BASE |
                                  DAP_SDK_MODULE_NET_SERVER | DAP_SDK_MODULE_NET_HTTP |
                                  DAP_SDK_MODULE_NET_STREAM | DAP_SDK_MODULE_NET_ENC |
                                  DAP_SDK_MODULE_NET_CLUSTER | DAP_SDK_MODULE_NET_NOTIFY |
                                  DAP_SDK_MODULE_NET_LINK_MGR | DAP_SDK_MODULE_GLOBAL_DB |
                                  DAP_SDK_MODULE_CLI_SERVER,
    DAP_SDK_MODULE_ALL          = 0xFFFFFFFF
} dap_sdk_modules_t;

typedef struct dap_sdk_config {
    uint32_t modules;
    const char *app_name;
    dap_log_level_t log_level;
    const char *sys_dir;            ///< Base directory; sets g_sys_dir_path
    const char *config_dir;         ///< Config directory for .cfg files
    const char *config_name;        ///< Config file name (opens g_config); NULL to skip
    const char *log_file;
    uint32_t io_threads;            ///< 0 = auto-detect
    uint32_t io_timeout;            ///< Connection timeout in seconds; 0 = default
    bool enable_debug;
    bool auto_node_cert;            ///< Generate node-addr signing cert if absent

    const struct dap_link_manager_callbacks *link_manager_callbacks; ///< NULL = default no-op stubs
} dap_sdk_config_t;

#if defined(DAP_OS_WASM_MT)
/**
 * @brief Pre-initialize WASMFS/OPFS before any filesystem access (WASM pthreads only).
 *        Safe to call multiple times; subsequent calls are no-ops.
 * @param a_mount Mount point (NULL defaults to "/dap")
 * @return 0 on success
 */
int dap_sdk_wasmfs_init(const char *a_mount);
#endif

/**
 * @brief Initialize DAP SDK with specified configuration
 * @param a_config Configuration structure
 * @return 0 on success, error code otherwise
 */
int dap_sdk_init(const dap_sdk_config_t *a_config);

/**
 * @brief Initialize DAP SDK with simple modules
 * @param a_modules Module flags (combination of dap_sdk_modules_t)
 * @return 0 on success, error code otherwise
 */
int dap_sdk_init_simple(uint32_t a_modules);

/**
 * @brief Initialize DAP SDK with app name and modules
 * @param a_app_name Application name
 * @param a_modules Module flags (combination of dap_sdk_modules_t)
 * @return 0 on success, error code otherwise
 */
int dap_sdk_init_with_app_name(const char *a_app_name, uint32_t a_modules);

/**
 * @brief Deinitialize DAP SDK
 */
void dap_sdk_deinit(void);

/**
 * @brief Check if DAP SDK is initialized
 * @return true if initialized, false otherwise
 */
bool dap_sdk_is_initialized(void);

/**
 * @brief Get current initialized modules
 * @return Current module flags
 */
uint32_t dap_sdk_get_modules(void);

#ifdef __cplusplus
}
#endif
