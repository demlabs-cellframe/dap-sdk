/**
 * @file dap_sys_paths.h
 * @brief Centralized system paths management for DAP SDK
 * @details Provides unified API for accessing system directories to avoid hardcoded paths
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

#pragma once

#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System path types enumeration
 * @details Defines all standard system directories used in DAP SDK
 */
typedef enum dap_sys_path_type {
    DAP_SYS_PATH_CONFIG,        /**< Configuration directory (etc) */
    DAP_SYS_PATH_NETWORK,       /**< Network configurations (network) */
    DAP_SYS_PATH_CACHE,         /**< Cache directory (cache) */
    DAP_SYS_PATH_LOGS,          /**< Log files directory (var/log) */
    DAP_SYS_PATH_TMP,           /**< Temporary files (tmp) */
    DAP_SYS_PATH_VAR_LIB,       /**< Variable library data (var/lib) */
    DAP_SYS_PATH_VAR_PLUGINS,   /**< Plugins directory (var/lib/plugins) */
    DAP_SYS_PATH_SHARE,         /**< Shared data (share) */
    DAP_SYS_PATH_SERVICES,      /**< Services configuration (service.d) */
    DAP_SYS_PATH_GLOBAL_DB,     /**< Global database (var/lib/global_db) */
    DAP_SYS_PATH_GEOIP,         /**< GeoIP database (share/geoip) */
    DAP_SYS_PATH_CERTIFICATES   /**< Certificates (share/ca) */
} dap_sys_path_type_t;

/**
 * @brief Get system path by type
 * @param a_path_type[in] Path type from dap_sys_path_type_t
 * @return Allocated string with full path (must be freed with DAP_DELETE)
 * @details Returns NULL on error. Path is constructed relative to g_sys_dir_path
 */
char* dap_sys_path_get(dap_sys_path_type_t a_path_type);

/**
 * @brief Get system path with additional subdirectory
 * @param a_path_type[in] Base path type
 * @param a_subdir[in] Additional subdirectory (can be NULL)
 * @return Allocated string with full path (must be freed with DAP_DELETE)
 * @details Appends subdirectory to base path with proper separator
 */
char* dap_sys_path_get_subdir(dap_sys_path_type_t a_path_type, const char *a_subdir);

/**
 * @brief Get network configuration path for specific network
 * @param a_net_name[in] Network name
 * @return Allocated string with path to network configuration (must be freed with DAP_DELETE)
 * @details Returns path like "network/{net_name}/"
 */
char* dap_sys_path_get_network_config(const char *a_net_name);

/**
 * @brief Get service configuration path
 * @return Allocated string with path to service configurations (must be freed with DAP_DELETE)
 * @details Returns path like "{config_path}/service.d"
 */
char* dap_sys_path_get_service_config(void);

/**
 * @brief Initialize system paths module
 * @return 0 on success, negative error code on failure
 * @details Must be called after g_sys_dir_path is set
 */
int dap_sys_paths_init(void);

/**
 * @brief Cleanup system paths module
 */
void dap_sys_paths_deinit(void);

#ifdef __cplusplus
}
#endif
