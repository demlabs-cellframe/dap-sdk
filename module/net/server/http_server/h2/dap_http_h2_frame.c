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

#include <inttypes.h>
#include <string.h>

#include "dap_common.h"
#include "dap_http_h2_frame.h"

#define LOG_TAG "dap_http_h2_frame"

static uint32_t s_read_be24(const uint8_t *a_p)
{
    return ((uint32_t)a_p[0] << 16) | ((uint32_t)a_p[1] << 8) | (uint32_t)a_p[2];
}

static uint32_t s_read_be32(const uint8_t *a_p)
{
    return ((uint32_t)a_p[0] << 24) | ((uint32_t)a_p[1] << 16) | ((uint32_t)a_p[2] << 8) | (uint32_t)a_p[3];
}

static void s_write_be24(uint8_t *a_p, uint32_t a_v)
{
    a_p[0] = (uint8_t)((a_v >> 16) & 0xffu);
    a_p[1] = (uint8_t)((a_v >> 8) & 0xffu);
    a_p[2] = (uint8_t)(a_v & 0xffu);
}

static void s_write_be32(uint8_t *a_p, uint32_t a_v)
{
    a_p[0] = (uint8_t)((a_v >> 24) & 0xffu);
    a_p[1] = (uint8_t)((a_v >> 16) & 0xffu);
    a_p[2] = (uint8_t)((a_v >> 8) & 0xffu);
    a_p[3] = (uint8_t)(a_v & 0xffu);
}

static void s_write_be16(uint8_t *a_p, uint16_t a_v)
{
    a_p[0] = (uint8_t)((a_v >> 8) & 0xffu);
    a_p[1] = (uint8_t)(a_v & 0xffu);
}

static int s_frame_header_write(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_length, uint8_t a_type, uint8_t a_flags, uint32_t a_stream_id)
{
    dap_h2_frame_header_t l_hdr = { .length = a_length, .type = a_type, .flags = a_flags, .stream_id = a_stream_id };
    return dap_h2_frame_header_serialize(&l_hdr, a_dst, a_dst_cap);
}

/**
 * @brief Parse a 9-byte on-the-wire frame header into a structure.
 */
int dap_h2_frame_header_parse(const uint8_t *a_src, size_t a_src_len, dap_h2_frame_header_t *a_out)
{
    if (!a_src || !a_out) {
        log_it(L_ERROR, "parse: null argument");
        return -1;
    }
    if (a_src_len < DAP_H2_FRAME_HEADER_SIZE) {
        log_it(L_WARNING, "parse: need %u bytes, got %zu", DAP_H2_FRAME_HEADER_SIZE, a_src_len);
        return -1;
    }
    a_out->length = s_read_be24(a_src) & 0xffffffu;
    a_out->type = a_src[3];
    a_out->flags = a_src[4];
    a_out->stream_id = s_read_be32(&a_src[5]) & 0x7fffffffu;
    return 0;
}

/**
 * @brief Serialize a frame header structure into a 9-byte on-the-wire form.
 */
int dap_h2_frame_header_serialize(const dap_h2_frame_header_t *a_hdr, uint8_t *a_dst, size_t a_dst_cap)
{
    if (!a_hdr || !a_dst) {
        log_it(L_ERROR, "serialize: null argument");
        return -1;
    }
    if (a_dst_cap < DAP_H2_FRAME_HEADER_SIZE) {
        log_it(L_WARNING, "serialize: buffer too small (%zu < %u)", a_dst_cap, DAP_H2_FRAME_HEADER_SIZE);
        return -1;
    }
    if (a_hdr->length > 0xffffffu) {
        log_it(L_WARNING, "serialize: length %" PRIu32 " exceeds 24 bits", a_hdr->length);
        return -1;
    }
    if (a_hdr->stream_id > 0x7fffffffu) {
        log_it(L_WARNING, "serialize: stream_id %" PRIu32 " exceeds 31 bits", a_hdr->stream_id);
        return -1;
    }
    s_write_be24(a_dst, a_hdr->length);
    a_dst[3] = a_hdr->type;
    a_dst[4] = a_hdr->flags;
    s_write_be32(&a_dst[5], a_hdr->stream_id);
    return (int)DAP_H2_FRAME_HEADER_SIZE;
}

/**
 * @brief Build a SETTINGS frame with the given entries.
 */
size_t dap_h2_frame_settings(uint8_t *a_dst, size_t a_dst_cap, const dap_h2_settings_entry_t *a_entries, size_t a_count)
{
    const size_t l_payload = a_count * 6u;
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + l_payload;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "settings: buffer too small or null (need %zu, cap %zu)", l_total, a_dst_cap);
        return 0;
    }
    if (a_count > 0 && !a_entries) {
        log_it(L_ERROR, "settings: null entries with count %zu", a_count);
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, (uint32_t)l_payload, DAP_H2_FRAME_SETTINGS, 0, 0) < 0)
        return 0;
    uint8_t *l_p = a_dst + DAP_H2_FRAME_HEADER_SIZE;
    for (size_t l_i = 0; l_i < a_count; l_i++) {
        s_write_be16(l_p, a_entries[l_i].id);
        s_write_be32(l_p + 2, a_entries[l_i].value);
        l_p += 6;
    }
    return l_total;
}

/**
 * @brief Build an empty SETTINGS frame with the ACK flag set.
 */
size_t dap_h2_frame_settings_ack(uint8_t *a_dst, size_t a_dst_cap)
{
    if (!a_dst || a_dst_cap < DAP_H2_FRAME_HEADER_SIZE) {
        log_it(L_ERROR, "settings_ack: buffer too small or null");
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, 0, DAP_H2_FRAME_SETTINGS, DAP_H2_FLAG_ACK, 0) < 0)
        return 0;
    return DAP_H2_FRAME_HEADER_SIZE;
}

/**
 * @brief Build a PING frame, optionally with ACK set.
 */
size_t dap_h2_frame_ping(uint8_t *a_dst, size_t a_dst_cap, const uint8_t a_opaque[8], bool a_ack)
{
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + 8u;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "ping: buffer too small or null");
        return 0;
    }
    uint8_t l_flags = a_ack ? DAP_H2_FLAG_ACK : 0;
    if (s_frame_header_write(a_dst, a_dst_cap, 8, DAP_H2_FRAME_PING, l_flags, 0) < 0)
        return 0;
    if (a_opaque)
        memcpy(a_dst + DAP_H2_FRAME_HEADER_SIZE, a_opaque, 8);
    else
        memset(a_dst + DAP_H2_FRAME_HEADER_SIZE, 0, 8);
    return l_total;
}

/**
 * @brief Build a GOAWAY frame with optional opaque debug data.
 */
size_t dap_h2_frame_goaway(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_last_stream_id, uint32_t a_error_code,
                           const uint8_t *a_debug_data, size_t a_debug_len)
{
    const size_t l_payload = 8u + a_debug_len;
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + l_payload;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "goaway: buffer too small or null");
        return 0;
    }
    if (a_debug_len > 0 && !a_debug_data) {
        log_it(L_ERROR, "goaway: debug_data null with debug_len %zu", a_debug_len);
        return 0;
    }
    if (a_last_stream_id > 0x7fffffffu) {
        log_it(L_WARNING, "goaway: last_stream_id exceeds 31 bits");
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, (uint32_t)l_payload, DAP_H2_FRAME_GOAWAY, 0, 0) < 0)
        return 0;
    uint8_t *l_p = a_dst + DAP_H2_FRAME_HEADER_SIZE;
    s_write_be32(l_p, a_last_stream_id & 0x7fffffffu);
    s_write_be32(l_p + 4, a_error_code);
    if (a_debug_len > 0)
        memcpy(l_p + 8, a_debug_data, a_debug_len);
    return l_total;
}

/**
 * @brief Build an RST_STREAM frame.
 */
size_t dap_h2_frame_rst_stream(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_stream_id, uint32_t a_error_code)
{
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + 4u;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "rst_stream: buffer too small or null");
        return 0;
    }
    if (a_stream_id == 0) {
        log_it(L_WARNING, "rst_stream: stream_id must be non-zero");
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, 4, DAP_H2_FRAME_RST_STREAM, 0, a_stream_id & 0x7fffffffu) < 0)
        return 0;
    s_write_be32(a_dst + DAP_H2_FRAME_HEADER_SIZE, a_error_code);
    return l_total;
}

/**
 * @brief Build a WINDOW_UPDATE frame.
 */
size_t dap_h2_frame_window_update(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_stream_id, uint32_t a_increment)
{
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + 4u;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "window_update: buffer too small or null");
        return 0;
    }
    if (a_increment == 0 || a_increment > 0x7fffffffu) {
        log_it(L_WARNING, "window_update: invalid increment %" PRIu32, a_increment);
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, 4, DAP_H2_FRAME_WINDOW_UPDATE, 0, a_stream_id & 0x7fffffffu) < 0)
        return 0;
    s_write_be32(a_dst + DAP_H2_FRAME_HEADER_SIZE, a_increment & 0x7fffffffu);
    return l_total;
}

/**
 * @brief Build a HEADERS frame carrying an HPACK block.
 */
size_t dap_h2_frame_headers(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_stream_id, uint8_t a_flags,
                            const uint8_t *a_hpack_block, size_t a_hpack_len)
{
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + a_hpack_len;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "headers: buffer too small or null");
        return 0;
    }
    if (a_hpack_len > 0 && !a_hpack_block) {
        log_it(L_ERROR, "headers: null hpack_block with len %zu", a_hpack_len);
        return 0;
    }
    if (a_stream_id == 0) {
        log_it(L_WARNING, "headers: stream_id must be non-zero");
        return 0;
    }
    if (a_hpack_len > DAP_H2_MAX_FRAME_SIZE_LIMIT) {
        log_it(L_WARNING, "headers: hpack_len exceeds max");
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, (uint32_t)a_hpack_len, DAP_H2_FRAME_HEADERS, a_flags, a_stream_id & 0x7fffffffu) < 0)
        return 0;
    if (a_hpack_len > 0)
        memcpy(a_dst + DAP_H2_FRAME_HEADER_SIZE, a_hpack_block, a_hpack_len);
    return l_total;
}

/**
 * @brief Build a DATA frame with the given payload.
 */
size_t dap_h2_frame_data(uint8_t *a_dst, size_t a_dst_cap, uint32_t a_stream_id, uint8_t a_flags, const uint8_t *a_payload,
                         size_t a_payload_len)
{
    const size_t l_total = DAP_H2_FRAME_HEADER_SIZE + a_payload_len;

    if (!a_dst || a_dst_cap < l_total) {
        log_it(L_ERROR, "data: buffer too small or null");
        return 0;
    }
    if (a_payload_len > 0 && !a_payload) {
        log_it(L_ERROR, "data: null payload with len %zu", a_payload_len);
        return 0;
    }
    if (a_stream_id == 0) {
        log_it(L_WARNING, "data: stream_id must be non-zero");
        return 0;
    }
    if (a_payload_len > DAP_H2_MAX_FRAME_SIZE_LIMIT) {
        log_it(L_WARNING, "data: payload_len exceeds max");
        return 0;
    }
    if (s_frame_header_write(a_dst, a_dst_cap, (uint32_t)a_payload_len, DAP_H2_FRAME_DATA, a_flags, a_stream_id & 0x7fffffffu) < 0)
        return 0;
    if (a_payload_len > 0)
        memcpy(a_dst + DAP_H2_FRAME_HEADER_SIZE, a_payload, a_payload_len);
    return l_total;
}

/**
 * @brief Validate frame header and payload length per RFC 7540.
 */
int dap_h2_frame_validate(const dap_h2_frame_header_t *a_hdr, size_t a_payload_len)
{
    if (!a_hdr) {
        log_it(L_ERROR, "validate: null header");
        return -1;
    }
    if (a_hdr->length != a_payload_len) {
        log_it(L_WARNING, "validate: length mismatch (header %" PRIu32 " vs buffer %zu)", a_hdr->length, a_payload_len);
        return -1;
    }
    if (a_hdr->length > DAP_H2_MAX_FRAME_SIZE_LIMIT) {
        log_it(L_WARNING, "validate: payload length %" PRIu32 " exceeds RFC max", a_hdr->length);
        return -1;
    }
    if (a_hdr->stream_id > 0x7fffffffu) {
        log_it(L_WARNING, "validate: reserved stream bit set");
        return -1;
    }

    switch (a_hdr->type) {
    case DAP_H2_FRAME_DATA:
    case DAP_H2_FRAME_HEADERS:
        if (a_hdr->stream_id == 0) {
            log_it(L_WARNING, "validate: %s requires non-zero stream", dap_h2_frame_type_str(a_hdr->type));
            return -1;
        }
        break;
    case DAP_H2_FRAME_PRIORITY:
        if (a_hdr->stream_id == 0) {
            log_it(L_WARNING, "validate: PRIORITY requires non-zero stream");
            return -1;
        }
        if (a_payload_len != 5) {
            log_it(L_WARNING, "validate: PRIORITY payload must be 5 bytes");
            return -1;
        }
        break;
    case DAP_H2_FRAME_RST_STREAM:
        if (a_hdr->stream_id == 0) {
            log_it(L_WARNING, "validate: RST_STREAM requires non-zero stream");
            return -1;
        }
        if (a_payload_len != 4) {
            log_it(L_WARNING, "validate: RST_STREAM payload must be 4 bytes");
            return -1;
        }
        break;
    case DAP_H2_FRAME_SETTINGS:
        if (a_hdr->stream_id != 0) {
            log_it(L_WARNING, "validate: SETTINGS stream_id must be 0");
            return -1;
        }
        if (a_hdr->flags & DAP_H2_FLAG_ACK) {
            if (a_payload_len != 0) {
                log_it(L_WARNING, "validate: SETTINGS ACK must have empty payload");
                return -1;
            }
        } else if (a_payload_len % 6 != 0) {
            log_it(L_WARNING, "validate: SETTINGS payload must be multiple of 6");
            return -1;
        }
        break;
    case DAP_H2_FRAME_PUSH_PROMISE:
    case DAP_H2_FRAME_CONTINUATION:
        if (a_hdr->stream_id == 0) {
            log_it(L_WARNING, "validate: %s requires non-zero stream", dap_h2_frame_type_str(a_hdr->type));
            return -1;
        }
        break;
    case DAP_H2_FRAME_PING:
        if (a_hdr->stream_id != 0) {
            log_it(L_WARNING, "validate: PING stream_id must be 0");
            return -1;
        }
        if (a_payload_len != 8) {
            log_it(L_WARNING, "validate: PING payload must be 8 bytes");
            return -1;
        }
        break;
    case DAP_H2_FRAME_GOAWAY:
        if (a_hdr->stream_id != 0) {
            log_it(L_WARNING, "validate: GOAWAY stream_id must be 0");
            return -1;
        }
        if (a_payload_len < 8) {
            log_it(L_WARNING, "validate: GOAWAY payload must be at least 8 bytes");
            return -1;
        }
        break;
    case DAP_H2_FRAME_WINDOW_UPDATE:
        if (a_payload_len != 4) {
            log_it(L_WARNING, "validate: WINDOW_UPDATE payload must be 4 bytes");
            return -1;
        }
        break;
    default:
        break;
    }

    return 0;
}

/**
 * @brief Return a static string name for an HTTP/2 error code.
 */
const char *dap_h2_error_str(uint32_t a_error_code)
{
    switch (a_error_code) {
    case DAP_H2_NO_ERROR:
        return "NO_ERROR";
    case DAP_H2_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case DAP_H2_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    case DAP_H2_FLOW_CONTROL_ERROR:
        return "FLOW_CONTROL_ERROR";
    case DAP_H2_SETTINGS_TIMEOUT:
        return "SETTINGS_TIMEOUT";
    case DAP_H2_STREAM_CLOSED:
        return "STREAM_CLOSED";
    case DAP_H2_FRAME_SIZE_ERROR:
        return "FRAME_SIZE_ERROR";
    case DAP_H2_REFUSED_STREAM:
        return "REFUSED_STREAM";
    case DAP_H2_CANCEL:
        return "CANCEL";
    case DAP_H2_COMPRESSION_ERROR:
        return "COMPRESSION_ERROR";
    case DAP_H2_CONNECT_ERROR:
        return "CONNECT_ERROR";
    case DAP_H2_ENHANCE_YOUR_CALM:
        return "ENHANCE_YOUR_CALM";
    case DAP_H2_INADEQUATE_SECURITY:
        return "INADEQUATE_SECURITY";
    case DAP_H2_HTTP_1_1_REQUIRED:
        return "HTTP_1_1_REQUIRED";
    default:
        return "unknown error code";
    }
}

/**
 * @brief Return a static string name for an HTTP/2 frame type.
 */
const char *dap_h2_frame_type_str(uint8_t a_type)
{
    switch (a_type) {
    case DAP_H2_FRAME_DATA:
        return "DATA";
    case DAP_H2_FRAME_HEADERS:
        return "HEADERS";
    case DAP_H2_FRAME_PRIORITY:
        return "PRIORITY";
    case DAP_H2_FRAME_RST_STREAM:
        return "RST_STREAM";
    case DAP_H2_FRAME_SETTINGS:
        return "SETTINGS";
    case DAP_H2_FRAME_PUSH_PROMISE:
        return "PUSH_PROMISE";
    case DAP_H2_FRAME_PING:
        return "PING";
    case DAP_H2_FRAME_GOAWAY:
        return "GOAWAY";
    case DAP_H2_FRAME_WINDOW_UPDATE:
        return "WINDOW_UPDATE";
    case DAP_H2_FRAME_CONTINUATION:
        return "CONTINUATION";
    default:
        return "unknown frame type";
    }
}
