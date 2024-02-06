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

typedef struct dap_link_manager dap_link_manager_t;
typedef struct dap_link dap_link_t;

typedef void (*dap_link_manager_callback_t)(dap_link_t *, void*);
typedef void (*dap_link_manager_callback_delete_t)(dap_link_manager_t *);
typedef bool (*dap_link_manager_callback_update_t)(void *);
typedef void (*dap_link_manager_callback_error_t)(dap_link_manager_t *, int, void *);
typedef dap_list_t *(*dap_link_manager_callback_node_list_t)(const char *);

typedef struct dap_link_manager_callbacks {
    dap_link_manager_callback_t connected;
    dap_link_manager_callback_t disconnected;
    dap_link_manager_callback_delete_t delete;
    dap_link_manager_callback_update_t update;
    dap_link_manager_callback_error_t error;
    dap_link_manager_callback_node_list_t get_node_list;
} dap_link_manager_callbacks_t;

// connection states
typedef enum dap_link_state {
    LINK_STATE_ERROR = -1,
    LINK_STATE_DISCONNECTED = 0,
    LINK_STATE_GET_NODE_ADDR = 1,
    LINK_STATE_NODE_ADDR_LEASED = 2,
    LINK_STATE_PING = 3,
    LINK_STATE_PONG = 4,
    LINK_STATE_CONNECTING = 5,
    LINK_STATE_ESTABLISHED = 100,
} dap_link_state_t;

typedef struct dap_link {
    dap_link_state_t state;
    uint64_t uplink_ip;
    struct in_addr addr_v4;
    struct in6_addr addr_v6;
    uint16_t port;
    dap_client_t *client;
    char *net;
    dap_stream_node_addr_t addr;
    bool keep_connection;
    dap_link_manager_t *link_manager;
    UT_hash_handle hh;
} dap_link_t;

typedef struct dap_link_manager {
    dap_stream_node_addr_t self_addr;
    uint32_t min_links_num;
    bool active;
    dap_list_t *active_nets;
    dap_link_t *self_links;
    dap_link_t *alien_links;
    dap_timerfd_t *update_timer;
    dap_link_manager_callbacks_t callbacks;
} dap_link_manager_t;

#define DAP_LINK(a) (a ? (dap_link_t *) (a)->_inheritor : NULL)

int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks);
void dap_link_manager_deinit();
dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks);
dap_link_manager_t *dap_link_manager_get_default();
void dap_link_manager_add_active_net(char *a_net_name);
void dap_link_manager_remove_active_net(char *a_net_name);
int dap_link_compare(dap_list_t *a_list1, dap_list_t *a_list2);