/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
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
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "dap_enc_key.h"
#include "dap_events_socket.h"
#include "dap_serialize.h"
typedef struct dap_stream dap_stream_t;
typedef struct dap_stream_session dap_stream_session_t;
#define STREAM_PKT_TYPE_DATA_PACKET 0x00
#define STREAM_PKT_TYPE_FRAGMENT_PACKET 0x01
#define STREAM_PKT_TYPE_SERVICE_PACKET 0xff
#define STREAM_PKT_TYPE_KEEPALIVE   0x11
#define STREAM_PKT_TYPE_ALIVE       0x12
#define STREAM_PKT_SIG_SIZE         8

#define DAP_STREAM_PKT_ENCRYPTION_OVERHEAD 200 //in fact is's about 2*16+15 for OAES

/**
 * Wire-format stream packet header (packed, 37 bytes).
 * Used on-the-wire and in dap_stream_pkt_t FAM wrapper.
 */
typedef struct dap_stream_pkt_hdr {
    uint8_t sig[STREAM_PKT_SIG_SIZE];
    uint32_t size;
    uint64_t timestamp;
    uint8_t type;
    uint64_t src_addr;
    uint64_t dst_addr;
} DAP_ALIGN_PACKED dap_stream_pkt_hdr_t;

#define DAP_STREAM_PKT_HDR_WIRE_SIZE 37
_Static_assert(sizeof(dap_stream_pkt_hdr_t) == DAP_STREAM_PKT_HDR_WIRE_SIZE,
               "dap_stream_pkt_hdr_t wire size");

/**
 * Naturally aligned in-memory version of the stream packet header.
 * Safe for WASM atomics and all platforms; use pack/unpack for conversion.
 */
typedef struct dap_stream_pkt_hdr_mem {
    uint8_t sig[STREAM_PKT_SIG_SIZE];
    uint32_t size;
    uint64_t timestamp;
    uint8_t type;
    uint64_t src_addr;
    uint64_t dst_addr;
} dap_stream_pkt_hdr_mem_t;

extern const dap_serialize_field_t g_dap_stream_pkt_hdr_fields[];
extern const dap_serialize_schema_t g_dap_stream_pkt_hdr_schema;
#define DAP_STREAM_PKT_HDR_MAGIC 0xDA5FEED1U

static inline int dap_stream_pkt_hdr_pack(const dap_stream_pkt_hdr_mem_t *a_mem,
                                           uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_STREAM_PKT_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_stream_pkt_hdr_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

static inline int dap_stream_pkt_hdr_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                             dap_stream_pkt_hdr_mem_t *a_mem)
{
    if (a_wire_size < DAP_STREAM_PKT_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_stream_pkt_hdr_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

typedef struct dap_stream_fragment_pkt {
    uint32_t size;
    uint32_t mem_shift;
    uint32_t full_size;
    uint8_t data[];
} dap_stream_fragment_pkt_t;
_Static_assert(sizeof(dap_stream_fragment_pkt_t) == 12, "stream fragment pkt wire size");

typedef struct dap_stream_pkt {
    dap_stream_pkt_hdr_t hdr;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_stream_pkt_t;

/**
 * Wire-format stream service packet (packed, 9 bytes).
 */
typedef struct dap_stream_srv_pkt {
    uint32_t session_id;
    uint8_t enc_type;
    uint32_t coockie;
} DAP_ALIGN_PACKED dap_stream_srv_pkt_t;

#define DAP_STREAM_SRV_PKT_WIRE_SIZE 9
_Static_assert(sizeof(dap_stream_srv_pkt_t) == DAP_STREAM_SRV_PKT_WIRE_SIZE,
               "dap_stream_srv_pkt_t wire size");

/**
 * Naturally aligned in-memory version of the service packet.
 */
typedef struct dap_stream_srv_pkt_mem {
    uint32_t session_id;
    uint8_t enc_type;
    uint32_t coockie;
} dap_stream_srv_pkt_mem_t;

extern const dap_serialize_field_t g_dap_stream_srv_pkt_fields[];
extern const dap_serialize_schema_t g_dap_stream_srv_pkt_schema;
#define DAP_STREAM_SRV_PKT_MAGIC 0xDA5FEED2U

static inline int dap_stream_srv_pkt_pack(const dap_stream_srv_pkt_mem_t *a_mem,
                                           uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_STREAM_SRV_PKT_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_stream_srv_pkt_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

static inline int dap_stream_srv_pkt_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                             dap_stream_srv_pkt_mem_t *a_mem)
{
    if (a_wire_size < DAP_STREAM_SRV_PKT_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_stream_srv_pkt_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

extern const uint8_t c_dap_stream_sig[8];

dap_stream_pkt_t * dap_stream_pkt_detect(void * a_data, size_t data_size);

size_t dap_stream_pkt_read_unsafe(dap_stream_t * a_stream, dap_stream_pkt_t * a_pkt, void * a_buf_out, size_t a_buf_out_size);

size_t dap_stream_pkt_write_unsafe(dap_stream_t * a_stream, uint8_t a_type, const void * data, size_t a_data_size);
size_t dap_stream_pkt_write_mt (dap_worker_t * a_w, dap_events_socket_uuid_t a_es_uuid, dap_enc_key_t *a_key, const void * data, size_t a_data_size);

void dap_stream_send_keepalive( dap_stream_t * a_stream);


