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

#include "dap_sdk.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_string.h"

// Core includes
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_config.h"
#include "dap_crc64.h"
#include "dap_net_common.h"

// Conditional includes based on available modules
// Core includes are always needed
// Other modules will be included only if available and needed

#define LOG_TAG "dap_sdk"

// Global state
static bool s_dap_sdk_initialized = false;
static uint32_t s_current_modules = 0;

// Forward declarations for all initialization functions
static int s_init_core(const dap_sdk_config_t *a_config);
static int s_init_crypto(const dap_sdk_config_t *a_config);
static int s_init_io(const dap_sdk_config_t *a_config);
static int s_init_global_db(const dap_sdk_config_t *a_config);
static int s_init_net_client(const dap_sdk_config_t *a_config);
static int s_init_net_server(const dap_sdk_config_t *a_config);
static int s_init_net_http(const dap_sdk_config_t *a_config);
static int s_init_net_stream(const dap_sdk_config_t *a_config);
static int s_init_net_dns(const dap_sdk_config_t *a_config);
static int s_init_net_enc(const dap_sdk_config_t *a_config);
static int s_init_net_notify(const dap_sdk_config_t *a_config);
static int s_init_net_link_mgr(const dap_sdk_config_t *a_config);
static int s_init_cli_server(const dap_sdk_config_t *a_config);
static int s_init_app_cli(const dap_sdk_config_t *a_config);
static int s_init_json_rpc(const dap_sdk_config_t *a_config);
static int s_init_plugin(const dap_sdk_config_t *a_config);
static int s_init_test(const dap_sdk_config_t *a_config);

// Forward declarations for all deinitialization functions
static void s_deinit_core(void);
static void s_deinit_crypto(void);
static void s_deinit_io(void);
static void s_deinit_global_db(void);
static void s_deinit_net_client(void);
static void s_deinit_net_server(void);
static void s_deinit_net_http(void);
static void s_deinit_net_stream(void);
static void s_deinit_net_dns(void);
static void s_deinit_net_enc(void);
static void s_deinit_net_notify(void);
static void s_deinit_net_link_mgr(void);
static void s_deinit_cli_server(void);
static void s_deinit_app_cli(void);
static void s_deinit_json_rpc(void);
static void s_deinit_plugin(void);
static void s_deinit_test(void);

/**
 * @brief Initialize core subsystems
 */
static int s_init_core(const dap_sdk_config_t *a_config) {
    int ret = 0;
    
    // Initialize logging first
    dap_log_level_set(a_config->log_level);
    log_it(L_INFO, "Initializing DAP SDK Core subsystems");
    
    // Initialize common subsystem with provided app name
    const char *app_name = a_config->app_name ? a_config->app_name : "DAP SDK";
    if ((ret = dap_common_init(app_name, a_config->log_file)) != 0) {
        log_it(L_ERROR, "Failed to initialize dap_common: %d", ret);
        return ret;
    }
    
    // Initialize CRC64
    if ((ret = dap_crc64_init()) != 0) {
        log_it(L_ERROR, "Failed to initialize CRC64: %d", ret);
        return ret;
    }
    
    // Initialize config system if config path provided
    if (a_config->temp_dir) {
        if ((ret = dap_config_init(a_config->temp_dir)) != 0) {
            log_it(L_ERROR, "Failed to initialize config system: %d", ret);
            return ret;
        }
    }
    
    log_it(L_INFO, "DAP SDK Core initialized successfully");
    return 0;
}

/**
 * @brief Initialize crypto subsystems
 */
static int s_init_crypto(const dap_sdk_config_t *a_config) {
    log_it(L_INFO, "Initializing DAP SDK Crypto subsystem");
    
    // For now, crypto modules work out of the box
    // Real initialization will be added when we have proper includes
    
    log_it(L_INFO, "DAP SDK Crypto initialized successfully");
    return 0;
}

/**
 * @brief Initialize IO subsystems
 */
static int s_init_io(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "Initializing DAP SDK IO subsystem");
    
    // Initialize events system (required for timers, sockets, etc.)
    // Use reasonable defaults: 6 threads (to support multi-threaded tests), 60 second timeout
    if (dap_events_init(6, 60) != 0) {
        log_it(L_ERROR, "Failed to initialize events system");
        return -1;
    }
    
    log_it(L_INFO, "DAP SDK IO subsystem initialized successfully");
    return 0;
}

/**
 * @brief Initialize network subsystems
 */
static int s_init_network(const dap_sdk_config_t *a_config) {
    log_it(L_INFO, "Initializing DAP SDK Network subsystem");
    
    (void)a_config;
    int rc = dap_net_common_init();
    if (rc != 0) {
        log_it(L_ERROR, "dap_net_common_init failed with code %d", rc);
        return rc;
    }
    
    log_it(L_INFO, "DAP SDK Network initialized successfully");
    return 0;
}

/**
 * @brief Initialize DAP SDK with specified configuration
 */
int dap_sdk_init(const dap_sdk_config_t *a_config) {
    if (!a_config) {
        return -1;
    }
    
    if (s_dap_sdk_initialized) {
        log_it(L_WARNING, "DAP SDK already initialized");
        return 0;
    }
    
    int ret = 0;
    uint32_t modules = a_config->modules;
    
    // Core is always required - force it if not specified
    if (!(modules & DAP_SDK_MODULE_CORE)) {
        modules |= DAP_SDK_MODULE_CORE;
        log_it(L_INFO, "Core module auto-enabled (always required)");
    }
    
    log_it(L_INFO, "Initializing DAP SDK with modules: 0x%08X", modules);
    
    // Initialize core (always required)
    if ((ret = s_init_core(a_config)) != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK Core");
        return ret;
    }
    
    // Initialize crypto module
    if (modules & DAP_SDK_MODULE_CRYPTO) {
        if ((ret = s_init_crypto(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Crypto subsystem");
            return ret;
        }
    }
    
    // Initialize IO module  
    if (modules & DAP_SDK_MODULE_IO) {
        if ((ret = s_init_io(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize IO subsystem");
            return ret;
        }
    }
    
    // Initialize global database module
    if (modules & DAP_SDK_MODULE_GLOBAL_DB) {
        if ((ret = s_init_global_db(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Global DB subsystem");
            return ret;
        }
    }
    
    // Initialize basic network modules
    if (modules & DAP_SDK_MODULE_NET_CLIENT) {
        if ((ret = s_init_net_client(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Network Client");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_SERVER) {
        if ((ret = s_init_net_server(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Network Server");
            return ret;
        }
    }
    
    // Initialize advanced network modules
    if (modules & DAP_SDK_MODULE_NET_HTTP) {
        if ((ret = s_init_net_http(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize HTTP Server/Client");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_STREAM) {
        if ((ret = s_init_net_stream(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Stream Protocol");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_DNS) {
        if ((ret = s_init_net_dns(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize DNS Server/Client");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_ENC) {
        if ((ret = s_init_net_enc(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Encryption Server");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_NOTIFY) {
        if ((ret = s_init_net_notify(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Notification Server");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_NET_LINK_MGR) {
        if ((ret = s_init_net_link_mgr(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Link Manager");
            return ret;
        }
    }
    
    // Initialize CLI and RPC modules
    if (modules & DAP_SDK_MODULE_CLI_SERVER) {
        if ((ret = s_init_cli_server(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize CLI Server");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_APP_CLI) {
        if ((ret = s_init_app_cli(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Application CLI");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_JSON_RPC) {
        if ((ret = s_init_json_rpc(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize JSON-RPC Server");
            return ret;
        }
    }
    
    // Initialize additional systems
    if (modules & DAP_SDK_MODULE_PLUGIN) {
        if ((ret = s_init_plugin(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Plugin System");
            return ret;
        }
    }
    
    if (modules & DAP_SDK_MODULE_TEST) {
        if ((ret = s_init_test(a_config)) != 0) {
            log_it(L_ERROR, "Failed to initialize Test Framework");
            return ret;
        }
    }
    
    s_dap_sdk_initialized = true;
    s_current_modules = modules;
    
    log_it(L_INFO, "DAP SDK initialized successfully with modules: 0x%08X", modules);
    return 0;
}

/**
 * @brief Initialize DAP SDK with simple modules
 */
int dap_sdk_init_simple(uint32_t a_modules) {
    dap_sdk_config_t config = {
        .modules = a_modules,
        .app_name = "DAP SDK",
        .log_level = L_INFO,
        .temp_dir = NULL,
        .log_file = NULL,
        .enable_debug = false
    };
    
    return dap_sdk_init(&config);
}

/**
 * @brief Initialize DAP SDK with app name and modules
 */
int dap_sdk_init_with_app_name(const char *a_app_name, uint32_t a_modules) {
    if (!a_app_name) {
        return -1;
    }
    
    dap_sdk_config_t config = {
        .modules = a_modules,
        .app_name = a_app_name,
        .log_level = L_INFO,
        .temp_dir = NULL,
        .log_file = NULL,
        .enable_debug = false
    };
    
    return dap_sdk_init(&config);
}

/**
 * @brief Deinitialize network subsystems
 */
static void s_deinit_network(void) {
    log_it(L_INFO, "Deinitializing DAP SDK Network subsystem");

    dap_net_common_deinit();

    log_it(L_INFO, "DAP SDK Network deinitialized");
}

/**
 * @brief Deinitialize IO subsystems
 */
static void s_deinit_io(void) {
    log_it(L_INFO, "Deinitializing DAP SDK IO subsystem");
    
    // Cleanup events system
    dap_events_deinit();
    
    log_it(L_INFO, "DAP SDK IO deinitialized");
}

/**
 * @brief Deinitialize crypto subsystems
 */
static void s_deinit_crypto(void) {
    log_it(L_INFO, "Deinitializing DAP SDK Crypto subsystem");
    
    // Crypto cleanup will be added when we have proper includes
    
    log_it(L_INFO, "DAP SDK Crypto deinitialized");
}

/**
 * @brief Deinitialize core subsystems
 */
static void s_deinit_core(void) {
    log_it(L_INFO, "Deinitializing DAP SDK Core subsystem");
    
    // Deinitialize config system
    dap_config_deinit();
    
    // Deinitialize common subsystem (should be last)
    dap_common_deinit();
    
    log_it(L_INFO, "DAP SDK Core deinitialized");
}

/**
 * @brief Deinitialize DAP SDK
 */
void dap_sdk_deinit(void) {
    if (!s_dap_sdk_initialized) {
        return;
    }
    
    log_it(L_INFO, "Deinitializing DAP SDK (modules were: 0x%08X)", s_current_modules);
    
    // Cleanup in reverse order of initialization based on what was initialized
    
    // Additional systems
    if (s_current_modules & DAP_SDK_MODULE_TEST) {
        s_deinit_test();
    }
    
    
    if (s_current_modules & DAP_SDK_MODULE_PLUGIN) {
        s_deinit_plugin();
    }
    
    // CLI and RPC modules
    if (s_current_modules & DAP_SDK_MODULE_JSON_RPC) {
        s_deinit_json_rpc();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_APP_CLI) {
        s_deinit_app_cli();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_CLI_SERVER) {
        s_deinit_cli_server();
    }
    
    // Advanced network modules
    if (s_current_modules & DAP_SDK_MODULE_NET_LINK_MGR) {
        s_deinit_net_link_mgr();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_NOTIFY) {
        s_deinit_net_notify();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_ENC) {
        s_deinit_net_enc();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_DNS) {
        s_deinit_net_dns();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_STREAM) {
        s_deinit_net_stream();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_HTTP) {
        s_deinit_net_http();
    }
    
    // Basic network modules
    if (s_current_modules & DAP_SDK_MODULE_NET_SERVER) {
        s_deinit_net_server();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_NET_CLIENT) {
        s_deinit_net_client();
    }
    
    // Core modules
    if (s_current_modules & DAP_SDK_MODULE_GLOBAL_DB) {
        s_deinit_global_db();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_IO) {
        s_deinit_io();
    }
    
    if (s_current_modules & DAP_SDK_MODULE_CRYPTO) {
        s_deinit_crypto();
    }
    
    // Core is always cleaned up last
    if (s_current_modules & DAP_SDK_MODULE_CORE) {
        s_deinit_core();
    }
    
    s_dap_sdk_initialized = false;
    s_current_modules = 0;
    
    // Final message - after common deinit this might not be visible
    printf("DAP SDK deinitialized successfully\n");
}

/**
 * @brief Check if DAP SDK is initialized
 */
bool dap_sdk_is_initialized(void) {
    return s_dap_sdk_initialized;
}

/**
 * @brief Get current initialized modules
 */
uint32_t dap_sdk_get_modules(void) {
    return s_current_modules;
}

// =============================================================================
// New module initialization functions
// =============================================================================

/**
 * @brief Initialize global database subsystem
 */
static int s_init_global_db(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Global DB initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize network client
 */
static int s_init_net_client(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Network Client initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize network server
 */
static int s_init_net_server(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Network Server initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize HTTP server/client
 */
static int s_init_net_http(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK HTTP Server/Client initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize stream protocol
 */
static int s_init_net_stream(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Stream Protocol initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize DNS server/client
 */
static int s_init_net_dns(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK DNS Server/Client initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize encryption server
 */
static int s_init_net_enc(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Encryption Server initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize notification server
 */
static int s_init_net_notify(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Notification Server initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize link manager
 */
static int s_init_net_link_mgr(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Link Manager initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize CLI server
 */
static int s_init_cli_server(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK CLI Server initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize application CLI
 */
static int s_init_app_cli(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Application CLI initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize JSON-RPC server
 */
static int s_init_json_rpc(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK JSON-RPC Server initialization (stub implementation)");
    return 0;
}

/**
 * @brief Initialize plugin system
 */
static int s_init_plugin(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Plugin System initialization (stub implementation)");
    return 0;
}


/**
 * @brief Initialize test framework
 */
static int s_init_test(const dap_sdk_config_t *a_config) {
    (void)a_config;
    log_it(L_INFO, "DAP SDK Test Framework initialization (stub implementation)");
    return 0;
}

// =============================================================================
// New module deinitialization functions
// =============================================================================

/**
 * @brief Deinitialize global database subsystem
 */
static void s_deinit_global_db(void) {
    log_it(L_INFO, "DAP SDK Global DB deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize network client
 */
static void s_deinit_net_client(void) {
    log_it(L_INFO, "DAP SDK Network Client deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize network server
 */
static void s_deinit_net_server(void) {
    log_it(L_INFO, "DAP SDK Network Server deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize HTTP server/client
 */
static void s_deinit_net_http(void) {
    log_it(L_INFO, "DAP SDK HTTP Server/Client deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize stream protocol
 */
static void s_deinit_net_stream(void) {
    log_it(L_INFO, "DAP SDK Stream Protocol deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize DNS server/client
 */
static void s_deinit_net_dns(void) {
    log_it(L_INFO, "DAP SDK DNS Server/Client deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize encryption server
 */
static void s_deinit_net_enc(void) {
    log_it(L_INFO, "DAP SDK Encryption Server deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize notification server
 */
static void s_deinit_net_notify(void) {
    log_it(L_INFO, "DAP SDK Notification Server deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize link manager
 */
static void s_deinit_net_link_mgr(void) {
    log_it(L_INFO, "DAP SDK Link Manager deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize CLI server
 */
static void s_deinit_cli_server(void) {
    log_it(L_INFO, "DAP SDK CLI Server deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize application CLI
 */
static void s_deinit_app_cli(void) {
    log_it(L_INFO, "DAP SDK Application CLI deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize JSON-RPC server
 */
static void s_deinit_json_rpc(void) {
    log_it(L_INFO, "DAP SDK JSON-RPC Server deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize plugin system
 */
static void s_deinit_plugin(void) {
    log_it(L_INFO, "DAP SDK Plugin System deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize AVRestream system
 */
static void s_deinit_avrestream(void) {
    log_it(L_INFO, "DAP SDK AVRestream System deinitialized (stub implementation)");
}

/**
 * @brief Deinitialize test framework
 */
static void s_deinit_test(void) {
    log_it(L_INFO, "DAP SDK Test Framework deinitialized (stub implementation)");
}
