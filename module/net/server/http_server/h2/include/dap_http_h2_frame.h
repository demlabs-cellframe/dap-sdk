/*
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 * DAP SDK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_H2_FRAME_HEADER_SIZE 9

/* Frame types (RFC 7540 section 6) */
#define DAP_H2_FRAME_DATA          0x00
#define DAP_H2_FRAME_HEADERS       0x01
#define DAP_H2_FRAME_PRIORITY      0x02
#define DAP_H2_FRAME_RST_STREAM    0x03
#define DAP_H2_FRAME_SETTINGS      0x04
#define DAP_H2_FRAME_PUSH_PROMISE  0x05
#define DAP_H2_FRAME_PING          0x06
#define DAP_H2_FRAME_GOAWAY        0x07
#define DAP_H2_FRAME_WINDOW_UPDATE 0x08
#define DAP_H2_FRAME_CONTINUATION  0x09

/* Flags */
#define DAP_H2_FLAG_END_STREAM     0x01
#define DAP_H2_FLAG_ACK            0x01
#define DAP_H2_FLAG_END_HEADERS    0x04
#define DAP_H2_FLAG_PADDED         0x08
#define DAP_H2_FLAG_PRIORITY       0x20

/* Error codes (RFC 7540 section 7) */
#define DAP_H2_NO_ERROR            0x00
#define DAP_H2_PROTOCOL_ERROR      0x01
#define DAP_H2_INTERNAL_ERROR      0x02
#define DAP_H2_FLOW_CONTROL_ERROR  0x03
#define DAP_H2_SETTINGS_TIMEOUT    0x04
#define DAP_H2_STREAM_CLOSED       0x05
#define DAP_H2_FRAME_SIZE_ERROR    0x06
#define DAP_H2_REFUSED_STREAM      0x07
#define DAP_H2_CANCEL              0x08
#define DAP_H2_COMPRESSION_ERROR   0x09
#define DAP_H2_CONNECT_ERROR       0x0a
#define DAP_H2_ENHANCE_YOUR_CALM   0x0b
#define DAP_H2_INADEQUATE_SECURITY 0x0c
#define DAP_H2_HTTP_1_1_REQUIRED   0x0d

/* Settings identifiers (RFC 7540 section 6.5.2) */
#define DAP_H2_SETTINGS_HEADER_TABLE_SIZE      0x01
#define DAP_H2_SETTINGS_ENABLE_PUSH            0x02
#define DAP_H2_SETTINGS_MAX_CONCURRENT_STREAMS 0x03
#define DAP_H2_SETTINGS_INITIAL_WINDOW_SIZE    0x04
#define DAP_H2_SETTINGS_MAX_FRAME_SIZE         0x05
#define DAP_H2_SETTINGS_MAX_HEADER_LIST_SIZE   0x06

/* Default values */
#define DAP_H2_DEFAULT_HEADER_TABLE_SIZE    4096
#define DAP_H2_DEFAULT_ENABLE_PUSH          1
#define DAP_H2_DEFAULT_MAX_CONCURRENT       100
#define DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE  65535
#define DAP_H2_DEFAULT_MAX_FRAME_SIZE       16384
#define DAP_H2_MAX_FRAME_SIZE_LIMIT         16777215
#define DAP_H2_DEFAULT_WINDOW_SIZE          65535
#define DAP_H2_CONNECTION_PREFACE           "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define DAP_H2_CONNECTION_PREFACE_LEN       24

typedef struct dap_h2_frame_header {
    uint32_t length;        /* 24 bits                         */
    uint8_t  type;
    uint8_t  flags;
    uint32_t stream_id;     /* 31 bits (R bit always 0)        */
} dap_h2_frame_header_t;

typedef struct dap_h2_settings_entry {
    uint16_t id;
    uint32_t value;
} dap_h2_settings_entry_t;

/**
 * @brief Parse and serialize the fixed 9-byte HTTP/2 frame header.
 */
int dap_h2_frame_header_parse(const uint8_t *src, size_t src_len, dap_h2_frame_header_t *out);
int dap_h2_frame_header_serialize(const dap_h2_frame_header_t *hdr, uint8_t *dst, size_t dst_cap);

/**
 * @brief Build common HTTP/2 frame types into a caller buffer.
 */
size_t dap_h2_frame_settings(uint8_t *dst, size_t dst_cap, const dap_h2_settings_entry_t *entries, size_t count);
size_t dap_h2_frame_settings_ack(uint8_t *dst, size_t dst_cap);
size_t dap_h2_frame_ping(uint8_t *dst, size_t dst_cap, const uint8_t opaque[8], bool ack);
size_t dap_h2_frame_goaway(uint8_t *dst, size_t dst_cap, uint32_t last_stream_id, uint32_t error_code, const uint8_t *debug_data, size_t debug_len);
size_t dap_h2_frame_rst_stream(uint8_t *dst, size_t dst_cap, uint32_t stream_id, uint32_t error_code);
size_t dap_h2_frame_window_update(uint8_t *dst, size_t dst_cap, uint32_t stream_id, uint32_t increment);
size_t dap_h2_frame_headers(uint8_t *dst, size_t dst_cap, uint32_t stream_id, uint8_t flags, const uint8_t *hpack_block, size_t hpack_len);
size_t dap_h2_frame_data(uint8_t *dst, size_t dst_cap, uint32_t stream_id, uint8_t flags, const uint8_t *payload, size_t payload_len);

/**
 * @brief Validate a frame header against payload length and RFC rules.
 */
int dap_h2_frame_validate(const dap_h2_frame_header_t *hdr, size_t payload_len);

/**
 * @brief Map HTTP/2 error codes and frame types to human-readable names.
 */
const char *dap_h2_error_str(uint32_t error_code);
const char *dap_h2_frame_type_str(uint8_t type);

#ifdef __cplusplus
}
#endif
