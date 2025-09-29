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
 * @brief Path mappings table
 * @details Defines relative paths for each system path type
 */
static const dap_path_mapping_t s_path_mappings[] = {
    { DAP_SYS_PATH_CONFIG,        "etc"               },
    { DAP_SYS_PATH_NETWORK,       "network"           },
    { DAP_SYS_PATH_CACHE,         "cache"             },
    { DAP_SYS_PATH_LOGS,          "var/log"           },
    { DAP_SYS_PATH_TMP,           "tmp"               },
    { DAP_SYS_PATH_VAR_LIB,       "var/lib"           },
    { DAP_SYS_PATH_VAR_PLUGINS,   "var/lib/plugins"   },
    { DAP_SYS_PATH_SHARE,         "share"             },
    { DAP_SYS_PATH_SERVICES,      "service.d"         },
    { DAP_SYS_PATH_GLOBAL_DB,     "var/lib/global_db" },
    { DAP_SYS_PATH_GEOIP,         "share/geoip"       },
    { DAP_SYS_PATH_CERTIFICATES,  "share/ca"          }
};

/**
 * @brief Number of path mappings
 */
static const size_t s_path_mappings_count = sizeof(s_path_mappings) / sizeof(s_path_mappings[0]);

/**
 * @brief Get relative path for given path type
 * @param a_path_type[in] Path type
 * @return Relative path string or NULL if not found
 */
static const char* s_get_relative_path(dap_sys_path_type_t a_path_type)
{
    for (size_t i = 0; i < s_path_mappings_count; i++) {
        if (s_path_mappings[i].type == a_path_type) {
            return s_path_mappings[i].relative_path;
        }
    }
    return NULL;
}

/**
 * @brief Get system path by type
 * @param a_path_type[in] Path type from dap_sys_path_type_t
 * @return Allocated string with full path (must be freed with DAP_DELETE)
 */
char* dap_sys_path_get(dap_sys_path_type_t a_path_type)
{
    if (!g_sys_dir_path) {
        log_it(L_ERROR, "System directory path not initialized");
        return NULL;
    }

    const char *l_relative_path = s_get_relative_path(a_path_type);
    if (!l_relative_path) {
        log_it(L_ERROR, "Unknown path type: %d", a_path_type);
        return NULL;
    }

    // Special case for network paths - they are relative to config path
    if (a_path_type == DAP_SYS_PATH_NETWORK) {
        const char *l_config_path = dap_config_path();
        if (!l_config_path) {
            log_it(L_ERROR, "Config path not initialized");
            return NULL;
        }
        return dap_strdup_printf("%s/%s", l_config_path, l_relative_path);
    }

    // Special case for services - they are relative to config path
    if (a_path_type == DAP_SYS_PATH_SERVICES) {
        const char *l_config_path = dap_config_path();
        if (!l_config_path) {
            log_it(L_ERROR, "Config path not initialized");
            return NULL;
        }
        return dap_strdup_printf("%s/%s", l_config_path, l_relative_path);
    }

    return dap_strdup_printf("%s/%s", g_sys_dir_path, l_relative_path);
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

    return dap_strdup_printf("network/%s/", a_net_name);
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
int dap_sys_paths_init(void)
{
    if (!g_sys_dir_path) {
        log_it(L_ERROR, "System directory path not set");
        return -1;
    }

    log_it(L_NOTICE, "System paths module initialized with base path: %s", g_sys_dir_path);
    return 0;
}

/**
 * @brief Cleanup system paths module
 */
void dap_sys_paths_deinit(void)
{
    log_it(L_DEBUG, "System paths module deinitialized");
}
