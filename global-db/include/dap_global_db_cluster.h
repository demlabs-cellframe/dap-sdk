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
#include "dap_cluster.h"
#include "dap_global_db.h"

#define DAP_GLOBAL_DB_PKT_PACK_MAX_COUNT    1024
#define DAP_GLOBAL_DB_CLUSTER_ANY           "global" // This mnemonim is for globally broadcasting grops

typedef struct dap_global_db_cluster dap_global_db_cluster_t;

typedef void (*dap_store_obj_callback_notify_t)(dap_global_db_context_t *a_context, dap_store_obj_t *a_obj, void *a_arg);

typedef struct dap_global_db_cluster {
    const char *mnemonim;
    const char *groups_mask;
    dap_cluster_t *member_cluster;
    uint64_t ttl;
    dap_store_obj_callback_notify_t callback_notify;
    void *callback_arg;
    dap_global_db_cluster_t *prev;
    dap_global_db_cluster_t *next;
} dap_global_db_cluster_t;

typedef struct dap_global_db_pkt {
    uint32_t crc;   // Must be first for compute
    dap_nanotime_t timestamp;
    uint16_t group_len;
    uint16_t key_len;
    uint32_t value_len;
    uint32_t data_len;
    byte_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_t;

DAP_STATIC_INLINE size_t dap_global_db_pkt_get_size(dap_global_db_pkt_t *a_pkt)
{
    return sizeof(*a_pkt) + a_pkt->data_len;
}

typedef struct dap_global_db_pkt_pack {
    uint64_t data_size;
    uint32_t obj_count;
    byte_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_pack_t;

dap_global_db_pkt_pack_t *dap_global_db_pkt_pack(dap_global_db_pkt_pack_t *a_old_pkt, dap_global_db_pkt_t *a_new_pkt);
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj);
dap_store_obj_t *dap_global_db_pkt_deserialize(dap_global_db_pkt_pack_t *a_pkt, size_t *a_store_obj_count);
int dap_global_db_cluster_init();
void dap_global_db_cluster_deinit();
dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name);
void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj);
dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint64_t a_ttl,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg);
