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
#include "dap_hash.h"
#include "dap_cluster.h"
#include "dap_serialize.h"

typedef enum dap_gossip_msg_type {
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST,
    DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA
} dap_gossip_msg_type_t;

// This is packet type for epidemic update broadcasting between cluster members
typedef struct dap_gossip_msg {
    uint8_t     version;                    // Retranslation protocol version
    uint8_t     payload_ch_id;              // Channel ID of payload callback
    byte_t      padding[2];
    uint32_t    trace_len;                  // Size of tracepath, in bytes
    uint64_t    payload_len;                // Size of payload, bytes
    dap_guuid_t cluster_id;                 // Links cluster ID to message retranslate to
    dap_hash_t  payload_hash;               // Payoad hash for doubles check
    byte_t      trace_n_payload[];          // Serialized form of message tracepath and payload itself
} dap_gossip_msg_t;
_Static_assert(sizeof(dap_gossip_msg_t) == 64, "gossip msg wire size");

/** Same layout as @ref dap_gossip_msg_t (fixed 64-byte prefix; wire and natural alignment match). */
typedef dap_gossip_msg_t dap_gossip_msg_mem_t;

#define DAP_GOSSIP_MSG_MAGIC 0xDA5FEEDAU

extern const dap_serialize_field_t g_dap_gossip_msg_fields[];
extern const dap_serialize_schema_t g_dap_gossip_msg_schema;

#define DAP_GOSSIP_MSG_HDR_WIRE_SIZE 64

static inline int dap_gossip_msg_hdr_pack(const dap_gossip_msg_mem_t *a_mem,
                                          uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_GOSSIP_MSG_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_gossip_msg_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

static inline int dap_gossip_msg_hdr_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                            dap_gossip_msg_mem_t *a_mem)
{
    if (a_wire_size < DAP_GOSSIP_MSG_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_gossip_msg_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

typedef void (*dap_gossip_callback_payload_t)(void *a_payload, size_t a_payload_size, dap_cluster_node_addr_t a_sender_addr);

#define DAP_STREAM_CH_GOSSIP_ID     'G'
#define DAP_GOSSIP_CURRENT_VERSION  1
#define DAP_GOSSIP_LIFETIME         15      // seconds

DAP_STATIC_INLINE uint64_t dap_gossip_msg_get_size(dap_gossip_msg_t *a_msg) { return sizeof(dap_gossip_msg_t) + (uint64_t)a_msg->trace_len + a_msg->payload_len <  sizeof(dap_gossip_msg_t) + (uint64_t)a_msg->trace_len
                                                                                    ? 0 : sizeof(dap_gossip_msg_t) + (uint64_t)a_msg->trace_len + a_msg->payload_len; }
int dap_stream_ch_gossip_init();
void dap_stream_ch_gossip_deinit();
void dap_gossip_msg_issue(dap_cluster_t *a_cluster, const char a_ch_id, const void *a_payload, size_t a_payload_size, dap_hash_fast_t *a_payload_hash);
int dap_stream_ch_gossip_callback_add(const char a_ch_id, dap_gossip_callback_payload_t a_callback);
