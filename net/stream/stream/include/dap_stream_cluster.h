/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2023
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
#include <dap_common.h>
#include <stdint.h>
#include <pthread.h>
#include "uthash.h"
#include "dap_list.h"
#include "dap_guuid.h"

typedef struct dap_cluster dap_cluster_t;

typedef struct dap_cluster_member {
    dap_stream_node_addr_t addr;    // Member addr, HT key
    int role;                       // Member role & access rights (user-defined enum)
    bool persistent;                // Persistent members won't be removed with its links
    void *info;                     // Member info pointer
    dap_cluster_t *cluster;         // Cluster pointer
    UT_hash_handle hh;
} dap_cluster_member_t;

typedef void (*dap_cluster_change_callback_t)(dap_cluster_t *a_cluster, dap_cluster_member_t *a_member);

// Role in cluster
typedef enum dap_cluster_role {
    DAP_CLUSTER_ROLE_INVALID = -1,
    DAP_CLUSTER_ROLE_EMBEDDED = 0,  // Default role, passive link managment
    DAP_CLUSTER_ROLE_AUTONOMIC,     // Role for mixed link management, passive by default, switching to active one for absent links
    DAP_CLUSTER_ROLE_ISOLATED,      // Role for active internal independent link managment
    DAP_CLUSTER_ROLE_VIRTUAL        // No links managment on this type of clusters
} dap_cluster_role_t;

typedef struct dap_cluster {
    const char *mnemonim;           // Field for alternative cluster finding, unique
    dap_guuid_t uuid;               // Unique cluster id
    dap_cluster_role_t role;        // Link management role
    pthread_rwlock_t members_lock;
    dap_cluster_member_t *members;  // Cluster members (by stream addr) and callbacks
    dap_cluster_change_callback_t members_add_callback;
    dap_cluster_change_callback_t members_delete_callback;
    void *_inheritor;
    UT_hash_handle hh, hh_str;      // Handles for uuid and mnemonim storages
} dap_cluster_t;

// Cluster common funcs
dap_cluster_t *dap_cluster_new(const char *a_mnemonim, dap_cluster_role_t a_role);
void dap_cluster_delete(dap_cluster_t *a_cluster);
dap_cluster_t *dap_cluster_find(dap_guuid_t a_uuid);
dap_cluster_t *dap_cluster_by_mnemonim(const char *a_mnemonim);

// Member funcs
dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_addr, int a_role, void *a_info);
dap_cluster_member_t *dap_cluster_member_find_unsafe(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr);
int dap_cluster_member_find_role(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr);
void dap_cluster_member_delete(dap_cluster_member_t *a_member);
void dap_cluster_broadcast(dap_cluster_t *a_cluster, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size,
                           dap_stream_node_addr_t *a_exclude_aray, size_t a_exclude_array_size);
dap_list_t *dap_cluster_get_shuffle_members(dap_cluster_t *a_cluster);
char *dap_cluster_get_links_info(dap_cluster_t *a_cluster);
void dap_cluster_link_delete_from_all(dap_stream_node_addr_t *a_addr);
