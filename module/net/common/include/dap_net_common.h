/*
 * DAP Network Common Types
 * Shared types and definitions for network modules
 * This module breaks circular dependencies between link_manager, global_db, and stream
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_guuid.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct dap_cluster dap_cluster_t;
typedef struct dap_cluster_member dap_cluster_member_t;
typedef struct dap_stream_node_addr dap_stream_node_addr_t;

// ============================================================================
// Cluster Callback Types (for inversion of control)
// ============================================================================

/**
 * @brief Callback for cluster member changes
 * @param a_member Changed member
 * @param a_arg Callback argument
 */
typedef void (*dap_cluster_change_callback_t)(dap_cluster_member_t *a_member, void *a_arg);

// ============================================================================
// Cluster Management Types
// ============================================================================

typedef enum dap_cluster_type {
    DAP_CLUSTER_TYPE_INVALID = 0,
    DAP_CLUSTER_TYPE_EMBEDDED,      // Network link management with balancer integration
    DAP_CLUSTER_TYPE_AUTONOMIC,     // Static link management, passive by default
    DAP_CLUSTER_TYPE_ISOLATED,      // Active internal independent link management
    DAP_CLUSTER_TYPE_SYSTEM,        // Special link management for local and global clusters
    DAP_CLUSTER_TYPE_VIRTUAL        // No links management for this type
} dap_cluster_type_t;

typedef enum dap_cluster_status {
    DAP_CLUSTER_STATUS_DISABLED = 0,
    DAP_CLUSTER_STATUS_ENABLED
} dap_cluster_status_t;

// ============================================================================
// Link Manager Callback Types (for inversion of control)
// ============================================================================

// Forward declaration for link manager
typedef struct dap_link dap_link_t;

/**
 * @brief Callback when link is connected
 * @param a_link Connected link
 * @param a_net_id Network ID
 */
typedef void (*dap_link_manager_callback_connected_t)(dap_link_t *a_link, uint64_t a_net_id);

/**
 * @brief Callback when link is disconnected
 * @param a_link Disconnected link
 * @param a_net_id Network ID
 */
typedef void (*dap_link_manager_callback_disconnected_t)(dap_link_t *a_link, uint64_t a_net_id);

/**
 * @brief Callback for link error
 * @param a_link Link with error
 * @param a_error_code Error code
 */
typedef void (*dap_link_manager_callback_error_t)(dap_link_t *a_link, int a_error_code);

/**
 * @brief Callback to fill network info
 */
typedef void (*dap_link_manager_callback_fill_net_info_t)(void);

/**
 * @brief Callback for link request
 */
typedef void (*dap_link_manager_callback_link_request_t)(void);

/**
 * @brief Callback when link count changes
 */
typedef void (*dap_link_manager_callback_link_count_changed_t)(void);

/**
 * @brief Link manager callbacks structure
 */
typedef struct dap_link_manager_callbacks {
    dap_link_manager_callback_connected_t connected;
    dap_link_manager_callback_disconnected_t disconnected;
    dap_link_manager_callback_error_t error;
    dap_link_manager_callback_fill_net_info_t fill_net_info;
    dap_link_manager_callback_link_request_t link_request;
    dap_link_manager_callback_link_count_changed_t link_count_changed;
} dap_link_manager_callbacks_t;

#ifdef __cplusplus
}
#endif
