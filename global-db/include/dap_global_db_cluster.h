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

typedef struct dap_global_db_cluster dap_global_db_cluster_t;

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
    dap_nanotime_t timestamp;
    uint16_t group_len;
    uint16_t key_len;
    uint32_t value_len;
    uint32_t sign_len;
    uint32_t crc;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_t;

DAP_STATIC_INLINE size_t dap_global_db_pkt_get_size(dap_global_db_pkt_t *a_pkt)
{
    return siezof(*a_pkt) + a_pkt->group_len + a_pkt->key_len + a_pkt->sign_len;
}

typedef struct dap_global_db_pkt_pack {
    uint64_t data_size;
    uint32_t obj_count;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_pack_t;
