/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2020
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
#include "dap_worker.h"
#include "dap_stream_ch.h"

#include "dap_proc_thread.h"
typedef struct dap_context_queue dap_context_queue_t;

typedef struct dap_stream_worker {
    dap_worker_t *worker;
    dap_stream_ch_t *channels;
    dap_context_queue_t *queue_ch_io;
    dap_context_queue_t *queue_ch_send;
    dap_context_queue_t **queue_ch_io_input;
    pthread_rwlock_t channels_rwlock;
} dap_stream_worker_t;

#define DAP_STREAM_WORKER(a) ((dap_stream_worker_t *)(a->_inheritor))

typedef struct dap_stream_worker_msg_io {
    dap_stream_ch_uuid_t ch_uuid;
    uint32_t flags_set;
    uint32_t flags_unset;
    uint8_t ch_pkt_type;
    void *data;
    size_t data_size;
} dap_stream_worker_msg_io_t;

typedef struct dap_stream_worker_msg_send {
    dap_events_socket_uuid_t uuid;
    char ch_id;
    uint8_t ch_pkt_type;
    void *data;
    size_t data_size;
} dap_stream_worker_msg_send_t;

int dap_stream_worker_init(void);
