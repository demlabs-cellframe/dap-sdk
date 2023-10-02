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

typedef void (*dap_store_obj_callback_notify_t)(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_obj, void *a_arg);

typedef struct dap_global_db_cluster {
    const char *mnemonim;
    const char *groups_mask;
    dap_cluster_t *member_cluster;
    uint64_t ttl;
    dap_store_obj_callback_notify_t callback_notify;
    void *callback_arg;
    struct dap_global_db_cluster *prev;
    struct dap_global_db_cluster *next;
} dap_global_db_cluster_t;

int dap_global_db_cluster_init();
void dap_global_db_cluster_deinit();
dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name);
void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj);
dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint64_t a_ttl,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg);
