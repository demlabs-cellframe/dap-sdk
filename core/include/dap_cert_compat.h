/**
 * @file dap_cert_compat.h
 * @brief Compatibility layer for unified certificate management
 * @details Provides backward compatibility wrapper functions
 *
 * @author Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * @copyright 2017-2025 (c) DeM Labs Inc. https://demlabs.net
 * All rights reserved.
 */

#pragma once

#include "dap_cert.h"
#include "dap_resource_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enhanced certificate loading with unified search
 * @param a_cert_name[in] Certificate name or path  
 * @param a_search_all_paths[in] If true, search in all ca_folders (SDK mode), if false, use primary only (CLI mode)
 * @return Certificate object or NULL if not found
 * @details Provides compatibility between CLI and SDK certificate loading approaches
 */
static inline dap_cert_t* dap_cert_find_by_name_enhanced(const char *a_cert_name, bool a_search_all_paths)
{
    dap_resource_search_strategy_t l_strategy = a_search_all_paths 
        ? DAP_RESOURCE_SEARCH_ALL_PATHS 
        : DAP_RESOURCE_SEARCH_CACHE_FIRST;
    return dap_resource_cert_load(a_cert_name, l_strategy);
}

/**
 * @brief Get certificate folder path (CLI compatibility)
 * @param a_folder_index[in] Folder index (0 for primary)
 * @return Folder path or NULL
 * @details Maintains CLI compatibility while supporting multiple paths
 */
static inline const char* dap_cert_get_folder_enhanced(int a_folder_index)
{
    if (a_folder_index == 0) {
        return dap_resource_cert_get_storage_path(true); // Primary path
    }
    
    // For non-primary paths, use resource manager
    uint16_t l_paths_count = 0;
    char **l_paths = dap_resource_get_search_paths(DAP_RESOURCE_TYPE_CERTIFICATE, &l_paths_count);
    
    if (l_paths && a_folder_index < l_paths_count) {
        const char *l_result = l_paths[a_folder_index];
        // Note: Cannot free here as we're returning a pointer
        // Caller should use this immediately
        return l_result;
    }
    
    if (l_paths) {
        dap_resource_paths_free(l_paths, l_paths_count);
    }
    
    return NULL;
}

/**
 * @brief Check if certificate exists in any configured path
 * @param a_cert_name[in] Certificate name
 * @return true if certificate exists
 */
static inline bool dap_cert_exists(const char *a_cert_name)
{
    return dap_resource_exists(a_cert_name, DAP_RESOURCE_TYPE_CERTIFICATE, NULL);
}

/**
 * @brief Get full path to certificate
 * @param a_cert_name[in] Certificate name
 * @return Full path or NULL (must be freed with DAP_DELETE)
 */
static inline char* dap_cert_get_full_path(const char *a_cert_name)
{
    char *l_found_path = NULL;
    dap_resource_exists(a_cert_name, DAP_RESOURCE_TYPE_CERTIFICATE, &l_found_path);
    return l_found_path;
}

#ifdef __cplusplus
}
#endif
