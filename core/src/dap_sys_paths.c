/**
 * @file dap_sys_paths.c
 * @brief Centralized system paths management for DAP SDK
 * @details Implementation of unified API for accessing system directories
 *
 * @author Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * @copyright 2017-2025 (c) DeM Labs Inc. https://demlabs.net
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 * DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
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

#include "dap_sys_paths.h"
#include "dap_config.h"
#include "dap_strfuncs.h"

#define LOG_TAG "sys_paths"

/**
 * @brief Path mapping structure
 * @details Maps path types to their relative paths
 */
typedef struct {
    dap_sys_path_type_t type;
    const char *relative_path;
} dap_path_mapping_t;

/**
 * @brief Path configuration structure
 * @details Maps path types to their configuration parameters and default values
 */
typedef struct {
    dap_sys_path_type_t type;
    const char *config_section;
    const char *config_param;
    const char *default_path;
} dap_path_config_t;

/**
 * @brief Path configuration table
 * @details All paths are now configurable through external configuration
 */
static const dap_path_config_t s_path_configs[] = {
    { DAP_SYS_PATH_CONFIG,        "paths", "config_dir",        "etc"               },
    { DAP_SYS_PATH_NETWORK,       "paths", "network_dir",       "network"           },
    { DAP_SYS_PATH_CACHE,         "paths", "cache_dir",         "cache"             },
    { DAP_SYS_PATH_LOGS,          "paths", "logs_dir",          "var/log"           },
    { DAP_SYS_PATH_TMP,           "paths", "tmp_dir",           "tmp"               },
    { DAP_SYS_PATH_VAR_LIB,       "paths", "var_lib_dir",       "var/lib"           },
    { DAP_SYS_PATH_VAR_PLUGINS,   "paths", "plugins_dir",       "var/lib/plugins"   },
    { DAP_SYS_PATH_SHARE,         "paths", "share_dir",         "share"             },
    { DAP_SYS_PATH_SERVICES,      "paths", "services_dir",      "service.d"         },
    { DAP_SYS_PATH_GLOBAL_DB,     "paths", "global_db_dir",     "var/lib/global_db" },
    { DAP_SYS_PATH_GEOIP,         "paths", "geoip_dir",         "share/geoip"       },
    { DAP_SYS_PATH_CERTIFICATES,  "paths", "certificates_dir",  "share/ca"          }
};

/**
 * @brief Number of path configurations
 */
static const size_t s_path_configs_count = sizeof(s_path_configs) / sizeof(s_path_configs[0]);

/**
 * @brief Global configuration reference for path resolution
 */
static dap_config_t *s_config = NULL;

/**
 * @brief Get path configuration for given path type
 * @param a_path_type[in] Path type
 * @return Path configuration or NULL if not found
 */
static const dap_path_config_t* s_get_path_config(dap_sys_path_type_t a_path_type)
{
    for (size_t i = 0; i < s_path_configs_count; i++) {
        if (s_path_configs[i].type == a_path_type) {
            return &s_path_configs[i];
        }
    }
    return NULL;
}

/**
 * @brief Get configured path for given path type
 * @param a_path_type[in] Path type
 * @return Configured path string (from config or default)
 */
static const char* s_get_configured_path(dap_sys_path_type_t a_path_type)
{
    const dap_path_config_t *l_config = s_get_path_config(a_path_type);
    if (!l_config) {
        log_it(L_ERROR, "Unknown path type: %d", a_path_type);
        return NULL;
    }

    // If no configuration available, return default
    if (!s_config) {
        log_it(L_WARNING, "No configuration available, using default path for type %d", a_path_type);
        return l_config->default_path;
    }

    // Get path from configuration with fallback to default
    const char *l_configured_path = dap_config_get_item_str_default(
        s_config, l_config->config_section, l_config->config_param, l_config->default_path
    );
    
    return l_configured_path;
}

/**
 * @brief Get system path by type
 * @param a_path_type[in] Path type from dap_sys_path_type_t
 * @return Allocated string with full path (must be freed with DAP_DELETE)
 */
char* dap_sys_path_get(dap_sys_path_type_t a_path_type)
{
    const char *l_configured_path = s_get_configured_path(a_path_type);
    if (!l_configured_path) {
        return NULL;
    }

    // Check if path is absolute (starts with /)
    if (l_configured_path[0] == '/') {
        return dap_strdup(l_configured_path);
    }

    // For relative paths, determine the base directory
    const char *l_base_path = NULL;
    
    // Special cases for paths that should be relative to config path
    if (a_path_type == DAP_SYS_PATH_NETWORK || a_path_type == DAP_SYS_PATH_SERVICES) {
        l_base_path = dap_config_path();
        if (!l_base_path) {
            log_it(L_ERROR, "Config path not initialized for path type %d", a_path_type);
            return NULL;
        }
    } else {
        // Use system directory path for other paths
        l_base_path = g_sys_dir_path;
        if (!l_base_path) {
            log_it(L_ERROR, "System directory path not initialized");
            return NULL;
        }
    }

    return dap_strdup_printf("%s/%s", l_base_path, l_configured_path);
}

/**
 * @brief Get system path with additional subdirectory
 * @param a_path_type[in] Base path type
 * @param a_subdir[in] Additional subdirectory (can be NULL)
 * @return Allocated string with full path (must be freed with DAP_DELETE)
 */
char* dap_sys_path_get_subdir(dap_sys_path_type_t a_path_type, const char *a_subdir)
{
    char *l_base_path = dap_sys_path_get(a_path_type);
    if (!l_base_path) {
        return NULL;
    }

    if (!a_subdir || !a_subdir[0]) {
        return l_base_path;
    }

    char *l_full_path = dap_strdup_printf("%s/%s", l_base_path, a_subdir);
    DAP_DELETE(l_base_path);
    return l_full_path;
}

/**
 * @brief Get network configuration path for specific network
 * @param a_net_name[in] Network name
 * @return Allocated string with path to network configuration (must be freed with DAP_DELETE)
 */
char* dap_sys_path_get_network_config(const char *a_net_name)
{
    if (!a_net_name || !a_net_name[0]) {
        log_it(L_ERROR, "Network name is empty");
        return NULL;
    }

    const char *l_network_base = s_get_configured_path(DAP_SYS_PATH_NETWORK);
    if (!l_network_base) {
        log_it(L_ERROR, "Network base path not configured");
        return NULL;
    }

    return dap_strdup_printf("%s/%s/", l_network_base, a_net_name);
}

/**
 * @brief Get service configuration path
 * @return Allocated string with path to service configurations (must be freed with DAP_DELETE)
 */
char* dap_sys_path_get_service_config(void)
{
    return dap_sys_path_get(DAP_SYS_PATH_SERVICES);
}

/**
 * @brief Initialize system paths module
 * @return 0 on success, negative error code on failure
 */
int dap_sys_paths_init(dap_config_t *a_config)
{
    if (!g_sys_dir_path) {
        log_it(L_ERROR, "System directory path not set");
        return -1;
    }

    // Store configuration reference for path resolution
    s_config = a_config;
    
    log_it(L_NOTICE, "System paths module initialized with base path: %s", g_sys_dir_path);
    if (s_config) {
        log_it(L_DEBUG, "Configuration-based path resolution enabled");
    } else {
        log_it(L_WARNING, "No configuration provided, using default paths only");
    }
    
    return 0;
}

/**
 * @brief Cleanup system paths module
 */
void dap_sys_paths_deinit(void)
{
    s_config = NULL;
    log_it(L_DEBUG, "System paths module deinitialized");
}
