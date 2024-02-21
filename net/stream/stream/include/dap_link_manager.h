/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Pavel Uhanov <pavel.uhanov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2024
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
#pragma once
#include <stdint.h>
#include "dap_list.h"
#include "dap_timerfd.h"
#include "dap_common.h"
#include "dap_client.h"
#include "dap_stream_cluster.h"

typedef struct dap_link_manager dap_link_manager_t;
typedef struct dap_link dap_link_t;

typedef void (*dap_link_manager_callback_t)(dap_link_t *, void*);
typedef void (*dap_link_manager_callback_connected_t)(dap_link_t *, uint64_t);
typedef void (*dap_link_manager_callback_error_t)(dap_link_t *, uint64_t, int);
typedef int (*dap_link_manager_callback_fill_net_info_t)(dap_link_t *);
typedef void (*dap_link_manager_callback_link_request_t)(uint64_t);

typedef struct dap_link_manager_callbacks {
    dap_link_manager_callback_connected_t connected;
    dap_link_manager_callback_error_t disconnected;
    dap_link_manager_callback_error_t error;
    dap_link_manager_callback_fill_net_info_t fill_net_info;
    dap_link_manager_callback_link_request_t link_request;
} dap_link_manager_callbacks_t;

// connection states
typedef enum dap_link_state {
    LINK_STATE_DISCONNECTED = 0,
    LINK_STATE_CONNECTING,
    LINK_STATE_ESTABLISHED,
} dap_link_state_t;

typedef struct dap_link {
    dap_link_state_t state;
    bool keep_connection;
    bool enabled;
    dap_stream_node_addr_t node_addr;
    dap_client_t *client;
    dap_list_t *role_clusters;
    dap_list_t *links_clusters;
    dap_link_manager_t *link_manager;
    UT_hash_handle hh;
} dap_link_t;

typedef struct dap_link_manager {
    dap_stream_node_addr_t self_addr;
    int32_t min_links_num;
    bool active;
    dap_list_t *nets;
    dap_link_t *links;
    dap_timerfd_t *update_timer;
    dap_link_manager_callbacks_t callbacks;
} dap_link_manager_t;

#define DAP_LINK(a) (a ? (dap_link_t *) (a)->_inheritor : NULL)

int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks);
void dap_link_manager_deinit();
dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks);
dap_link_manager_t *dap_link_manager_get_default();
int dap_link_manager_add_net(uint64_t a_net_id, dap_cluster_t *a_link_cluster);
void dap_link_manager_remove_active_net(uint64_t a_net_id);
void dap_link_manager_add_role_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster);
void dap_link_manager_add_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster);
void dap_link_manager_remove_role_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster);
void dap_link_manager_remove_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster);
dap_link_t *dap_link_manager_link_create_or_update(dap_stream_node_addr_t *a_node_addr, struct in_addr *a_addr_v4, struct in6_addr *a_addr_v6, uint16_t a_port);
int dap_link_manager_link_add(uint64_t a_net_id, dap_link_t *a_link);
void dap_link_manager_set_net_status(uint64_t a_net_id, bool a_status);
size_t dap_link_manager_links_count(uint64_t a_net_id);
size_t dap_link_manager_needed_links_count(uint64_t a_net_id);