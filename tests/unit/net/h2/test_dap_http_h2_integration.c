/*
 * Authors:
 * DeM Labs Ltd. https://demlabs.net
 * Copyright (c) 2025
 *
 * This file is part of DAP the open source project.
 *
 * DAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Integration-style tests for HTTP/2: hand-built frames fed through dap_h2_connection_input().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_http_h2.h"
#include "dap_http_h2_frame.h"
#include "dap_http_h2_hpack.h"

static void s_mock_esocket_init(dap_events_socket_t *a_es)
{
    memset(a_es, 0, sizeof(*a_es));
    a_es->fd = -1;
    a_es->socket = (SOCKET)-1;
}

static void s_mock_esocket_reset(dap_events_socket_t *a_es)
{
    DAP_DELETE(a_es->buf_out);
    a_es->buf_out = NULL;
    a_es->buf_out_size = 0;
    a_es->buf_out_size_max = 0;
}

/**
 * @brief Append client→server handshake bytes: empty SETTINGS + SETTINGS ACK.
 */
static size_t s_build_client_handshake_frames(uint8_t *a_buf, size_t a_cap)
{
    size_t l_off = 0;
    size_t l_n = dap_h2_frame_settings(a_buf + l_off, a_cap - l_off, NULL, 0);
    assert(l_n == DAP_H2_FRAME_HEADER_SIZE);
    l_off += l_n;
    l_n = dap_h2_frame_settings_ack(a_buf + l_off, a_cap - l_off);
    assert(l_n == DAP_H2_FRAME_HEADER_SIZE);
    l_off += l_n;
    return l_off;
}

static void test_client_server_frame_exchange(void)
{
    puts("test_client_server_frame_exchange");

    static const uint8_t s_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

    dap_events_socket_t l_es;
    s_mock_esocket_init(&l_es);

    dap_h2_connection_t l_conn;
    assert(dap_h2_connection_init(&l_conn, NULL, &l_es) == 0);

    uint8_t l_pkt[256];
    size_t l_pref_len = sizeof(s_preface) - 1;
    memcpy(l_pkt, s_preface, l_pref_len);
    size_t l_handshake_len = s_build_client_handshake_frames(l_pkt + l_pref_len, sizeof(l_pkt) - l_pref_len);
    size_t l_total = l_pref_len + l_handshake_len;
    assert(l_total <= sizeof(l_pkt));

    size_t l_cons = 0;
    assert(dap_h2_connection_input(&l_conn, l_pkt, l_total, &l_cons) == 0);
    assert(l_cons == l_total);
    assert(l_conn.state == DAP_H2_CONN_ACTIVE);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);
}

static void test_headers_frame_decode(void)
{
    puts("test_headers_frame_decode");

    static const uint8_t s_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

    dap_events_socket_t l_es;
    s_mock_esocket_init(&l_es);

    dap_h2_connection_t l_conn;
    assert(dap_h2_connection_init(&l_conn, NULL, &l_es) == 0);

    dap_hpack_context_t l_enc;
    assert(dap_hpack_context_init(&l_enc, DAP_H2_DEFAULT_HEADER_TABLE_SIZE) == 0);

    dap_hpack_header_t l_hdrs[2];
    l_hdrs[0].name = (char *)":method";
    l_hdrs[0].name_len = 7;
    l_hdrs[0].value = (char *)"GET";
    l_hdrs[0].value_len = 3;
    l_hdrs[1].name = (char *)":path";
    l_hdrs[1].name_len = 5;
    l_hdrs[1].value = (char *)"/hello";
    l_hdrs[1].value_len = 6;

    uint8_t l_hpack[256];
    size_t l_hpack_len = 0;
    assert(dap_hpack_encode(&l_enc, l_hdrs, 2, l_hpack, sizeof(l_hpack), &l_hpack_len) == 0);
    dap_hpack_context_deinit(&l_enc);

    uint8_t l_headers_frm[DAP_H2_FRAME_HEADER_SIZE + sizeof(l_hpack)];
    size_t l_hf = dap_h2_frame_headers(l_headers_frm, sizeof(l_headers_frm), 1u,
                                       (uint8_t)(DAP_H2_FLAG_END_HEADERS | DAP_H2_FLAG_END_STREAM),
                                       l_hpack, l_hpack_len);
    assert(l_hf > 0);

    uint8_t l_pkt[512];
    size_t l_pos = 0;
    memcpy(l_pkt + l_pos, s_preface, sizeof(s_preface) - 1);
    l_pos += sizeof(s_preface) - 1;
    l_pos += s_build_client_handshake_frames(l_pkt + l_pos, sizeof(l_pkt) - l_pos);
    assert(l_pos + l_hf <= sizeof(l_pkt));
    memcpy(l_pkt + l_pos, l_headers_frm, l_hf);
    l_pos += l_hf;

    size_t l_cons = 0;
    assert(dap_h2_connection_input(&l_conn, l_pkt, l_pos, &l_cons) == 0);
    assert(l_cons == l_pos);
    assert(l_conn.state == DAP_H2_CONN_ACTIVE);

    dap_h2_stream_t *l_st = dap_h2_stream_find(&l_conn, 1u);
    assert(l_st);
    assert(strcmp(l_st->method, "GET") == 0);
    assert(strcmp(l_st->path, "/hello") == 0);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);
}

int main(void)
{
    test_client_server_frame_exchange();
    test_headers_frame_decode();
    puts("test_dap_http_h2_integration: all tests passed");
    return 0;
}
