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

/**
 * @brief DAP SDK Module flags for initialization
 */
typedef enum dap_sdk_modules {
    // Core modules
    DAP_SDK_MODULE_CORE         = 0x00000001,    ///< Core modules (always required)
    DAP_SDK_MODULE_CRYPTO       = 0x00000002,    ///< Cryptographic modules
    DAP_SDK_MODULE_IO           = 0x00000004,    ///< I/O and event system
    DAP_SDK_MODULE_GLOBAL_DB    = 0x00000008,    ///< Global database system
    
    // Network modules
    DAP_SDK_MODULE_NET_CLIENT   = 0x00000010,    ///< Network client
    DAP_SDK_MODULE_NET_SERVER   = 0x00000020,    ///< Basic network server
    DAP_SDK_MODULE_NET_HTTP     = 0x00000040,    ///< HTTP server/client
    DAP_SDK_MODULE_NET_STREAM   = 0x00000080,    ///< Stream protocol
    DAP_SDK_MODULE_NET_DNS      = 0x00000100,    ///< DNS server/client
    DAP_SDK_MODULE_NET_ENC      = 0x00000200,    ///< Encryption server
    DAP_SDK_MODULE_NET_NOTIFY   = 0x00000400,    ///< Notification server
    DAP_SDK_MODULE_NET_LINK_MGR = 0x00000800,    ///< Link manager
    
    // CLI and RPC
    DAP_SDK_MODULE_CLI_SERVER   = 0x00001000,    ///< CLI server
    DAP_SDK_MODULE_APP_CLI      = 0x00002000,    ///< Application CLI
    DAP_SDK_MODULE_JSON_RPC     = 0x00004000,    ///< JSON-RPC server
    
    // Additional systems
    DAP_SDK_MODULE_PLUGIN       = 0x00008000,    ///< Plugin system
    DAP_SDK_MODULE_AVRESTREAM   = 0x00010000,    ///< Audio/Video streaming
    DAP_SDK_MODULE_TEST         = 0x00020000,    ///< Test framework
    
    // Convenience combinations
    DAP_SDK_MODULE_MINIMAL      = DAP_SDK_MODULE_CORE,
    DAP_SDK_MODULE_BASIC        = DAP_SDK_MODULE_CORE | DAP_SDK_MODULE_CRYPTO,
    DAP_SDK_MODULE_NETWORK_BASE = DAP_SDK_MODULE_CORE | DAP_SDK_MODULE_IO | 
                                 DAP_SDK_MODULE_NET_CLIENT | DAP_SDK_MODULE_NET_SERVER,
    DAP_SDK_MODULE_WEB_SERVER   = DAP_SDK_MODULE_NETWORK_BASE | DAP_SDK_MODULE_NET_HTTP,
    DAP_SDK_MODULE_FULL_NET     = DAP_SDK_MODULE_CORE | DAP_SDK_MODULE_CRYPTO | 
                                 DAP_SDK_MODULE_IO | DAP_SDK_MODULE_GLOBAL_DB |
                                 DAP_SDK_MODULE_NET_CLIENT | DAP_SDK_MODULE_NET_SERVER |
                                 DAP_SDK_MODULE_NET_HTTP | DAP_SDK_MODULE_NET_STREAM |
                                 DAP_SDK_MODULE_JSON_RPC,
    DAP_SDK_MODULE_ALL          = 0xFFFFFFFF     ///< All available modules
} dap_sdk_modules_t;

/**
 * @brief DAP SDK Configuration structure
 */
typedef struct dap_sdk_config {
    uint32_t modules;               ///< Module flags (combination of dap_sdk_modules_t)
    const char *app_name;           ///< Application name for logging and identification
    dap_log_level_t log_level;      ///< Logging level
    const char *temp_dir;           ///< Temporary directory (optional)
    const char *log_file;           ///< Log file path (optional)
    bool enable_debug;              ///< Enable debug mode
} dap_sdk_config_t;

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
