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

#include "dap_time.h"
#include "dap_stream_cluster.h"
#include "dap_global_db.h"

#define DAP_GLOBAL_DB_CLUSTER_ANY                   "global"    // This mnemonim is for globally broadcasting grops
#define DAP_GLOBAL_DB_CLUSTER_BROADCAST_LIFETIME    15          // Seconds

typedef enum dap_global_db_role {
    DAP_GDB_MEMBER_ROLE_NOBODY = 0, // No access
    DAP_GDB_MEMBER_ROLE_GUEST,      // Read-only access
    DAP_GDB_MEMBER_ROLE_USER,       // Read-write access, no delete or rewrite
    DAP_GDB_MEMBER_ROLE_ROOT        // Full access
} dap_global_db_role_t;

DAP_STATIC_INLINE const char *dap_global_db_cluster_role_str(dap_global_db_role_t a_role)
{
    switch (a_role) {
    case DAP_GDB_MEMBER_ROLE_NOBODY: return "NOBODY";
    case DAP_GDB_MEMBER_ROLE_GUEST: return "GUEST";
    case DAP_GDB_MEMBER_ROLE_USER: return "USER";
    case DAP_GDB_MEMBER_ROLE_ROOT: return "ROOT";
    default: return "UNKNOWN";
    }
}

typedef void (*dap_store_obj_callback_notify_t)(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_obj, void *a_arg);

typedef struct dap_global_db_cluster {
    const char *mnemonim;
    const char *groups_mask;
    dap_cluster_t *member_cluster;
    dap_global_db_role_t default_role;
    uint64_t ttl;                   // Time-to-life for objects in this cluster, in seconds
    bool owner_root_access;         // deny if false, grant overwise
    dap_store_obj_callback_notify_t callback_notify;
    void *callback_arg;
    dap_global_db_instance_t *dbi;
    struct dap_global_db_cluster *prev;
    struct dap_global_db_cluster *next;
} dap_global_db_cluster_t;

int dap_global_db_cluster_init();
void dap_global_db_cluster_deinit();
dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name);
void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj);
dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint64_t a_ttl, bool a_owner_access,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg,
                                                   dap_global_db_role_t a_default_role);
void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster);
int dap_global_db_cluster_member_add(const char *a_mnemonim, dap_stream_node_addr_t a_node_addr, dap_global_db_role_t a_role);
void dap_global_db_cluster_notify(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj);
