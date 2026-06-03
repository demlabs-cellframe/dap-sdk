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

/**
 * @file test_dap_http_h2_hpack.c
 * @brief Unit tests for HTTP/2 HPACK and frame helpers (assert + stderr, no test framework).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_http_h2_hpack.h"
#include "dap_http_h2_hpack_huffman.h"
#include "dap_http_h2_frame.h"

#define HPACK_OK 0

enum { S_HDR_CAP = 8, S_NAME_MAX = 128, S_VAL_MAX = 512 };

typedef struct s_hdr_collect {
    char names[S_HDR_CAP][S_NAME_MAX];
    char values[S_HDR_CAP][S_VAL_MAX];
    size_t count;
} s_hdr_collect_t;

static uint8_t s_prefix_mask_u8(uint8_t a_bits)
{
    assert(a_bits >= 1 && a_bits <= 8);
    return (uint8_t)((1u << a_bits) - 1u);
}

static void s_hpack_int_roundtrip(uint64_t a_val, uint8_t a_prefix_bits)
{
    uint8_t l_buf[32];

    memset(l_buf, 0, sizeof l_buf);
    size_t l_elen = 0;
    assert(dap_hpack_int_encode(a_val, a_prefix_bits, s_prefix_mask_u8(a_prefix_bits),
                                l_buf, sizeof l_buf, &l_elen) == HPACK_OK);
    uint64_t l_dec = 0;
    size_t l_used = 0;
    assert(dap_hpack_int_decode(l_buf, l_elen, a_prefix_bits, &l_dec, &l_used) == HPACK_OK);
    assert(l_dec == a_val);
    assert(l_used == l_elen);
}

static void s_test_hpack_integer(void)
{
    fprintf(stderr, "test: HPACK integer encode/decode\n");

    s_hpack_int_roundtrip(10u, 5);
    {
        uint8_t l_buf[32];
        memset(l_buf, 0, sizeof l_buf);
        size_t l_elen = 0;
        assert(dap_hpack_int_encode(1337u, 5, s_prefix_mask_u8(5), l_buf, sizeof l_buf, &l_elen) == HPACK_OK);
        assert(l_elen > 1u);
        uint64_t l_dec = 0;
        size_t l_used = 0;
        assert(dap_hpack_int_decode(l_buf, l_elen, 5, &l_dec, &l_used) == HPACK_OK);
        assert(l_dec == 1337u);
        assert(l_used == l_elen);
    }
    {
        uint8_t l_buf[32];
        memset(l_buf, 0, sizeof l_buf);
        size_t l_elen = 0;
        assert(dap_hpack_int_encode(42u, 8, s_prefix_mask_u8(8), l_buf, sizeof l_buf, &l_elen) == HPACK_OK);
        assert(l_elen == 1u);
        assert(l_buf[0] == 42);
        uint64_t l_dec = 0;
        size_t l_used = 0;
        assert(dap_hpack_int_decode(l_buf, l_elen, 8, &l_dec, &l_used) == HPACK_OK);
        assert(l_dec == 42u);
    }
    s_hpack_int_roundtrip(0u, 5);
    s_hpack_int_roundtrip(0x7FFFFFFFu, 5);

    fprintf(stderr, "PASSED (HPACK integer)\n");
}

static void s_huff_roundtrip(const char *a_plain)
{
    const uint8_t *l_in = (const uint8_t *)a_plain;
    size_t l_in_len = strlen(a_plain);
    size_t l_pred = dap_hpack_huffman_encoded_len(l_in, l_in_len);
    uint8_t l_enc[512];
    size_t l_enc_len = 0;
    assert(dap_hpack_huffman_encode(l_in, l_in_len, l_enc, sizeof l_enc, &l_enc_len) == 0);
    assert(l_enc_len == l_pred);
    uint8_t l_dec[512];
    size_t l_dec_len = 0;
    assert(dap_hpack_huffman_decode(l_enc, l_enc_len, l_dec, sizeof l_dec, &l_dec_len) == 0);
    assert(l_dec_len == l_in_len);
    assert(memcmp(l_dec, l_in, l_in_len) == 0);
}

static void s_test_huffman(void)
{
    fprintf(stderr, "test: Huffman encode/decode\n");

    {
        const char *l_host = "www.example.com";
        size_t l_plain = strlen(l_host);
        size_t l_enc_sz = dap_hpack_huffman_encoded_len((const uint8_t *)l_host, l_plain);
        assert(l_enc_sz < l_plain);
        s_huff_roundtrip(l_host);
    }
    {
        size_t l_pred = dap_hpack_huffman_encoded_len((const uint8_t *)"", 0);
        assert(l_pred == 0);
        uint8_t l_dummy[4];
        size_t l_out = 0;
        assert(dap_hpack_huffman_encode((const uint8_t *)"", 0, l_dummy, sizeof l_dummy, &l_out) == 0);
        assert(l_out == 0);
        size_t l_dec_len = 0;
        assert(dap_hpack_huffman_decode(NULL, 0, l_dummy, sizeof l_dummy, &l_dec_len) == 0);
        assert(l_dec_len == 0);
    }
    s_huff_roundtrip("no-cache");

    fprintf(stderr, "PASSED (Huffman)\n");
}

static void s_test_static_table(void)
{
    fprintf(stderr, "test: HPACK static table lookup\n");

    size_t l_idx = 0;
    assert(dap_hpack_static_find(":method", 7, "GET", 3, &l_idx) != NULL);
    assert(l_idx == 2u);
    assert(dap_hpack_static_find(":method", 7, "POST", 4, &l_idx) != NULL);
    assert(l_idx == 3u);
    assert(dap_hpack_static_find(":path", 5, "/", 1, &l_idx) != NULL);
    assert(l_idx == 4u);
    assert(dap_hpack_static_find(":status", 7, "200", 3, &l_idx) != NULL);
    assert(l_idx == 8u);
    assert(dap_hpack_static_find("x-custom", 8, "val", 3, &l_idx) == NULL);

    fprintf(stderr, "PASSED (HPACK static table)\n");
}

static void s_test_hpack_context_lifecycle(void)
{
    fprintf(stderr, "test: HPACK context init/deinit\n");

    dap_hpack_context_t l_ctx;
    assert(dap_hpack_context_init(&l_ctx, DAP_HPACK_DEFAULT_TABLE_SIZE) == HPACK_OK);
    dap_hpack_context_deinit(&l_ctx);

    fprintf(stderr, "PASSED (HPACK context lifecycle)\n");
}

static void s_hdr_collect_cb(const char *a_name, size_t a_name_len, const char *a_value, size_t a_value_len,
                             void *a_userdata)
{
    s_hdr_collect_t *l_c = (s_hdr_collect_t *)a_userdata;

    assert(l_c->count < S_HDR_CAP);
    assert(a_name_len < S_NAME_MAX && a_value_len < S_VAL_MAX);
    memcpy(l_c->names[l_c->count], a_name, a_name_len);
    l_c->names[l_c->count][a_name_len] = '\0';
    memcpy(l_c->values[l_c->count], a_value, a_value_len);
    l_c->values[l_c->count][a_value_len] = '\0';
    l_c->count++;
}

static void s_test_hpack_roundtrip_headers(void)
{
    fprintf(stderr, "test: HPACK full encode/decode round-trip\n");

    dap_hpack_context_t l_enc;
    dap_hpack_context_t l_dec;

    assert(dap_hpack_context_init(&l_enc, DAP_HPACK_DEFAULT_TABLE_SIZE) == HPACK_OK);
    assert(dap_hpack_context_init(&l_dec, DAP_HPACK_DEFAULT_TABLE_SIZE) == HPACK_OK);

    dap_hpack_header_t l_hdrs[3];
    l_hdrs[0] = (dap_hpack_header_t){ ":method", "GET", 7, 3 };
    l_hdrs[1] = (dap_hpack_header_t){ ":path", "/", 5, 1 };
    l_hdrs[2] = (dap_hpack_header_t){ "content-type", "text/html", 12, 9 };

    uint8_t l_block[512];
    size_t l_blen = 0;
    assert(dap_hpack_encode(&l_enc, l_hdrs, 3, l_block, sizeof l_block, &l_blen) == HPACK_OK);
    assert(l_blen > 0);

    s_hdr_collect_t l_col;
    memset(&l_col, 0, sizeof l_col);
    assert(dap_hpack_decode(&l_dec, l_block, l_blen, s_hdr_collect_cb, &l_col) == HPACK_OK);
    assert(l_col.count == 3u);
    assert(strcmp(l_col.names[0], ":method") == 0 && strcmp(l_col.values[0], "GET") == 0);
    assert(strcmp(l_col.names[1], ":path") == 0 && strcmp(l_col.values[1], "/") == 0);
    assert(strcmp(l_col.names[2], "content-type") == 0 && strcmp(l_col.values[2], "text/html") == 0);

    dap_hpack_context_deinit(&l_dec);
    dap_hpack_context_deinit(&l_enc);

    fprintf(stderr, "PASSED (HPACK round-trip)\n");
}

static uint32_t s_read_be32(const uint8_t *a_p)
{
    return ((uint32_t)a_p[0] << 24) | ((uint32_t)a_p[1] << 16) | ((uint32_t)a_p[2] << 8) | (uint32_t)a_p[3];
}

static void s_test_frame_header_parse_serialize(void)
{
    fprintf(stderr, "test: HTTP/2 frame header parse/serialize\n");

    {
        dap_h2_frame_header_t l_in = { .length = 123u, .type = DAP_H2_FRAME_HEADERS,
                                         .flags = DAP_H2_FLAG_END_HEADERS, .stream_id = 1u };
        uint8_t l_wire[16];
        assert(dap_h2_frame_header_serialize(&l_in, l_wire, sizeof l_wire) == (int)DAP_H2_FRAME_HEADER_SIZE);
        dap_h2_frame_header_t l_out;
        assert(dap_h2_frame_header_parse(l_wire, DAP_H2_FRAME_HEADER_SIZE, &l_out) == 0);
        assert(l_out.length == 123u);
        assert(l_out.type == DAP_H2_FRAME_HEADERS);
        assert(l_out.flags == DAP_H2_FLAG_END_HEADERS);
        assert(l_out.stream_id == 1u);
    }
    {
        dap_h2_frame_header_t l_in = { .length = 100u, .type = DAP_H2_FRAME_DATA,
                                         .flags = DAP_H2_FLAG_END_STREAM, .stream_id = 9u };
        uint8_t l_wire[16];
        assert(dap_h2_frame_header_serialize(&l_in, l_wire, sizeof l_wire) == (int)DAP_H2_FRAME_HEADER_SIZE);
        dap_h2_frame_header_t l_out;
        assert(dap_h2_frame_header_parse(l_wire, DAP_H2_FRAME_HEADER_SIZE, &l_out) == 0);
        assert(l_out.type == DAP_H2_FRAME_DATA);
        assert(l_out.flags == DAP_H2_FLAG_END_STREAM);
        assert(l_out.stream_id == 9u);
        assert(l_out.length == 100u);
    }
    {
        uint8_t l_ack[32];
        size_t l_n = dap_h2_frame_settings_ack(l_ack, sizeof l_ack);
        assert(l_n == (size_t)DAP_H2_FRAME_HEADER_SIZE);
        dap_h2_frame_header_t l_h;
        assert(dap_h2_frame_header_parse(l_ack, DAP_H2_FRAME_HEADER_SIZE, &l_h) == 0);
        assert(l_h.length == 0u);
        assert(l_h.type == DAP_H2_FRAME_SETTINGS);
        assert(l_h.flags == DAP_H2_FLAG_ACK);
        assert(l_h.stream_id == 0u);
    }

    fprintf(stderr, "PASSED (frame header)\n");
}

static void s_test_frame_builders(void)
{
    fprintf(stderr, "test: HTTP/2 frame builders\n");

    {
        uint8_t l_buf[32];
        size_t l_n = dap_h2_frame_settings_ack(l_buf, sizeof l_buf);
        assert(l_n == 9u);
        assert(l_buf[3] == DAP_H2_FRAME_SETTINGS);
        assert(l_buf[4] == DAP_H2_FLAG_ACK);
    }
    {
        uint8_t l_buf[32];
        const uint8_t l_op[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        size_t l_n = dap_h2_frame_ping(l_buf, sizeof l_buf, l_op, false);
        assert(l_n == 17u);
        assert(memcmp(l_buf + 9, l_op, 8) == 0);
        dap_h2_frame_header_t l_h;
        assert(dap_h2_frame_header_parse(l_buf, DAP_H2_FRAME_HEADER_SIZE, &l_h) == 0);
        assert(l_h.type == DAP_H2_FRAME_PING);
        assert(l_h.length == 8u);
    }
    {
        uint8_t l_buf[32];
        size_t l_n = dap_h2_frame_window_update(l_buf, sizeof l_buf, 11u, 12345u);
        assert(l_n == 13u);
        dap_h2_frame_header_t l_h;
        assert(dap_h2_frame_header_parse(l_buf, DAP_H2_FRAME_HEADER_SIZE, &l_h) == 0);
        assert(l_h.type == DAP_H2_FRAME_WINDOW_UPDATE);
        assert(l_h.stream_id == 11u);
        assert(l_h.length == 4u);
        assert(s_read_be32(l_buf + 9) == 12345u);
    }
    {
        uint8_t l_buf[64];
        const uint8_t l_dbg[] = { 'b', 'y', 'e' };
        size_t l_n = dap_h2_frame_goaway(l_buf, sizeof l_buf, 42u, DAP_H2_PROTOCOL_ERROR, l_dbg, sizeof l_dbg);
        assert(l_n == 9u + 8u + sizeof l_dbg);
        dap_h2_frame_header_t l_h;
        assert(dap_h2_frame_header_parse(l_buf, DAP_H2_FRAME_HEADER_SIZE, &l_h) == 0);
        assert(l_h.type == DAP_H2_FRAME_GOAWAY);
        assert(l_h.stream_id == 0u);
        assert(l_h.length == 8u + (uint32_t)sizeof l_dbg);
        assert(s_read_be32(l_buf + 9) == 42u);
        assert(s_read_be32(l_buf + 13) == DAP_H2_PROTOCOL_ERROR);
        assert(memcmp(l_buf + 17, l_dbg, sizeof l_dbg) == 0);
    }

    fprintf(stderr, "PASSED (frame builders)\n");
}

static void s_test_error_and_type_strings(void)
{
    fprintf(stderr, "test: HTTP/2 error/type strings\n");

    assert(strcmp(dap_h2_error_str(DAP_H2_NO_ERROR), "NO_ERROR") == 0);
    assert(strcmp(dap_h2_frame_type_str(DAP_H2_FRAME_HEADERS), "HEADERS") == 0);
    assert(dap_h2_error_str(0xFFFFFFFFu) != NULL);
    assert(strlen(dap_h2_error_str(0xFFFFFFFFu)) > 0);

    fprintf(stderr, "PASSED (error/type strings)\n");
}

int main(void)
{
    assert(dap_common_init("test_dap_http_h2_hpack", NULL) == 0);

    s_test_hpack_integer();
    s_test_huffman();
    s_test_static_table();
    s_test_hpack_context_lifecycle();
    s_test_hpack_roundtrip_headers();
    s_test_frame_header_parse_serialize();
    s_test_frame_builders();
    s_test_error_and_type_strings();

    dap_common_deinit();
    return 0;
}
