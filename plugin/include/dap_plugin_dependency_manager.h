/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy.a@gmail.com>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2024
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

/**
 * @file dap_plugin_dependency_manager.h
 * @brief DAP SDK Plugin Dependency Manager
 * 
 * Provides dynamic plugin loading with dependency resolution and file type handler registration.
 * Follows DAP SDK architectural principles:
 * - Modular architecture with zero hardcoded dependencies
 * - Security-first design with validation at each step
 * - Cross-platform compatibility
 * - Memory-safe operations using DAP SDK macros
 * - Performance-optimized hash-based lookups
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "dap_common.h"
#include "dap_plugin_manifest.h"
#include "dap_plugin.h"
#include "uthash.h"

/**
 * @brief Plugin file type handler registration
 * 
 * Dynamic registration system for associating file extensions with plugin handlers.
 * Eliminates hardcoded language enums in favor of runtime registration.
 */
typedef struct dap_plugin_type_handler {
    char *file_extension;                     ///< File extension (e.g., ".py", ".js", ".lua")
    char *handler_plugin_name;                ///< Plugin name that handles this type
    dap_plugin_manifest_t *handler_manifest;  ///< Handler plugin manifest
    dap_plugin_callbacks_t callbacks;         ///< Plugin operation callbacks
    uint64_t registration_time;               ///< Registration timestamp for audit
    UT_hash_handle hh;                       ///< Hash table handle for uthash
} dap_plugin_type_handler_t;

/**
 * @brief Plugin dependency graph node
 * 
 * Represents a plugin with its dependencies for topological sorting.
 */
typedef struct dap_plugin_dependency_node {
    char *plugin_name;                              ///< Plugin name
    dap_plugin_manifest_t *manifest;                ///< Plugin manifest
    struct dap_plugin_dependency_node **dependencies; ///< Array of dependency nodes
    size_t dependencies_count;                      ///< Number of dependencies
    size_t dependencies_capacity;                   ///< Allocated capacity for dependencies
    bool visited;                                   ///< DFS traversal flag
    bool in_stack;                                  ///< Cycle detection flag
    UT_hash_handle hh;                             ///< Hash table handle
} dap_plugin_dependency_node_t;

/**
 * @brief Plugin detection result structure
 * 
 * Result of scanning directories for plugin files.
 */
typedef struct dap_plugin_detection_result {
    char *file_path;                ///< Full path to plugin file
    char *extension;                ///< Detected file extension
    char *handler_name;             ///< Required handler plugin name
    float confidence;               ///< Detection confidence (0.0 to 1.0)
} dap_plugin_detection_result_t;

/**
 * @brief Plugin load statistics
 * 
 * Statistics for plugin loading operations.
 */
typedef struct dap_plugin_load_stats {
    size_t total_loaded;                      ///< Total loaded plugins
    size_t dependencies_resolved;             ///< Dependencies resolved
    size_t circular_dependencies_found;       ///< Circular dependencies found
    size_t auto_loads_triggered;              ///< Auto-loads triggered
    size_t handlers_registered;               ///< Handlers registered
} dap_plugin_load_stats_t;

/**
 * @brief Plugin security validation context
 * 
 * Security validation parameters following DAP SDK security-first principles.
 */
typedef struct dap_plugin_security_context {
    bool enforce_manifest_validation;    ///< Require valid manifests
    bool enforce_dependency_validation;  ///< Validate all dependencies
    bool enforce_path_validation;        ///< Validate file paths
    bool enforce_signature_validation;   ///< Validate plugin signatures
    size_t max_plugin_size;             ///< Maximum allowed plugin size
    size_t max_dependency_depth;        ///< Maximum dependency chain depth
    const char **allowed_extensions;     ///< Allowed file extensions
    size_t allowed_extensions_count;     ///< Number of allowed extensions
} dap_plugin_security_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize plugin dependency manager
 * 
 * Initializes the plugin dependency manager with DAP SDK security context.
 * Must be called before any other plugin operations.
 * 
 * @param a_security_context Security validation parameters
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_dependency_manager_init(const dap_plugin_security_context_t *a_security_context);

/**
 * @brief Cleanup plugin dependency manager
 * 
 * Cleans up all allocated resources following DAP SDK memory management practices.
 * Should be called during application shutdown.
 */
void dap_plugin_dependency_manager_deinit(void);

/**
 * @brief Register file type handler
 * 
 * Dynamically registers a plugin as handler for specific file extensions.
 * Replaces hardcoded language enums with runtime registration.
 * 
 * @param a_extension File extension (e.g., ".py", ".js")
 * @param a_handler_name Plugin name that handles this type
 * @param a_callbacks Plugin operation callbacks
 * @param a_user_data User data passed to callbacks
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_register_type_handler(const char *a_extension,
                                    const char *a_handler_name,
                                    dap_plugin_type_callbacks_t *a_callbacks,
                                    void *a_user_data);

/**
 * @brief Unregister file type handler
 * 
 * Removes a previously registered file type handler.
 * 
 * @param a_file_extension File extension to unregister
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_unregister_type_handler(const char *a_file_extension);

/**
 * @brief Find handler for file type
 * 
 * Locates registered handler for a specific file extension.
 * 
 * @param a_extension File extension to lookup
 * @return Handler name string or NULL if not found
 */
const char *dap_plugin_find_type_handler(const char *a_extension);

/**
 * @brief Get all registered handlers
 * 
 * Returns array of all registered file type handlers.
 * 
 * @param a_handlers_count Output parameter for number of handlers
 * @return Array of handler pointers (caller must not free)
 */
dap_plugin_type_handler_t **dap_plugin_get_all_handlers(size_t *a_handlers_count);

/**
 * @brief Scan directory for plugin files
 * 
 * Scans specified directory for plugin files and identifies required handlers.
 * Follows DAP SDK security practices with path validation.
 * 
 * @param a_directory Directory to scan
 * @param a_recursive Whether to scan subdirectories
 * @param a_results Output array of detection results
 * @param a_results_count Output parameter for number of results
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_scan_directory(const char *a_directory,
                             bool a_recursive,
                             dap_plugin_detection_result_t **a_results,
                             size_t *a_results_count);

/**
 * @brief Free detection results
 * 
 * Frees memory allocated for detection results.
 * 
 * @param a_results Array of detection results
 * @param a_results_count Number of results
 */
void dap_plugin_detection_results_free(dap_plugin_detection_result_t *a_results, size_t a_results_count);

/**
 * @brief Auto-load required handlers
 * 
 * Automatically loads plugin handlers for detected plugin files.
 * 
 * @param a_results Array of detection results
 * @param a_results_count Number of detection results
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_auto_load_handlers(dap_plugin_detection_result_t *a_results,
                                 size_t a_results_count);

/**
 * @brief Load plugin with dependencies
 * 
 * Loads a plugin and all its dependencies in correct order.
 * 
 * @param a_plugin_name Plugin name to load
 * @param a_plugin_path Path to plugin file (can be NULL)
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_load_with_dependencies(const char *a_plugin_name,
                                     const char *a_plugin_path);

/**
 * @brief Build dependency graph
 * 
 * Constructs dependency graph for all available plugins.
 * Uses hash tables for O(1) lookup performance.
 * 
 * @param a_plugin_directory Directory containing plugins
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_build_dependency_graph(const char *a_plugin_directory);

/**
 * @brief Validate dependency graph
 * 
 * Validates dependency graph for cycles and missing dependencies.
 * Implements DFS-based cycle detection.
 * 
 * @param a_has_cycles Output parameter indicating if cycles exist
 * @param a_missing_deps Output array of missing dependencies
 * @param a_missing_count Output parameter for number of missing dependencies
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_validate_dependency_graph(bool *a_has_cycles,
                                        char ***a_missing_deps,
                                        size_t *a_missing_count);

/**
 * @brief Get topological order
 * 
 * Returns plugins in topological order for dependency-safe loading.
 * 
 * @param a_ordered_plugins Output array of plugin names in load order
 * @param a_plugins_count Output parameter for number of plugins
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_get_topological_order(char ***a_ordered_plugins, size_t *a_plugins_count);

/**
 * @brief Detect dependency cycles
 * 
 * Detects circular dependencies using DFS traversal.
 * 
 * @param a_cycle_info Output parameter with cycle information
 * @return true if cycles detected, false otherwise
 */
bool dap_plugin_detect_dependency_cycles(char **a_cycle_info);

/**
 * @brief Preload critical handlers
 * 
 * Preloads essential plugin handlers for performance optimization.
 * Follows DAP SDK 3-phase performance optimization strategy.
 * 
 * @param a_critical_extensions Array of critical file extensions
 * @param a_extensions_count Number of critical extensions
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_preload_critical_handlers(const char **a_critical_extensions,
                                        size_t a_extensions_count);

/**
 * @brief Load plugins in parallel
 * 
 * Loads independent plugins in parallel for performance.
 * Respects dependency order and security constraints.
 * 
 * @param a_plugin_names Array of plugin names to load
 * @param a_plugins_count Number of plugins to load
 * @param a_max_parallel_loads Maximum parallel operations
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_load_parallel(const char **a_plugin_names,
                            size_t a_plugins_count,
                            size_t a_max_parallel_loads);

/**
 * @brief Cache dependency resolution
 * 
 * Caches dependency resolution results for performance.
 * 
 * @param a_cache_file Path to cache file
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_cache_dependencies(const char *a_cache_file);

/**
 * @brief Validate plugin security
 * 
 * Validates plugin against security policies.
 * Implements DAP SDK security-first principles.
 * 
 * @param a_plugin_path Path to plugin file
 * @param a_manifest Plugin manifest
 * @param a_validation_result Output parameter with validation results
 * @return true if plugin passes security validation
 */
bool dap_plugin_validate_security(const char *a_plugin_path,
                                 const dap_plugin_manifest_t *a_manifest,
                                 char **a_validation_result);

/**
 * @brief Validate plugin manifest
 * 
 * Validates plugin manifest structure and content.
 * 
 * @param a_manifest Plugin manifest to validate
 * @param a_validation_errors Output parameter with validation errors
 * @return true if manifest is valid
 */
bool dap_plugin_validate_manifest(const dap_plugin_manifest_t *a_manifest,
                                 char **a_validation_errors);

/**
 * @brief Sanitize plugin path
 * 
 * Validates and sanitizes plugin file paths for security.
 * 
 * @param a_plugin_path Path to validate
 * @param a_sanitized_path Output parameter with sanitized path
 * @return true if path is valid and sanitized
 */
bool dap_plugin_sanitize_path(const char *a_plugin_path, char **a_sanitized_path);

/**
 * @brief Get loading statistics
 * 
 * Returns plugin loading performance and security statistics.
 * 
 * @return Pointer to statistics structure
 */
const dap_plugin_load_stats_t *dap_plugin_get_load_stats(void);

/**
 * @brief Reset loading statistics
 * 
 * Resets plugin loading statistics counters.
 */
void dap_plugin_reset_loading_stats(void);

/**
 * @brief Get handler registry status
 * 
 * Returns information about registered handlers.
 * 
 * @param a_registry_info Output parameter with registry information
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_get_registry_status(char **a_registry_info);

/**
 * @brief Free dependency graph
 * 
 * Frees memory allocated for dependency graph.
 * 
 * @param a_graph Dependency graph to free
 */
void dap_plugin_free_dependency_graph(dap_plugin_dependency_node_t *a_graph);

/**
 * @brief Free string array
 * 
 * Utility function to free array of strings.
 * 
 * @param a_strings Array of strings to free
 * @param a_count Number of strings
 */
void dap_plugin_free_string_array(char **a_strings, size_t a_count);

/**
 * @brief Register plugin loaded callback
 * 
 * Registers callback to be called when plugin is loaded.
 * 
 * @param a_callback Callback function
 * @param a_user_data User data to pass to callback
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_register_loaded_callback(void (*a_callback)(const char *a_plugin_name, void *a_user_data),
                                       void *a_user_data);

/**
 * @brief Register handler registered callback
 * 
 * Registers callback to be called when file type handler is registered.
 * 
 * @param a_callback Callback function
 * @param a_user_data User data to pass to callback
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_register_handler_callback(void (*a_callback)(const char *a_extension,
                                                          const char *a_handler,
                                                          void *a_user_data),
                                        void *a_user_data);

/**
 * @brief Create dependency-sorted plugin list
 * 
 * Creates a topologically sorted list of plugins based on dependencies.
 * 
 * @param a_manifests Array of plugin manifests
 * @param a_manifests_count Number of manifests
 * @param a_sorted_names Output array of sorted plugin names
 * @param a_sorted_count Output parameter for number of sorted plugins
 * @return 0 on success, negative error code on failure
 */
int dap_plugin_create_dependency_order(dap_plugin_manifest_t *a_manifests,
                                      size_t a_manifests_count,
                                      char ***a_sorted_names,
                                      size_t *a_sorted_count);

/**
 * @brief Print debug information
 * 
 * Prints debug information about the dependency manager state.
 */
void dap_plugin_dependency_manager_debug_print(void);

#ifdef __cplusplus
}
#endif 