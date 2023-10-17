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
#include "dap_stream_ch_pkt.h"
#include "dap_stream_worker.h"
#include "dap_guuid.h"

typedef enum dap_gossip_msg_type {
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST,
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA
} dap_gossip_msg_type_t;

// This is packet type for epidemic update broadcasting between cluster members
typedef struct dap_gossip_msg {
    dap_guuid_t cluster_id;                 // Links cluster ID to message retranslate to
    uint8_t     version;                    // Retranslation protocol version
    uint8_t     payload_ch_id;              // Channel ID of payload callback
    byte_t      padding[2];
    uint32_t    trace_len;                  // Size of tracepath, in bytes
    uint64_t    payload_len;                // Size of payload, bytes
    uint16_t    payload_hash_len;           // Size of payoad hash for doubles check callback
    byte_t      hash_n_trace_n_payload;     // Serialized form for payload hash, message tracepath and payload itself
} DAP_ALIGN_PACKED dap_gossip_msg_t;

typedef bool (*dap_gossip_callback_check_t)(void *a_hash, size_t a_hash_size);
typedef void (*dap_gossip_callback_payload_t)(void *a_payload, size_t *a_payload_size, void *a_hash, size_t a_hash_size);

#define DAP_STREAM_CH_GOSSIP_ID     'G'
#define DAP_GOSSIP_CURRENT_VERSION  1
#define DAP_GOSSIP_LIFETIME         10      // seconds

DAP_STATIC_INLINE size_t dap_gossip_msg_size_get(a_msg) { return sizeof(dap_gossip_msg_t) + a_msg->payload_hash_len + l_msg->trace_len + l_msg->payload_len; }
int dap_stream_ch_gossip_init();
void dap_stream_ch_gossip_deinit();
void dap_gossip_msg_retranslate(dap_gossip_msg_t *a_msg, dap_stream_node_addr_t *a_tracepath, size_t a_step_count);
