/*
 Copyright (c) 2020 (c) DeM Labs Ltd http://demlabs.net
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/


#pragma once

#define STREAM_CH_PKT_TYPE_REQUEST      0x0
//#define STREAM_CH_PKT_TYPE_KEEPALIVE    0x11

#include <stdint.h>
#include <stddef.h>
#include "dap_stream.h"
#include "dap_enc_key.h"
#include "dap_context_queue.h"
#include "dap_serialize.h"

typedef unsigned int dap_stream_ch_uuid_t;

/**
 * Wire-format channel packet header (packed, 16 bytes).
 */
typedef struct dap_stream_ch_pkt_hdr {
    uint8_t id;
    uint8_t enc_type;
    uint8_t type;
    uint8_t padding;
    uint64_t seq_id;
    uint32_t data_size;
} DAP_ALIGN_PACKED dap_stream_ch_pkt_hdr_t;

#define DAP_STREAM_CH_PKT_HDR_WIRE_SIZE 16
_Static_assert(sizeof(dap_stream_ch_pkt_hdr_t) == DAP_STREAM_CH_PKT_HDR_WIRE_SIZE,
               "dap_stream_ch_pkt_hdr_t wire size");

/**
 * Naturally aligned in-memory version. Identical field order,
 * compiler inserts alignment padding automatically.
 */
typedef struct dap_stream_ch_pkt_hdr_mem {
    uint8_t id;
    uint8_t enc_type;
    uint8_t type;
    uint8_t padding;
    uint64_t seq_id;
    uint32_t data_size;
} dap_stream_ch_pkt_hdr_mem_t;

extern const dap_serialize_field_t g_dap_stream_ch_pkt_hdr_fields[];
extern const dap_serialize_schema_t g_dap_stream_ch_pkt_hdr_schema;
#define DAP_STREAM_CH_PKT_HDR_MAGIC 0xDA5FEED3U

static inline int dap_stream_ch_pkt_hdr_pack(const dap_stream_ch_pkt_hdr_mem_t *a_mem,
                                              uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_STREAM_CH_PKT_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_stream_ch_pkt_hdr_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

static inline int dap_stream_ch_pkt_hdr_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                                dap_stream_ch_pkt_hdr_mem_t *a_mem)
{
    if (a_wire_size < DAP_STREAM_CH_PKT_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_stream_ch_pkt_hdr_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

typedef struct dap_stream_ch_pkt{
    dap_stream_ch_pkt_hdr_t hdr;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_stream_ch_pkt_t;

typedef void (*dap_stream_ch_callback_packet_t)(void *, uint8_t, dap_stream_ch_pkt_t *, void *);

int dap_stream_ch_pkt_init();
void dap_stream_ch_pkt_deinit();

DAP_PRINTF_ATTR(3, 4) ssize_t dap_stream_ch_pkt_write_f_unsafe(dap_stream_ch_t *a_ch, uint8_t a_type, const char *a_format, ...);
size_t dap_stream_ch_pkt_write_unsafe(dap_stream_ch_t * a_ch,  uint8_t a_type, const void * a_data, size_t a_data_size);

int dap_stream_ch_pkt_send_by_addr(dap_cluster_node_addr_t *a_addr, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size);

DAP_PRINTF_ATTR(4, 5) size_t dap_stream_ch_pkt_write_f_mt(dap_stream_worker_t *a_worker , dap_stream_ch_uuid_t a_ch_uuid, uint8_t a_type, const char *a_str, ...);
size_t dap_stream_ch_pkt_write_mt(dap_stream_worker_t * a_worker , dap_stream_ch_uuid_t a_ch_uuid,  uint8_t a_type, const void * a_data, size_t a_data_size);
int dap_stream_ch_pkt_send_mt(dap_stream_worker_t *a_worker, dap_events_socket_uuid_t a_uuid, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size);

DAP_PRINTF_ATTR(4, 5) size_t dap_stream_ch_pkt_write_f_inter(dap_context_queue_t *a_queue_input , dap_stream_ch_uuid_t a_ch_uuid, uint8_t a_type, const char *a_str, ...);
size_t dap_stream_ch_pkt_write_inter(dap_context_queue_t * a_queue_input , dap_stream_ch_uuid_t a_ch_uuid,  uint8_t a_type, const void * a_data, size_t a_data_size);

#define dap_stream_ch_pkt_write dap_stream_ch_pkt_write_mt
#define dap_stream_ch_pkt_write_f dap_stream_ch_pkt_write_f_mt
