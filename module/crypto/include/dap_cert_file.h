/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2018
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include <stdint.h>
#include <time.h>
#include "dap_common.h"
#include "dap_cert.h"
#include "dap_math_ops.h"
#include "dap_serialize.h"

// Magic .dapcert signature
#define dap_cert_FILE_HDR_SIGN 0x0F300C4711E29380
#define dap_cert_FILE_VERSION 1

// Default certificate with private key and optionaly some signs
#define dap_cert_FILE_TYPE_PRIVATE 0x00
// Default certificate with public key and optionaly some signs
#define dap_cert_FILE_TYPE_PUBLIC 0xf0


typedef struct dap_cert_file_hdr
{
    uint64_t sign;
    int version;
    uint8_t type;
    dap_sign_type_t sign_type;
    uint64_t data_size;
    uint64_t data_pvt_size;
    uint64_t metadata_size;
    time_t ts_last_used;
} DAP_ALIGN_PACKED dap_cert_file_hdr_t;
DAP_STATIC_ASSERT(sizeof(dap_cert_file_hdr_t) == sizeof(uint64_t) + sizeof(int) + sizeof(uint8_t) +
                      sizeof(dap_sign_type_t) + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t) +
                      sizeof(time_t),
                  "dap_cert_file_hdr_t packed wire size");

/**
 * Naturally aligned in-memory view for dap_cert_file_hdr_t wire fields.
 */
typedef struct dap_cert_file_hdr_mem {
    uint64_t sign;
    int32_t version;
    uint8_t type;
    uint32_t sign_type_raw;
    uint64_t data_size;
    uint64_t data_pvt_size;
    uint64_t metadata_size;
    time_t ts_last_used;
} dap_cert_file_hdr_mem_t;

extern const dap_serialize_field_t g_dap_cert_file_hdr_fields[];
extern const dap_serialize_schema_t g_dap_cert_file_hdr_schema;
#define DAP_CERT_FILE_HDR_MAGIC 0xDA5FEEDDU
#define DAP_CERT_FILE_HDR_WIRE_SIZE sizeof(dap_cert_file_hdr_t)

DAP_STATIC_INLINE int dap_cert_file_hdr_pack(const dap_cert_file_hdr_mem_t *a_mem,
                                             uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_CERT_FILE_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_cert_file_hdr_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

DAP_STATIC_INLINE int dap_cert_file_hdr_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                               dap_cert_file_hdr_mem_t *a_mem)
{
    if (a_wire_size < DAP_CERT_FILE_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_cert_file_hdr_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

typedef struct dap_cert_file{
    dap_cert_file_hdr_t hdr;
    uint8_t data[];
}DAP_ALIGN_PACKED dap_cert_file_t;

typedef struct dap_cert_file_aux {
    size_t *buf;
    size_t idx;
} dap_cert_file_aux_t;

#ifdef __cplusplus
extern "C" {
#endif

int dap_cert_file_save(dap_cert_t * a_cert, const char * a_cert_file_path);
uint8_t* dap_cert_mem_save(dap_cert_t * a_cert, uint32_t *a_cert_size_out);

dap_cert_t* dap_cert_file_load(const char * a_cert_file_path);
dap_cert_t* dap_cert_mem_load(const void * a_data, size_t a_data_size);

#ifdef __cplusplus
}
#endif
