/*
 * DAP Network Common Types
 * Shared types and definitions for network modules
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"
#include "dap_cluster_node.h"

#ifdef __cplusplus
extern "C" {
#endif

int dap_net_common_init(void);
void dap_net_common_deinit(void);

int dap_net_common_parse_stream_addrs(void *a_cfg, const char *a_config, const char *a_section,
                                       dap_cluster_node_addr_t **a_addrs, uint16_t *a_addrs_count);

// Stream event callbacks (to break stream → link_manager dependency)
typedef void (*dap_stream_event_add_callback_t)(dap_cluster_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);
typedef void (*dap_stream_event_replace_callback_t)(dap_cluster_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);
typedef void (*dap_stream_event_delete_callback_t)(dap_cluster_node_addr_t *a_addr, void *a_user_data);

int dap_stream_event_callbacks_register(dap_stream_event_add_callback_t a_add_cb,
                                        dap_stream_event_replace_callback_t a_replace_cb,
                                        dap_stream_event_delete_callback_t a_delete_cb,
                                        void *a_user_data);
void dap_stream_event_callbacks_unregister(void);

void dap_stream_event_notify_add(dap_cluster_node_addr_t *a_addr, bool a_is_uplink);
void dap_stream_event_notify_replace(dap_cluster_node_addr_t *a_addr, bool a_is_uplink);
void dap_stream_event_notify_delete(dap_cluster_node_addr_t *a_addr);

#ifdef __cplusplus
}
#endif
