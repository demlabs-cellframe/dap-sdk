/*
 * DAP Network Common Types
 * Shared types and definitions for network modules
 * This module breaks circular dependencies between link_manager, global_db, and stream
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Network Node Address (moved from dap_common.h)
// ============================================================================
// This is network-specific type, not core type
// Moved here to break dependency of core module on network concepts

/**
 * @brief Stream node address (network identifier)
 * Originally in dap_common.h but moved to net/common as network-specific type
 */
typedef union dap_stream_node_addr {
    uint64_t uint64;
    uint16_t words[sizeof(uint64_t)/2];
    uint8_t raw[sizeof(uint64_t)];  // Access to selected octets
} DAP_ALIGN_PACKED dap_stream_node_addr_t;

// ============================================================================
// Node Address Macros and Functions
// ============================================================================

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define NODE_ADDR_FP_STR      "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)  a->words[2],a->words[3],a->words[0],a->words[1]
#define NODE_ADDR_FPS_ARGS(a)  &a->words[2],&a->words[3],&a->words[0],&a->words[1]
#define NODE_ADDR_FP_ARGS_S(a)  a.words[2],a.words[3],a.words[0],a.words[1]
#define NODE_ADDR_FPS_ARGS_S(a)  &a.words[2],&a.words[3],&a.words[0],&a.words[1]
#else
#define NODE_ADDR_FP_STR      "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)  (a)->words[3],(a)->words[2],(a)->words[1],(a)->words[0]
#define NODE_ADDR_FPS_ARGS(a)  &(a)->words[3],&(a)->words[2],&(a)->words[1],&(a)->words[0]
#define NODE_ADDR_FP_ARGS_S(a)  (a).words[3],(a).words[2],(a).words[1],(a).words[0]
#define NODE_ADDR_FPS_ARGS_S(a)  &(a).words[3],&(a).words[2],&(a).words[1],&(a).words[0]
#endif

// Node address utility functions
int dap_stream_node_addr_from_str(dap_stream_node_addr_t *a_addr, const char *a_addr_str);

static inline bool dap_stream_node_addr_is_blank(dap_stream_node_addr_t *a_addr) { 
    return !a_addr->uint64; 
}

#define DAP_NODE_ADDR_LEN 23
typedef union dap_node_addr_str {
    const char s[DAP_NODE_ADDR_LEN];
} dap_node_addr_str_t;

dap_node_addr_str_t dap_stream_node_addr_to_str_static_(dap_stream_node_addr_t a_address);
#define dap_stream_node_addr_to_str_static(a) dap_stream_node_addr_to_str_static_(a).s

/**
 * @brief Initialize network common module (registers config parsers)
 * Should be called after dap_config_init()
 * @return 0 on success, negative on error
 */
int dap_net_common_init(void);

/**
 * @brief Deinitialize network common module
 */
void dap_net_common_deinit(void);

/**
 * @brief Parse stream node addresses from config file (convenience wrapper)
 * Uses registered parser "stream_addrs"
 * @param a_cfg Config object
 * @param a_config Config name
 * @param a_section Section name
 * @param a_addrs Output array of addresses (will be allocated)
 * @param a_addrs_count Output count of addresses
 * @return 0 on success, negative on error
 */
int dap_net_common_parse_stream_addrs(void *a_cfg, const char *a_config, const char *a_section, 
                                       dap_stream_node_addr_t **a_addrs, uint16_t *a_addrs_count);

// Forward declarations for breaking circular dependencies
typedef struct dap_cluster dap_cluster_t;
typedef struct dap_cluster_member dap_cluster_member_t;
typedef struct dap_link_manager dap_link_manager_t;
typedef struct dap_link dap_link_t;

// ============================================================================
// Stream Event Callbacks (to break stream → link_manager dependency)
// ============================================================================
// Stream notifies interested parties about stream lifecycle events via callbacks
// This allows link_manager (and other modules) to register callbacks without stream depending on them

typedef void (*dap_stream_event_add_callback_t)(dap_stream_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);
typedef void (*dap_stream_event_replace_callback_t)(dap_stream_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);
typedef void (*dap_stream_event_delete_callback_t)(dap_stream_node_addr_t *a_addr, void *a_user_data);

// Stream event callbacks registration
int dap_stream_event_callbacks_register(dap_stream_event_add_callback_t a_add_cb,
                                        dap_stream_event_replace_callback_t a_replace_cb,
                                        dap_stream_event_delete_callback_t a_delete_cb,
                                        void *a_user_data);
void dap_stream_event_callbacks_unregister(void);

// Stream event notification (called by stream module)
void dap_stream_event_notify_add(dap_stream_node_addr_t *a_addr, bool a_is_uplink);
void dap_stream_event_notify_replace(dap_stream_node_addr_t *a_addr, bool a_is_uplink);
void dap_stream_event_notify_delete(dap_stream_node_addr_t *a_addr);

// ============================================================================
// Cluster Management Types (must be before callbacks that use them)
// ============================================================================

// Cluster mnemonics for broadcasting
#define DAP_STREAM_CLUSTER_GLOBAL   "global"    // Globally broadcasting groups
#define DAP_STREAM_CLUSTER_LOCAL    "local"     // Non-broadcasting groups

typedef struct dap_link_manager dap_link_manager_t;
typedef struct dap_link dap_link_t;

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
// Cluster Event Callbacks (to break global_db → link_manager dependency)
// ============================================================================

// Callback for cluster member add (called when member added to cluster)
typedef void (*dap_cluster_member_add_callback_t)(dap_cluster_member_t *a_member, void *a_arg);
// Callback for cluster member delete (called when member removed from cluster)
typedef void (*dap_cluster_member_delete_callback_t)(dap_cluster_member_t *a_member, void *a_arg);

// Cluster callbacks registry (for specific cluster types)
typedef struct dap_cluster_callbacks {
    dap_cluster_member_add_callback_t add_callback;
    dap_cluster_member_delete_callback_t delete_callback;
    void *arg;
} dap_cluster_callbacks_t;

// Register cluster callbacks for specific cluster type
int dap_cluster_callbacks_register(dap_cluster_type_t a_cluster_type, 
                                    dap_cluster_member_add_callback_t a_add_cb,
                                    dap_cluster_member_delete_callback_t a_del_cb,
                                    void *a_arg);
// Get registered callbacks for cluster type
dap_cluster_callbacks_t* dap_cluster_callbacks_get(dap_cluster_type_t a_cluster_type);

#ifdef __cplusplus
}
#endif
