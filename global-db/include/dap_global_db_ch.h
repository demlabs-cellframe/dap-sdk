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

#include "dap_common.h"
#include "dap_time.h"
#include "dap_timerfd.h"
#include "dap_global_db_pkt.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_worker.h"

#define DAP_GLOBAL_DB_TASK_PRIORITY DAP_QUEUE_MSG_PRIORITY_LOW

enum dap_stream_ch_gdb_state {
    DAP_STREAM_CH_GDB_STATE_IDLE,
    DAP_STREAM_CH_GDB_STATE_UPDATE,
    DAP_STREAM_CH_GDB_STATE_SYNC,
    DAP_STREAM_CH_GDB_STATE_UPDATE_REMOTE,
    DAP_STREAM_CH_GDB_STATE_SYNC_REMOTE
};

enum dap_global_db_cluster_pkt_type {
    DAP_STREAM_CH_GDB_PKT_TYPE_RECORD_PACK,
    DAP_STREAM_CH_GDB_PKT_TYPE_PERIODIC,
    DAP_STREAM_CH_GDB_PKT_TYPE_DELETE
};

typedef struct dap_stream_ch_gdb_pkt {
    uint8_t version;
    uint8_t padding[7];
    dap_stream_node_addr_t sender_addr;
    dap_stream_node_addr_t receiver_addr;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_stream_ch_gdb_pkt_t;

typedef struct dap_stream_ch_gdb {
    void *_inheritor;

    enum dap_stream_ch_gdb_state state;
    uint64_t stats_request_gdb_processed;

    dap_global_db_driver_hash_t *remote_gdbs; // Remote gdbs

    //dap_stream_ch_chain_pkt_hdr_t request_hdr;
    //dap_list_t *request_db_iter;

    int timer_shots;
    dap_timerfd_t *activity_timer;
    int sent_breaks;

    dap_stream_ch_callback_packet_t callback_notify_packet_out;
    dap_stream_ch_callback_packet_t callback_notify_packet_in;
    void *callback_notify_arg;
} dap_stream_ch_gdb_t;

#define DAP_STREAM_CH_GDB(a) ((dap_stream_ch_gdb_t *) ((a)->internal) )
#define DAP_STREAM_CH(a) ((dap_stream_ch_t *)((a)->_inheritor))
#define DAP_STREAM_CH_GDB_ID 'D'

int dap_global_db_ch_init();
void dap_global_db_ch_deinit();
dap_stream_ch_gdb_pkt_t *dap_global_db_ch_pkt_new();
