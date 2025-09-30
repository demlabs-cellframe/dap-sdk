/**
 * @file dap_resource_manager.c
 * @brief Implementation of unified resource management
 * @details Centralized system to eliminate CLI/SDK inconsistencies
 *
 * @author Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * @copyright 2017-2025 (c) DeM Labs Inc. https://demlabs.net
 * All rights reserved.
 */

#include "dap_resource_manager.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "resource_manager"

/**
 * @brief Resource configuration mapping
 */
typedef struct {
    dap_resource_type_t type;
    const char *config_section;
    const char *config_param;
    const char *extension;
    const char *default_path;
} dap_resource_config_map_t;

/**
 * @brief Resource configuration table
 */
static const dap_resource_config_map_t s_resource_configs[] = {
    { DAP_RESOURCE_TYPE_CERTIFICATE, "resources", "ca_folders",    ".dcert",   "share/ca" },
    { DAP_RESOURCE_TYPE_WALLET,      "resources", "wallets_path",  ".dwallet", "var/lib/wallets" },
    { DAP_RESOURCE_TYPE_CONFIG,      NULL,        NULL,            ".cfg",     "etc" },
    { DAP_RESOURCE_TYPE_KEY,         "resources", "keys_path",     ".dkey",    "var/lib/keys" }
};

static const size_t s_resource_configs_count = sizeof(s_resource_configs) / sizeof(s_resource_configs[0]);

/**
 * @brief Global configuration reference
 */
static dap_config_t *s_config = NULL;

/**
 * @brief Cached paths for performance
 */
static char *s_primary_cert_path = NULL;
static char *s_wallet_path = NULL;

/**
 * @brief Get resource configuration by type
 */
static const dap_resource_config_map_t* s_get_resource_config(dap_resource_type_t a_type)
{
    for (size_t i = 0; i < s_resource_configs_count; i++) {
        if (s_resource_configs[i].type == a_type) {
            return &s_resource_configs[i];
        }
    }
    return NULL;
}

/**
 * @brief Initialize resource manager
 */
int dap_resource_manager_init(dap_config_t *a_config)
{
    if (!a_config) {
        log_it(L_ERROR, "Configuration object is NULL");
        return -1;
    }

    s_config = a_config;

    // Cache primary certificate path (CLI compatibility)
    uint16_t l_ca_folders_count = 0;
    char **l_ca_folders = dap_config_get_item_str_path_array(s_config, "resources", "ca_folders", &l_ca_folders_count);
    if (l_ca_folders && l_ca_folders_count > 0) {
        s_primary_cert_path = dap_strdup(l_ca_folders[0]);
        // Remove trailing slash for CLI compatibility
        size_t len = strlen(s_primary_cert_path);
        if (len > 0 && s_primary_cert_path[len - 1] == '/') {
            s_primary_cert_path[len - 1] = '\0';
        }
    }
    dap_config_get_item_str_path_array_free(l_ca_folders, l_ca_folders_count);

    // Cache wallet path
    s_wallet_path = dap_config_get_item_str_path_default(s_config, "resources", "wallets_path", "var/lib/wallets");

    log_it(L_NOTICE, "Resource manager initialized");
    log_it(L_DEBUG, "Primary cert path: %s", s_primary_cert_path ? s_primary_cert_path : "NULL");
    log_it(L_DEBUG, "Wallet path: %s", s_wallet_path ? s_wallet_path : "NULL");

    return 0;
}

/**
 * @brief Cleanup resource manager
 */
void dap_resource_manager_deinit(void)
{
    DAP_DELETE(s_primary_cert_path);
    // Note: s_wallet_path is managed by dap_config, don't free it
    s_config = NULL;
    log_it(L_DEBUG, "Resource manager deinitialized");
}

/**
 * @brief Get unified search paths for resource type
 */
char** dap_resource_get_search_paths(dap_resource_type_t a_type, uint16_t *a_paths_count)
{
    if (!a_paths_count) {
        log_it(L_ERROR, "Paths count pointer is NULL");
        return NULL;
    }

    *a_paths_count = 0;
    
    if (!s_config) {
        log_it(L_ERROR, "Resource manager not initialized");
        return NULL;
    }

    const dap_resource_config_map_t *l_config = s_get_resource_config(a_type);
    if (!l_config) {
        log_it(L_ERROR, "Unknown resource type: %d", a_type);
        return NULL;
    }

    // Handle different resource types
    switch (a_type) {
        case DAP_RESOURCE_TYPE_CERTIFICATE:
            return dap_config_get_item_str_path_array(s_config, l_config->config_section, 
                                                     l_config->config_param, a_paths_count);
        
        case DAP_RESOURCE_TYPE_WALLET:
        case DAP_RESOURCE_TYPE_KEY: {
            char *l_path = dap_config_get_item_str_path_default(s_config, l_config->config_section,
                                                               l_config->config_param, l_config->default_path);
            if (l_path) {
                char **l_paths = DAP_NEW_Z_SIZE(char*, sizeof(char*));
                l_paths[0] = dap_strdup(l_path);
                *a_paths_count = 1;
                return l_paths;
            }
            break;
        }
        
        default:
            log_it(L_WARNING, "Resource type %d not implemented", a_type);
            break;
    }

    return NULL;
}

/**
 * @brief Free search paths array
 */
void dap_resource_paths_free(char **a_paths, uint16_t a_count)
{
    if (!a_paths) return;
    
    for (uint16_t i = 0; i < a_count; i++) {
        DAP_DELETE(a_paths[i]);
    }
    DAP_DELETE(a_paths);
}

/**
 * @brief Find resource using unified search
 */
char* dap_resource_find(dap_resource_context_t *a_context)
{
    if (!a_context || !a_context->name) {
        log_it(L_ERROR, "Invalid search context");
        return NULL;
    }

    // If explicit path provided, use it directly
    if (a_context->explicit_path) {
        if (access(a_context->explicit_path, F_OK) == 0) {
            return dap_strdup(a_context->explicit_path);
        }
        return NULL;
    }

    const dap_resource_config_map_t *l_config = s_get_resource_config(a_context->type);
    if (!l_config) {
        log_it(L_ERROR, "Unknown resource type: %d", a_context->type);
        return NULL;
    }

    // Get search paths
    uint16_t l_paths_count = 0;
    char **l_search_paths = a_context->search_paths 
        ? a_context->search_paths 
        : dap_resource_get_search_paths(a_context->type, &l_paths_count);
    
    if (!l_search_paths) {
        log_it(L_DEBUG, "No search paths for resource type %d", a_context->type);
        return NULL;
    }

    uint16_t l_actual_count = a_context->search_paths_count > 0 
        ? a_context->search_paths_count 
        : l_paths_count;

    // Search in all paths
    char *l_found_path = NULL;
    for (uint16_t i = 0; i < l_actual_count; i++) {
        char *l_full_path;
        
        if (a_context->use_extension) {
            // Check if name already has extension
            if (strstr(a_context->name, l_config->extension)) {
                l_full_path = dap_strdup_printf("%s/%s", l_search_paths[i], a_context->name);
            } else {
                l_full_path = dap_strdup_printf("%s/%s%s", l_search_paths[i], 
                                               a_context->name, l_config->extension);
            }
        } else {
            l_full_path = dap_strdup_printf("%s/%s", l_search_paths[i], a_context->name);
        }

        if (access(l_full_path, F_OK) == 0) {
            l_found_path = l_full_path;
            a_context->found_path = l_search_paths[i]; // Set where it was found
            break;
        }

        DAP_DELETE(l_full_path);
    }

    // Free search paths if we allocated them
    if (!a_context->search_paths) {
        dap_resource_paths_free(l_search_paths, l_paths_count);
    }

    return l_found_path;
}

/**
 * @brief Load certificate using unified API
 */
dap_cert_t* dap_resource_cert_load(const char *a_cert_name, dap_resource_search_strategy_t a_strategy)
{
    if (!a_cert_name) {
        log_it(L_ERROR, "Certificate name is NULL");
        return NULL;
    }

    // For now, this is a placeholder that will be implemented 
    // after the main cert system integrates with resource manager
    log_it(L_DEBUG, "Certificate loading via resource manager: %s", a_cert_name);
    
    // Return NULL for now - the actual implementation will be done
    // when dap_cert.c properly includes this system
    return NULL;
}

/**
 * @brief Get certificate storage path
 */
const char* dap_resource_cert_get_storage_path(bool a_primary_only)
{
    if (a_primary_only) {
        return s_primary_cert_path; // CLI compatibility
    }
    
    // For SDK, could return first path from array, but this is primary path anyway
    return s_primary_cert_path;
}

/**
 * @brief Get wallet storage path
 */
const char* dap_resource_wallet_get_storage_path(void)
{
    return s_wallet_path;
}

/**
 * @brief Check if resource exists
 */
bool dap_resource_exists(const char *a_name, dap_resource_type_t a_type, char **a_found_path)
{
    dap_resource_context_t l_context = dap_resource_context_create(a_name, a_type);
    l_context.use_extension = true;
    
    char *l_path = dap_resource_find(&l_context);
    if (l_path) {
        if (a_found_path) {
            *a_found_path = l_path; // Transfer ownership
        } else {
            DAP_DELETE(l_path);
        }
        return true;
    }
    
    return false;
}

/**
 * @brief Save resource to appropriate location
 */
char* dap_resource_save(const char *a_name, dap_resource_type_t a_type, 
                       const void *a_data, size_t a_data_size, bool a_use_primary_path)
{
    if (!a_name || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid save parameters");
        return NULL;
    }

    const dap_resource_config_map_t *l_config = s_get_resource_config(a_type);
    if (!l_config) {
        log_it(L_ERROR, "Unknown resource type: %d", a_type);
        return NULL;
    }

    const char *l_storage_path = NULL;
    
    // Get appropriate storage path
    switch (a_type) {
        case DAP_RESOURCE_TYPE_CERTIFICATE:
            l_storage_path = dap_resource_cert_get_storage_path(a_use_primary_path);
            break;
        case DAP_RESOURCE_TYPE_WALLET:
            l_storage_path = dap_resource_wallet_get_storage_path();
            break;
        default:
            log_it(L_ERROR, "Save not implemented for resource type %d", a_type);
            return NULL;
    }

    if (!l_storage_path) {
        log_it(L_ERROR, "No storage path for resource type %d", a_type);
        return NULL;
    }

    // Create full path
    char *l_full_path;
    if (strstr(a_name, l_config->extension)) {
        l_full_path = dap_strdup_printf("%s/%s", l_storage_path, a_name);
    } else {
        l_full_path = dap_strdup_printf("%s/%s%s", l_storage_path, a_name, l_config->extension);
    }

    // Ensure directory exists
    dap_mkdir_with_parents(l_storage_path);

    // Save file
    FILE *l_file = fopen(l_full_path, "wb");
    if (!l_file) {
        log_it(L_ERROR, "Cannot create file: %s", l_full_path);
        DAP_DELETE(l_full_path);
        return NULL;
    }

    size_t l_written = fwrite(a_data, 1, a_data_size, l_file);
    fclose(l_file);

    if (l_written != a_data_size) {
        log_it(L_ERROR, "Failed to write complete data to %s", l_full_path);
        unlink(l_full_path);
        DAP_DELETE(l_full_path);
        return NULL;
    }

    log_it(L_DEBUG, "Saved resource %s to %s", a_name, l_full_path);
    return l_full_path;
}

/**
 * @brief Get file extension for resource type
 */
const char* dap_resource_get_extension(dap_resource_type_t a_type)
{
    const dap_resource_config_map_t *l_config = s_get_resource_config(a_type);
    return l_config ? l_config->extension : NULL;
}

/**
 * @brief Create default resource context
 */
dap_resource_context_t dap_resource_context_create(const char *a_name, dap_resource_type_t a_type)
{
    dap_resource_context_t l_context = {0};
    l_context.type = a_type;
    l_context.strategy = DAP_RESOURCE_SEARCH_ALL_PATHS;
    l_context.name = a_name;
    l_context.explicit_path = NULL;
    l_context.search_paths = NULL;
    l_context.search_paths_count = 0;
    l_context.use_extension = false;
    l_context.found_path = NULL;
    return l_context;
}

/**
 * @brief Validate resource name
 */
bool dap_resource_name_validate(const char *a_name, dap_resource_type_t a_type)
{
    if (!a_name || strlen(a_name) == 0) {
        return false;
    }

    // Basic validation: no dangerous characters
    const char *l_forbidden = "\\:*?\"<>|";
    for (const char *p = l_forbidden; *p; p++) {
        if (strchr(a_name, *p)) {
            return false;
        }
    }

    // Name length check
    if (strlen(a_name) > 255) {
        return false;
    }

    return true;
}

/**
 * @brief Get resource type from file extension
 */
dap_resource_type_t dap_resource_type_from_filename(const char *a_filename)
{
    if (!a_filename) {
        return DAP_RESOURCE_TYPE_UNKNOWN;
    }

    for (size_t i = 0; i < s_resource_configs_count; i++) {
        if (strstr(a_filename, s_resource_configs[i].extension)) {
            return s_resource_configs[i].type;
        }
    }

    return DAP_RESOURCE_TYPE_UNKNOWN;
}
