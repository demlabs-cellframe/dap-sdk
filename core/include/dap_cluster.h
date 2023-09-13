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
#include "dap_guuid.h"

typedef struct dap_cluster dap_cluster_t;

// Role in cluster, could be combination of different roles at same time
typedef uint16_t dap_cluster_role_t;

// Client can only join to the cluster
#define DAP_CLUSTER_ROLE_CLIENT     0x0001
// Host owns cluster and can operate it as he wants
#define DAP_CLUSTER_ROLE_HOST       0x0002
// Operator has limited set of permissions for cluster operations
#define DAP_CLUSTER_ROLE_OPERATOR   0x0004
// Server accepts connections from clients, exchanges with content info
// and provide service for others
#define DAP_CLUSTER_ROLE_SERVER     0x0100
// Balancer split connections and content between servers
#define DAP_CLUSTER_ROLE_BALANCER   0x0200

#define DAP_CLUSTER_ROLE_ALL        0xFFFF

typedef struct dap_cluster_member {
    dap_stream_node_addr_t addr;    // Member addr, HT key
    dap_cluster_role_t role;        // Member role & access rights
    dap_cluster_t *cluster;         // Cluster pointer
    void *info;                     // Member info pointer
    UT_hash_handle hh;
} dap_cluster_member_t;

typedef enum dap_cluster_member_op {
    DAP_CLUSTER_MEMBER_ADD,
    DAP_CLUSTER_MEMBER_DELETE
} dap_cluster_member_op_t;

typedef void (*dap_cluster_change_callback_t)(dap_cluster_t *a_cluster, dap_cluster_member_t *a_member, dap_cluster_member_op_t a_operation);

typedef void dap_cluster_options_t;

typedef struct dap_cluster {
    dap_guuid_t guuid;
    pthread_rwlock_t members_lock;
    dap_cluster_member_t *members;
    dap_cluster_options_t *options;
    dap_cluster_change_callback_t members_callback;
    void *_inheritor;
    UT_hash_handle hh;
} dap_cluster_t;

// Cluster common funcs
dap_cluster_t *dap_cluster_new(dap_cluster_options_t *a_options);
void dap_cluster_delete(dap_cluster_t *a_cluster);
dap_cluster_t *dap_cluster_find(dap_guuid_t a_guuid);

// Member funcs
dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_addr, dap_cluster_role_t a_role, void *a_info);
dap_cluster_member_t *dap_cluster_member_find(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr);
void dap_cluster_member_delete(dap_cluster_member_t *a_member);
