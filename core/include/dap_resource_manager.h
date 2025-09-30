/**
 * @file dap_resource_manager.h
 * @brief Unified resource management for certificates, wallets, and paths
 * @details Provides centralized API to eliminate inconsistencies between CLI and SDK
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
#include "dap_config.h"

// Forward declarations
typedef struct dap_cert dap_cert_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resource types enumeration
 * @details Defines all resource types managed by the unified system
 */
typedef enum dap_resource_type {
    DAP_RESOURCE_TYPE_CERTIFICATE,     /**< Certificate files (.dcert) */
    DAP_RESOURCE_TYPE_WALLET,          /**< Wallet files (.dwallet) */
    DAP_RESOURCE_TYPE_CONFIG,          /**< Configuration files (.cfg) */
    DAP_RESOURCE_TYPE_KEY,             /**< Key files (.dkey) */
    DAP_RESOURCE_TYPE_UNKNOWN
} dap_resource_type_t;

/**
 * @brief Search strategy for resource lookup
 * @details Defines how resources should be located
 */
typedef enum dap_resource_search_strategy {
    DAP_RESOURCE_SEARCH_CACHE_FIRST,   /**< Check memory cache first, then filesystem */
    DAP_RESOURCE_SEARCH_FILE_FIRST,    /**< Check filesystem first, then cache */
    DAP_RESOURCE_SEARCH_CACHE_ONLY,    /**< Only check memory cache */
    DAP_RESOURCE_SEARCH_FILE_ONLY,     /**< Only check filesystem */
    DAP_RESOURCE_SEARCH_ALL_PATHS      /**< Search in all configured paths */
} dap_resource_search_strategy_t;

/**
 * @brief Resource search context
 * @details Contains search parameters and results
 */
typedef struct dap_resource_context {
    dap_resource_type_t type;                    /**< Resource type */
    dap_resource_search_strategy_t strategy;     /**< Search strategy */
    const char *name;                           /**< Resource name (without extension) */
    const char *explicit_path;                  /**< Explicit path (overrides search) */
    char **search_paths;                        /**< Array of search paths */
    uint16_t search_paths_count;                /**< Number of search paths */
    bool use_extension;                         /**< Auto-add file extension */
    const char *found_path;                     /**< Path where resource was found */
} dap_resource_context_t;

/**
 * @brief Initialize resource manager
 * @param a_config[in] Configuration object
 * @return 0 on success, negative error code on failure
 * @details Must be called before using any resource manager functions
 */
int dap_resource_manager_init(dap_config_t *a_config);

/**
 * @brief Cleanup resource manager
 */
void dap_resource_manager_deinit(void);

/**
 * @brief Get unified search paths for resource type
 * @param a_type[in] Resource type
 * @param a_paths_count[out] Number of paths returned
 * @return Array of search paths (must be freed with dap_resource_paths_free)
 * @details Returns all configured paths for the given resource type
 */
char** dap_resource_get_search_paths(dap_resource_type_t a_type, uint16_t *a_paths_count);

/**
 * @brief Free search paths array
 * @param a_paths[in] Paths array to free
 * @param a_count[in] Number of paths in array
 */
void dap_resource_paths_free(char **a_paths, uint16_t a_count);

/**
 * @brief Find resource using unified search
 * @param a_context[in,out] Search context
 * @return Full path to resource or NULL if not found (must be freed with DAP_DELETE)
 * @details Unified search function that eliminates CLI/SDK inconsistencies
 */
char* dap_resource_find(dap_resource_context_t *a_context);

/**
 * @brief Load certificate using unified API
 * @param a_cert_name[in] Certificate name or path
 * @param a_strategy[in] Search strategy
 * @return Certificate object or NULL if not found
 * @details Replaces inconsistent certificate loading between CLI and SDK
 */
dap_cert_t* dap_resource_cert_load(const char *a_cert_name, dap_resource_search_strategy_t a_strategy);

/**
 * @brief Get certificate storage path
 * @param a_primary_only[in] Return only primary path (CLI compatibility)
 * @return Primary certificate path or NULL (do not free)
 * @details Provides CLI-compatible single path while maintaining SDK multi-path support
 */
const char* dap_resource_cert_get_storage_path(bool a_primary_only);

/**
 * @brief Get wallet storage path
 * @return Wallet storage path or NULL (do not free)
 * @details Unified wallet path accessor for CLI/SDK compatibility
 */
const char* dap_resource_wallet_get_storage_path(void);

/**
 * @brief Check if resource exists
 * @param a_name[in] Resource name
 * @param a_type[in] Resource type
 * @param a_found_path[out] Full path where found (optional, must be freed with DAP_DELETE)
 * @return true if resource exists, false otherwise
 */
bool dap_resource_exists(const char *a_name, dap_resource_type_t a_type, char **a_found_path);

/**
 * @brief Save resource to appropriate location
 * @param a_name[in] Resource name
 * @param a_type[in] Resource type
 * @param a_data[in] Resource data
 * @param a_data_size[in] Data size
 * @param a_use_primary_path[in] Save to primary path only (CLI mode)
 * @return Full path where saved or NULL on error (must be freed with DAP_DELETE)
 */
char* dap_resource_save(const char *a_name, dap_resource_type_t a_type, 
                       const void *a_data, size_t a_data_size, bool a_use_primary_path);

/**
 * @brief Get file extension for resource type
 * @param a_type[in] Resource type
 * @return File extension string (including dot)
 */
const char* dap_resource_get_extension(dap_resource_type_t a_type);

/**
 * @brief Create default resource context
 * @param a_name[in] Resource name
 * @param a_type[in] Resource type
 * @return Initialized context structure
 */
dap_resource_context_t dap_resource_context_create(const char *a_name, dap_resource_type_t a_type);

/**
 * @brief Validate resource name
 * @param a_name[in] Resource name to validate
 * @param a_type[in] Resource type
 * @return true if name is valid, false otherwise
 */
bool dap_resource_name_validate(const char *a_name, dap_resource_type_t a_type);

/**
 * @brief Get resource type from file extension
 * @param a_filename[in] Filename with extension
 * @return Detected resource type
 */
dap_resource_type_t dap_resource_type_from_filename(const char *a_filename);

#ifdef __cplusplus
}
#endif
