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
#include "dap_proc_thread.h"
#include "dap_stream_ch.h"

/**
  * @struct Node address
  *
  */
typedef union dap_stream_node_addr {
    uint64_t uint64;
    uint16_t words[sizeof(uint64_t)/2];
    uint8_t raw[sizeof(uint64_t)];  // Access to selected octects
} DAP_ALIGN_PACKED dap_stream_node_addr_t;

typedef dap_stream_node_addr_t dap_chain_node_addr_t;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define NODE_ADDR_FP_STR      "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)  a->words[2],a->words[3],a->words[0],a->words[1]
#define NODE_ADDR_FPS_ARGS(a)  &a->words[2],&a->words[3],&a->words[0],&a->words[1]
#define NODE_ADDR_FP_ARGS_S(a)  a.words[2],a.words[3],a.words[0],a.words[1]
#define NODE_ADDR_FPS_ARGS_S(a)  &a.words[2],&a.words[3],&a.words[0],&a.words[1]
#else
#define NODE_ADDR_FP_STR      "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)  a->words[3],a->words[2],a->words[1],a->words[0]
#define NODE_ADDR_FPS_ARGS(a)  &a->words[3],&a->words[2],&a->words[1],&a->words[0]
#define NODE_ADDR_FP_ARGS_S(a)  a.words[3],a.words[2],a.words[1],a.words[0]
#define NODE_ADDR_FPS_ARGS_S(a)  &a.words[3],&a.words[2],&a.words[1],&a.words[0]
#endif

DAP_STATIC_INLINE bool dap_stream_node_addr_str_check(const char *a_addr_str) {
    size_t l_str_len = dap_strlen(a_addr_str);
    if (l_str_len == 22) {
        for (int n =0; n < 22; n+= 6) {
            if (!dap_is_xdigit(a_addr_str[n]) || !dap_is_xdigit(a_addr_str[n + 1]) ||
                !dap_is_xdigit(a_addr_str[n + 2]) || !dap_is_xdigit(a_addr_str[n + 3])) {
                return false;
            }
        }
        for (int n = 4; n < 18; n += 6) {
            if (a_addr_str[n] != ':' || a_addr_str[n + 1] != ':')
                return false;
        }
        return true;
    }
    return false;
}

#define dap_chain_node_addr_str_check dap_stream_node_addr_str_check

DAP_STATIC_INLINE int dap_stream_node_addr_from_str(dap_stream_node_addr_t *a_addr, const char *a_addr_str)
{
    if (!a_addr || !a_addr_str){
        return -1;
    }
    if (sscanf(a_addr_str, NODE_ADDR_FP_STR, NODE_ADDR_FPS_ARGS(a_addr)) == 4)
        return 0;
    if (sscanf(a_addr_str, "0x%016" DAP_UINT64_FORMAT_x, &a_addr->uint64) == 1)
        return 0;
    return -1;
}

#define dap_chain_node_addr_from_str dap_stream_node_addr_from_str

DAP_STATIC_INLINE bool dap_stream_node_addr_not_null(dap_stream_node_addr_t * a_addr) { return a_addr->uint64 != 0; }

#define dap_chain_node_addr_not_null dap_stream_node_addr_not_null

typedef struct dap_stream_worker {
    dap_worker_t * worker;
    dap_events_socket_t *queue_ch_io; // IO queue for channels
    dap_events_socket_t **queue_ch_io_input; // IO queue inputs for channels
    dap_stream_ch_t * channels; // Client channels assigned on worker. Unsafe list, operate only in worker's context
    pthread_rwlock_t channels_rwlock;
} dap_stream_worker_t;

#define DAP_STREAM_WORKER(a) ((dap_stream_worker_t*) (a->_inheritor)  )

typedef struct dap_stream_worker_msg_io {
    dap_stream_ch_uuid_t ch_uuid;
    uint32_t flags_set; // set flags
    uint32_t flags_unset; // unset flags
    uint8_t ch_pkt_type;
    void * data;
    size_t data_size;
} dap_stream_worker_msg_io_t;

int dap_stream_worker_init();

size_t dap_proc_thread_stream_ch_write_inter(dap_proc_thread_t * a_thread,dap_worker_t * a_worker, dap_stream_ch_uuid_t a_ch_uuid,
                                        uint8_t a_type,const void * a_data, size_t a_data_size);

DAP_PRINTF_ATTR(5, 6) size_t dap_proc_thread_stream_ch_write_f_inter(dap_proc_thread_t *a_thread,
                                                                     dap_worker_t *a_worker,
                                                                     dap_stream_ch_uuid_t a_ch_uuid,
                                                                     uint8_t a_type,
                                                                     const char *a_format,
                                                                     ...);
