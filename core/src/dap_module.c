/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe network https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "dap_common.h"
#include "dap_module.h"
#include "dap_strfuncs.h"
#include "uthash.h"
#include "dap_list.h"

#define LOG_TAG "dap_module"

/**
 * @brief Module registry entry
 */
typedef struct dap_module_registry_entry {
    char name[128];                         ///< Module name
    unsigned int version;                   ///< Module version
    const char *dependencies;               ///< Dependency string (optional)
    dap_module_callback_init_t init_cb;     ///< Initialization callback
    dap_module_args_t *init_args;          ///< Initialization arguments
    dap_module_callback_deinit_t deinit_cb; ///< Deinitialization callback
    bool initialized;                      ///< Initialization state
    UT_hash_handle hh;                      ///< Hash table handle (keyed by name)
} dap_module_registry_entry_t;

// Global module registry
static dap_module_registry_entry_t *s_module_registry = NULL;
static bool s_module_system_initialized = false;

/**
 * @brief Add a module to the registry
 * 
 * @param a_name Module name (must be unique)
 * @param a_version Module version
 * @param a_dependencies Dependency string (comma-separated, optional)
 * @param a_init_callback Initialization callback function
 * @param a_init_args Initialization arguments array (optional)
 * @param a_deinit_callback Deinitialization callback function (optional)
 * @return 0 on success, -1 on failure (e.g., duplicate name)
 */
int dap_module_add(const char *a_name, unsigned int a_version, const char *a_dependencies,
                   dap_module_callback_init_t a_init_callback, dap_module_args_t a_init_args[],
                   dap_module_callback_deinit_t a_deinit_callback)
{
    if (!a_name || !a_init_callback) {
        log_it(L_ERROR, "dap_module_add: Invalid arguments");
        return -1;
    }
    
    // Check if module already registered
    dap_module_registry_entry_t *l_existing = NULL;
    HASH_FIND_STR(s_module_registry, a_name, l_existing);
    if (l_existing) {
        log_it(L_WARNING, "dap_module_add: Module '%s' already registered (version %u)", 
               a_name, l_existing->version);
        return -1;
    }
    
    // Create new registry entry
    dap_module_registry_entry_t *l_entry = DAP_NEW_Z(dap_module_registry_entry_t);
    if (!l_entry) {
        log_it(L_ERROR, "dap_module_add: Failed to allocate memory for module '%s'", a_name);
        return -1;
    }
    
    strncpy(l_entry->name, a_name, sizeof(l_entry->name) - 1);
    l_entry->name[sizeof(l_entry->name) - 1] = '\0';
    l_entry->version = a_version;
    l_entry->dependencies = a_dependencies ? dap_strdup(a_dependencies) : NULL;
    l_entry->init_cb = a_init_callback;
    l_entry->init_args = a_init_args;
    l_entry->deinit_cb = a_deinit_callback;
    l_entry->initialized = false;
    
    // Add to hash table
    HASH_ADD_STR(s_module_registry, name, l_entry);
    
    log_it(L_DEBUG, "dap_module_add: Registered module '%s' (version %u)", a_name, a_version);
    return 0;
}

/**
 * @brief Find a module by name
 * 
 * @param a_name Module name
 * @return Module registry entry or NULL if not found
 */
static dap_module_registry_entry_t *s_module_find(const char *a_name)
{
    if (!a_name) {
        return NULL;
    }
    
    dap_module_registry_entry_t *l_entry = NULL;
    HASH_FIND_STR(s_module_registry, a_name, l_entry);
    return l_entry;
}

/**
 * @brief Parse comma-separated dependency string into array of module names
 * 
 * @param a_dependencies Dependency string (comma-separated, can be NULL)
 * @param a_dep_names Output array of dependency names (allocated, caller must free)
 * @param a_dep_count Output number of dependencies
 * @return 0 on success, -1 on failure
 */
static int s_parse_dependencies(const char *a_dependencies, char ***a_dep_names, size_t *a_dep_count)
{
    if (!a_dependencies || !a_dependencies[0]) {
        *a_dep_names = NULL;
        *a_dep_count = 0;
        return 0;
    }
    
    // Count dependencies
    size_t l_count = 1;
    const char *l_ptr = a_dependencies;
    while (*l_ptr) {
        if (*l_ptr == ',') {
            l_count++;
        }
        l_ptr++;
    }
    
    // Allocate array of pointers
    char **l_names = DAP_NEW_Z_COUNT(char *, l_count);
    if (!l_names) {
        log_it(L_ERROR, "s_parse_dependencies: Failed to allocate memory");
        return -1;
    }
    
    // Parse dependencies
    size_t l_idx = 0;
    const char *l_start = a_dependencies;
    l_ptr = a_dependencies;
    
    while (*l_ptr) {
        if (*l_ptr == ',') {
            // Extract dependency name
            size_t l_len = l_ptr - l_start;
            if (l_len > 0) {
                l_names[l_idx] = DAP_NEW_Z_SIZE(char, l_len + 1);
                if (!l_names[l_idx]) {
                    // Free already allocated names
                    for (size_t i = 0; i < l_idx; i++) {
                        DAP_DELETE(l_names[i]);
                    }
                    DAP_DELETE(l_names);
                    return -1;
                }
                strncpy(l_names[l_idx], l_start, l_len);
                l_names[l_idx][l_len] = '\0';
                // Trim whitespace
                while (*l_names[l_idx] == ' ') {
                    memmove(l_names[l_idx], l_names[l_idx] + 1, strlen(l_names[l_idx]));
                }
                l_idx++;
            }
            l_start = l_ptr + 1;
        }
        l_ptr++;
    }
    
    // Last dependency
    size_t l_len = l_ptr - l_start;
    if (l_len > 0) {
        l_names[l_idx] = DAP_NEW_Z_SIZE(char, l_len + 1);
        if (!l_names[l_idx]) {
            // Free already allocated names
            for (size_t i = 0; i < l_idx; i++) {
                DAP_DELETE(l_names[i]);
            }
            DAP_DELETE(l_names);
            return -1;
        }
        strncpy(l_names[l_idx], l_start, l_len);
        l_names[l_idx][l_len] = '\0';
        // Trim whitespace
        while (*l_names[l_idx] == ' ') {
            memmove(l_names[l_idx], l_names[l_idx] + 1, strlen(l_names[l_idx]));
        }
        l_idx++;
    }
    
    *a_dep_names = l_names;
    *a_dep_count = l_idx;
    return 0;
}

/**
 * @brief Free dependency array
 */
static void s_free_dependencies(char **a_dep_names, size_t a_dep_count)
{
    if (a_dep_names) {
        for (size_t i = 0; i < a_dep_count; i++) {
            DAP_DELETE(a_dep_names[i]);
        }
        DAP_DELETE(a_dep_names);
    }
}

/**
 * @brief Check if all dependencies of a module are initialized
 * 
 * @param a_entry Module entry
 * @return true if all dependencies are initialized, false otherwise
 */
static bool s_all_dependencies_initialized(dap_module_registry_entry_t *a_entry)
{
    if (!a_entry->dependencies || !a_entry->dependencies[0]) {
        return true; // No dependencies
    }
    
    char **l_dep_names = NULL;
    size_t l_dep_count = 0;
    
    if (s_parse_dependencies(a_entry->dependencies, &l_dep_names, &l_dep_count) != 0) {
        log_it(L_ERROR, "s_all_dependencies_initialized: Failed to parse dependencies for '%s'", 
               a_entry->name);
        return false;
    }
    
    bool l_all_initialized = true;
    for (size_t i = 0; i < l_dep_count; i++) {
        dap_module_registry_entry_t *l_dep = s_module_find(l_dep_names[i]);
        if (!l_dep) {
            log_it(L_ERROR, "s_all_dependencies_initialized: Dependency '%s' not found for module '%s'", 
                   l_dep_names[i], a_entry->name);
            l_all_initialized = false;
            break;
        }
        if (!l_dep->initialized) {
            l_all_initialized = false;
            break;
        }
    }
    
    s_free_dependencies(l_dep_names, l_dep_count);
    return l_all_initialized;
}

/**
 * @brief Initialize all registered modules
 * 
 * Modules are initialized in dependency order using topological sorting.
 * Dependencies are initialized before dependents.
 * 
 * @return 0 on success, -1 on failure
 */
int dap_module_init_all(void)
{
    if (s_module_system_initialized) {
        log_it(L_DEBUG, "dap_module_init_all: Module system already initialized");
        return 0;
    }
    
    log_it(L_NOTICE, "dap_module_init_all: Initializing all registered modules (with dependency resolution)");
    
    int l_failed = 0;
    size_t l_total_modules = HASH_COUNT(s_module_registry);
    size_t l_initialized_count = 0;
    size_t l_iterations = 0;
    const size_t l_max_iterations = l_total_modules * l_total_modules; // Prevent infinite loops
    
    // Iteratively initialize modules whose dependencies are satisfied
    while (l_initialized_count < l_total_modules && l_iterations < l_max_iterations) {
        l_iterations++;
        bool l_progress_made = false;
        
        dap_module_registry_entry_t *l_entry, *l_tmp;
        HASH_ITER(hh, s_module_registry, l_entry, l_tmp) {
            if (l_entry->initialized) {
                continue;
            }
            
            // Check if all dependencies are initialized
            if (!s_all_dependencies_initialized(l_entry)) {
                continue;
            }
            
            // All dependencies satisfied, initialize this module
            log_it(L_INFO, "dap_module_init_all: Initializing module '%s' (version %u) [%zu/%zu]", 
                   l_entry->name, l_entry->version, l_initialized_count + 1, l_total_modules);
            
            if (l_entry->dependencies && l_entry->dependencies[0]) {
                log_it(L_DEBUG, "dap_module_init_all: Module '%s' dependencies satisfied", l_entry->name);
            }
            
            int l_ret = l_entry->init_cb(NULL);
            if (l_ret != 0) {
                // If init returns error code -2 (already registered from dap_net_transport_register),
                // treat it as success (idempotent operation)
                if (l_ret == -2) {
                    log_it(L_DEBUG, "dap_module_init_all: Module '%s' already registered (idempotent), marking as initialized", 
                           l_entry->name);
                    l_entry->initialized = true;
                    l_initialized_count++;
                    l_progress_made = true;
                } else {
                    log_it(L_ERROR, "dap_module_init_all: Failed to initialize module '%s': %d", 
                           l_entry->name, l_ret);
                    l_failed++;
                    // Still mark as initialized to prevent infinite loop
                    l_entry->initialized = true;
                    l_initialized_count++;
                    l_progress_made = true;
                }
            } else {
                l_entry->initialized = true;
                l_initialized_count++;
                l_progress_made = true;
                log_it(L_INFO, "dap_module_init_all: Module '%s' initialized successfully", 
                       l_entry->name);
            }
        }
        
        if (!l_progress_made) {
            // No progress made - might be circular dependency or missing dependency
            log_it(L_ERROR, "dap_module_init_all: No progress made in iteration %zu, checking for unresolved dependencies", 
                   l_iterations);
            
            HASH_ITER(hh, s_module_registry, l_entry, l_tmp) {
                if (!l_entry->initialized) {
                    log_it(L_ERROR, "dap_module_init_all: Module '%s' cannot be initialized (unresolved dependencies?)", 
                           l_entry->name);
                    if (l_entry->dependencies && l_entry->dependencies[0]) {
                        log_it(L_ERROR, "  Dependencies: %s", l_entry->dependencies);
                    }
                }
            }
            break;
        }
    }
    
    if (l_iterations >= l_max_iterations) {
        log_it(L_ERROR, "dap_module_init_all: Maximum iterations reached (%zu), possible circular dependency", 
               l_max_iterations);
    }
    
    s_module_system_initialized = true;
    
    if (l_failed > 0) {
        log_it(L_ERROR, "dap_module_init_all: Failed to initialize %d module(s)", l_failed);
        return -1;
    }
    
    log_it(L_INFO, "dap_module_init_all: All modules initialized successfully (%zu modules in %zu iterations)", 
           l_initialized_count, l_iterations);
    return 0;
}

/**
 * @brief Deinitialize all registered modules
 * 
 * Modules are deinitialized in reverse registration order.
 */
void dap_module_deinit_all(void)
{
    if (!s_module_system_initialized) {
        log_it(L_DEBUG, "dap_module_deinit_all: Module system not initialized");
        return;
    }
    
    log_it(L_NOTICE, "dap_module_deinit_all: Deinitializing all registered modules");
    
    dap_module_registry_entry_t *l_entry, *l_tmp;
    
    // Collect all entries that need deinitialization
    dap_list_t *l_deinit_list = NULL;
    HASH_ITER(hh, s_module_registry, l_entry, l_tmp) {
        if (l_entry->initialized && l_entry->deinit_cb) {
            l_deinit_list = dap_list_append(l_deinit_list, l_entry);
        }
    }
    
    // Deinitialize in reverse order (last added first)
    if (l_deinit_list) {
        // Count total modules to deinitialize and total list size
        size_t l_total_modules = 0;
        size_t l_list_size = 0;
        dap_list_t *l_count_iter = l_deinit_list;
        while (l_count_iter) {
            l_entry = (dap_module_registry_entry_t *)l_count_iter->data;
            if (l_entry && l_entry->deinit_cb && l_entry->initialized) {
                l_total_modules++;
            }
            l_list_size++;
            l_count_iter = l_count_iter->next;
        }
        log_it(L_INFO, "dap_module_deinit_all: Found %zu module(s) to deinitialize out of %zu entries in list", 
               l_total_modules, l_list_size);
        
        // Get last element
        dap_list_t *l_iter = l_deinit_list;
        while (l_iter && l_iter->next) {
            l_iter = l_iter->next;
        }
        
        // Iterate backwards
        size_t l_module_index = 0;
        size_t l_iteration_count = 0;
        size_t l_consecutive_skipped = 0; // Track consecutive skipped entries
        const size_t l_max_iterations = l_list_size + 1; // Allow one extra iteration for safety
        while (l_iter != NULL) {
            // Safety check: prevent infinite loop
            if (l_iteration_count >= l_max_iterations) {
                log_it(L_ERROR, "dap_module_deinit_all: Infinite loop detected! Breaking after %zu iterations", 
                       l_iteration_count);
                break;
            }
            l_iteration_count++;
            
            l_entry = (dap_module_registry_entry_t *)l_iter->data;
            if (l_entry && l_entry->deinit_cb && l_entry->initialized) {
                // Reset consecutive skipped counter when we find a module to deinitialize
                l_consecutive_skipped = 0;
                
                log_it(L_INFO, "dap_module_deinit_all: [%zu/%zu] Deinitializing module '%s'", 
                       l_module_index + 1, l_total_modules, l_entry->name);
                fflush(stdout); // Ensure log is flushed before calling deinit_cb
                
                // Call deinit callback - this may hang if module has issues
                l_entry->deinit_cb();
                
                l_entry->initialized = false;
                log_it(L_DEBUG, "dap_module_deinit_all: Module '%s' deinitialized successfully", l_entry->name);
                l_module_index++;
                
                // Early exit: if we've processed all modules, break
                if (l_module_index >= l_total_modules) {
                    log_it(L_DEBUG, "dap_module_deinit_all: All modules processed, exiting iteration loop");
                    break;
                }
            } else {
                l_consecutive_skipped++;
                // Log why this entry is skipped (only for first few or if all modules are processed)
                if (l_module_index >= l_total_modules && l_consecutive_skipped <= 3) {
                    if (!l_entry) {
                        log_it(L_DEBUG, "dap_module_deinit_all: Skipping NULL entry at iteration %zu", l_iteration_count);
                    } else if (!l_entry->deinit_cb) {
                        log_it(L_DEBUG, "dap_module_deinit_all: Skipping entry '%s' (no deinit_cb) at iteration %zu", 
                               l_entry->name, l_iteration_count);
                    } else if (!l_entry->initialized) {
                        log_it(L_DEBUG, "dap_module_deinit_all: Skipping entry '%s' (already deinitialized) at iteration %zu", 
                               l_entry->name, l_iteration_count);
                    }
                }
            }
            
            // Move to previous element
            dap_list_t *l_prev = l_iter->prev;
            if (l_prev == l_iter) {
                // Circular reference detected - break to prevent infinite loop
                log_it(L_ERROR, "dap_module_deinit_all: Circular reference detected in deinit list! Breaking.");
                break;
            }
            l_iter = l_prev;
        }
        
        log_it(L_DEBUG, "dap_module_deinit_all: Finished iteration loop: processed %zu modules in %zu iterations", 
               l_module_index, l_iteration_count);
        
        dap_list_free(l_deinit_list);
    }
    
    // Clear module registry to prevent destructors from trying to deinitialize again
    // Free all entries first
    if (s_module_registry != NULL) {
        dap_module_registry_entry_t *l_entry, *l_tmp;
        HASH_ITER(hh, s_module_registry, l_entry, l_tmp) {
            HASH_DEL(s_module_registry, l_entry);
            if (l_entry->dependencies) {
                DAP_DELETE(l_entry->dependencies);
            }
            DAP_DELETE(l_entry);
        }
    }
    s_module_registry = NULL;
    s_module_system_initialized = false;
    
    log_it(L_INFO, "dap_module_deinit_all: All modules deinitialized");
}

/**
 * @brief Check if a module is initialized
 * 
 * @param a_name Module name
 * @return true if initialized, false otherwise
 */
bool dap_module_is_initialized(const char *a_name)
{
    dap_module_registry_entry_t *l_entry = s_module_find(a_name);
    return l_entry ? l_entry->initialized : false;
}

/**
 * @brief Mark a module as initialized (for use by constructors)
 * 
 * This function allows constructors to mark modules as initialized after
 * successfully calling init functions directly, preventing duplicate initialization.
 * 
 * @param a_name Module name
 * @return 0 on success, -1 if module not found
 */
int dap_module_mark_initialized(const char *a_name)
{
    if (!a_name) {
        log_it(L_ERROR, "dap_module_mark_initialized: Invalid module name");
        return -1;
    }
    
    dap_module_registry_entry_t *l_entry = s_module_find(a_name);
    if (!l_entry) {
        log_it(L_WARNING, "dap_module_mark_initialized: Module '%s' not found in registry", a_name);
        return -1;
    }
    
    if (l_entry->initialized) {
        log_it(L_DEBUG, "dap_module_mark_initialized: Module '%s' already marked as initialized", a_name);
        return 0;
    }
    
    l_entry->initialized = true;
    log_it(L_DEBUG, "dap_module_mark_initialized: Module '%s' marked as initialized", a_name);
    return 0;
}

/**
 * @brief Get module version
 * 
 * @param a_name Module name
 * @return Module version or 0 if not found
 */
unsigned int dap_module_get_version(const char *a_name)
{
    dap_module_registry_entry_t *l_entry = s_module_find(a_name);
    return l_entry ? l_entry->version : 0;
}
