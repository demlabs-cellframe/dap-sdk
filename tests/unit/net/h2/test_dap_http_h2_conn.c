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
 * Unit tests for the HTTP/2 connection handler (dap_http_h2).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_http_h2.h"
#include "dap_http_h2_frame.h"
#include "dap_http_h2_hpack.h"
#include "dap_events_socket.h"

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
 * @brief Stack-allocated connection needs stream limits and flow-control defaults
 *        because dap_h2_stream_create() rejects when local_max_concurrent == 0.
 */
static void s_prep_conn_for_streams(dap_h2_connection_t *a_conn)
{
    memset(a_conn, 0, sizeof(*a_conn));
    a_conn->local_max_concurrent = DAP_H2_MAX_STREAMS;
    a_conn->peer_initial_window_size = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    a_conn->local_initial_window_size = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
}

static void test_preface_detection(void)
{
    puts("test_preface_detection");

    static const uint8_t s_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    assert(sizeof(s_preface) - 1 == 24u);
    assert(dap_h2_detect_preface(s_preface, 24));

    static const char s_http11[] = "GET / HTTP/1.1\r\n";
    assert(!dap_h2_detect_preface((const uint8_t *)s_http11, 16));

    assert(!dap_h2_detect_preface(NULL, 0));
    assert(!dap_h2_detect_preface(s_preface, 12));
    assert(!dap_h2_detect_preface(s_preface, 0));
}

static void test_stream_management(void)
{
    puts("test_stream_management");

    dap_h2_connection_t l_conn;
    s_prep_conn_for_streams(&l_conn);

    dap_h2_stream_t *l_s1 = dap_h2_stream_create(&l_conn, 1);
    assert(l_s1);
    assert(l_conn.stream_count == 1);
    assert(dap_h2_stream_find(&l_conn, 1) == l_s1);
    assert(dap_h2_stream_find(&l_conn, 99) == NULL);

    dap_h2_stream_t *l_s3 = dap_h2_stream_create(&l_conn, 3);
    dap_h2_stream_t *l_s5 = dap_h2_stream_create(&l_conn, 5);
    assert(l_s3 && l_s5);
    assert(l_conn.stream_count == 3);
    assert(dap_h2_stream_find(&l_conn, 1) == l_s1);
    assert(dap_h2_stream_find(&l_conn, 3) == l_s3);
    assert(dap_h2_stream_find(&l_conn, 5) == l_s5);

    uint32_t l_count_before = l_conn.stream_count;
    dap_h2_stream_close(&l_conn, l_s3);
    assert(l_conn.stream_count == l_count_before - 1);
    assert(dap_h2_stream_find(&l_conn, 3) == NULL);

    dap_h2_stream_close(&l_conn, l_s1);
    dap_h2_stream_close(&l_conn, l_s5);
    assert(l_conn.stream_count == 0);
    assert(l_conn.streams == NULL);
}

static void test_client_init(void)
{
    puts("test_client_init");

    dap_events_socket_t l_es;
    s_mock_esocket_init(&l_es);

    dap_h2_connection_t l_conn;
    assert(dap_h2_connection_client_init(&l_conn, &l_es) == 0);
    assert(l_conn.is_client);
    assert(l_conn.state == DAP_H2_CONN_SETTINGS);
    assert(l_conn.esocket == &l_es);
    assert(l_es.buf_out_size >= DAP_H2_CONNECTION_PREFACE_LEN);
    assert(memcmp(l_es.buf_out, DAP_H2_CONNECTION_PREFACE, DAP_H2_CONNECTION_PREFACE_LEN) == 0);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);
}

static void test_server_preface_input(void)
{
    puts("test_server_preface_input");

    dap_events_socket_t l_es;
    s_mock_esocket_init(&l_es);

    dap_h2_connection_t l_conn;
    assert(dap_h2_connection_init(&l_conn, NULL, &l_es) == 0);
    assert(l_conn.state == DAP_H2_CONN_PREFACE);

    static const uint8_t s_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    size_t l_cons = 0;

    assert(dap_h2_connection_input(&l_conn, s_preface, 12, &l_cons) == 0);
    assert(l_cons == 12);
    assert(l_conn.preface_received == 12);
    assert(l_conn.state == DAP_H2_CONN_PREFACE);

    assert(dap_h2_connection_input(&l_conn, s_preface + 12, 12, &l_cons) == 0);
    assert(l_cons == 12);
    assert(l_conn.preface_received == DAP_H2_CONNECTION_PREFACE_LEN);
    assert(l_conn.state == DAP_H2_CONN_SETTINGS);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);

    /* One-shot preface on a fresh connection */
    assert(dap_h2_connection_init(&l_conn, NULL, &l_es) == 0);
    assert(dap_h2_connection_input(&l_conn, s_preface, 24, &l_cons) == 0);
    assert(l_cons == 24);
    assert(l_conn.preface_received == DAP_H2_CONNECTION_PREFACE_LEN);
    assert(l_conn.state == DAP_H2_CONN_SETTINGS);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);
}

static void test_send_request_client(void)
{
    puts("test_send_request_client");

    dap_events_socket_t l_es;
    s_mock_esocket_init(&l_es);

    dap_h2_connection_t l_conn;
    assert(dap_h2_connection_client_init(&l_conn, &l_es) == 0);
    s_mock_esocket_reset(&l_es);
    l_conn.esocket = &l_es;

    uint32_t l_sid = dap_h2_connection_send_request(&l_conn, "GET", "/test", "localhost",
                                                     NULL, 0, NULL, 0);
    assert(l_sid == 1);
    assert(l_conn.last_local_stream_id == 1);

    dap_h2_stream_t *l_st = dap_h2_stream_find(&l_conn, 1);
    assert(l_st);
    assert(l_st->state == DAP_H2_SSTATE_HALF_CLOSED_LOCAL);

    dap_h2_connection_deinit(&l_conn);
    s_mock_esocket_reset(&l_es);
}

int main(void)
{
    test_preface_detection();
    test_stream_management();
    test_client_init();
    test_server_preface_input();
    test_send_request_client();
    puts("test_dap_http_h2_conn: all tests passed");
    return 0;
}
