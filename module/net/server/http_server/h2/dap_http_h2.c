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
 * HTTP/2 connection handler (RFC 7540).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_http_h2.h"
#include "dap_http_h2_frame.h"
#include "dap_http_h2_hpack.h"
#include "dap_http_header.h"
#include "dap_http_server.h"
#include "dap_http_client.h"
#include "dap_http_simple.h"
#include "dap_ht.h"

#define LOG_TAG "dap_http_h2"

/**
 * @brief s_dispatch_h2_request  Dispatch a fully received HTTP/2 request through
 *        the standard dap_http URL-processor pipeline. Mirrors the URL lookup
 *        logic from dap_http_client_read (z_dirname / z_basename) so that the
 *        same URL proc table works for both HTTP/1.1 and HTTP/2.
 */
static void s_dispatch_h2_request(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream)
{
    if (!a_conn || !a_stream || !a_conn->http)
        return;

    dap_http_server_t *l_http = a_conn->http;

    /* Work on a mutable copy of the path (may contain query string) */
    char l_full_path[1024];
    strncpy(l_full_path, a_stream->path, sizeof(l_full_path) - 1);
    l_full_path[sizeof(l_full_path) - 1] = '\0';

    /* Split off query string: "/enc_init/abc?foo=bar" → path "/enc_init/abc", qs "foo=bar" */
    char *l_qs = "";
    char *l_qmark = strchr(l_full_path, '?');
    if (l_qmark) {
        *l_qmark = '\0';
        l_qs = l_qmark + 1;
    }

    size_t l_path_len = strlen(l_full_path);

    /* z_dirname: null-terminate at last '/', keeping only dirname */
    int32_t l_dirname_ret = 0;
    if (l_path_len >= 2) {
        char *l_ptr = l_full_path + l_path_len - 1;
        while (l_ptr > l_full_path) {
            if (*l_ptr == '/')
                break;
            --l_ptr;
        }
        l_dirname_ret = (int32_t)(l_ptr - l_full_path);
        if (l_dirname_ret)
            l_full_path[l_dirname_ret] = '\0';
    }

    /* Lookup URL processor by dirname key */
    dap_http_url_proc_t *l_url_proc = NULL;
    dap_ht_find_str(l_http->url_proc, l_full_path, l_url_proc);

    /* Restore the path */
    if (l_dirname_ret)
        l_full_path[l_dirname_ret] = '/';

    /* z_basename: extract tail after last '/' */
    char *l_basename = l_full_path + l_path_len - 1;
    while (l_basename > l_full_path) {
        if (*(l_basename - 1) == '/') break;
        --l_basename;
    }

    if (!l_url_proc) {
        log_it(L_WARNING, "H2 stream %u: no URL processor for '%s'", a_stream->id, a_stream->path);
        dap_h2_connection_send_response(a_conn, a_stream->id, 404, NULL, 0,
                                        (const uint8_t *)"Not Found", 9);
        return;
    }

    /* Build a transient dap_http_client_t for the URL proc callbacks */
    dap_http_client_t *l_cl = DAP_NEW_Z(dap_http_client_t);
    if (!l_cl)
        return;

    l_cl->esocket    = a_conn->esocket;
    l_cl->socket_num = a_conn->esocket ? a_conn->esocket->socket : -1;
    l_cl->http       = l_http;
    l_cl->proc       = l_url_proc;
    l_cl->_internal  = a_stream;
    l_cl->h2         = a_conn;

    /* Method */
    strncpy(l_cl->action, a_stream->method, sizeof(l_cl->action) - 1);
    l_cl->action_len = strlen(l_cl->action);

    /* URL path (basename only, matching HTTP/1.1 convention) */
    strncpy(l_cl->url_path, l_basename, sizeof(l_cl->url_path) - 1);
    l_cl->url_path_len = strlen(l_cl->url_path);

    /* Query string */
    strncpy(l_cl->in_query_string, l_qs, sizeof(l_cl->in_query_string) - 1);
    l_cl->in_query_string_len = strlen(l_cl->in_query_string);

    /* Headers from h2 stream → linked list */
    l_cl->in_headers = dap_http_headers_dup(a_stream->headers);
    l_cl->in_content_length = a_stream->body_len;

    /* Content-Type from stream headers */
    dap_http_header_t *l_ct = dap_http_header_find(l_cl->in_headers, "Content-Type");
    if (l_ct)
        strncpy(l_cl->in_content_type, l_ct->value, sizeof(l_cl->in_content_type) - 1);

    log_it(L_INFO, "H2 dispatch: stream %u, %s %s (qs '%s')",
           a_stream->id, l_cl->action, l_cl->url_path, l_cl->in_query_string);

    /* --- Invoke URL proc lifecycle callbacks --- */
    if (l_url_proc->new_callback)
        l_url_proc->new_callback(l_cl, NULL);

    if (l_url_proc->headers_read_callback)
        l_url_proc->headers_read_callback(l_cl, NULL);

    if (a_stream->body && a_stream->body_len > 0 && l_url_proc->data_read_callback) {
        dap_events_socket_t *l_es = a_conn->esocket;
        byte_t  *l_save_buf  = l_es->buf_in;
        size_t   l_save_size = l_es->buf_in_size;

        l_es->buf_in      = a_stream->body;
        l_es->buf_in_size = a_stream->body_len;

        l_url_proc->data_read_callback(l_cl, NULL);

        l_es->buf_in      = l_save_buf;
        l_es->buf_in_size = l_save_size;
    }

    if (l_url_proc->headers_write_callback)
        l_url_proc->headers_write_callback(l_cl, NULL);

    if (!l_cl->out_headers)
        dap_http_client_out_header_generate(l_cl);

    /* Collect response headers for h2 */
    size_t l_hdr_count = 0;
    for (dap_http_header_t *l_h = l_cl->out_headers; l_h; l_h = l_h->next)
        l_hdr_count++;

    dap_hpack_header_t *l_resp_hdrs = NULL;
    if (l_hdr_count > 0) {
        l_resp_hdrs = DAP_NEW_Z_COUNT(dap_hpack_header_t, l_hdr_count);
        size_t l_idx = 0;
        for (dap_http_header_t *l_h = l_cl->out_headers; l_h; l_h = l_h->next, l_idx++) {
            l_resp_hdrs[l_idx].name      = l_h->name;
            l_resp_hdrs[l_idx].name_len  = l_h->namesz;
            l_resp_hdrs[l_idx].value     = l_h->value;
            l_resp_hdrs[l_idx].value_len = l_h->valuesz;
        }
    }

    uint16_t l_status = l_cl->reply_status_code ? l_cl->reply_status_code : 200;

    dap_h2_connection_send_response(a_conn, a_stream->id, l_status,
                                    l_resp_hdrs, l_hdr_count, NULL, 0);

    DAP_DELETE(l_resp_hdrs);

    if (l_url_proc->delete_callback)
        l_url_proc->delete_callback(l_cl, NULL);

    /* Cleanup transient http_client */
    while (l_cl->in_headers)
        dap_http_header_remove(&l_cl->in_headers, l_cl->in_headers);
    while (l_cl->out_headers)
        dap_http_header_remove(&l_cl->out_headers, l_cl->out_headers);
    DAP_DELETE(l_cl);
}

/**
 * @brief s_stream_complete  Called when END_STREAM is received on a stream.
 *        On server: dispatches the request to URL proc pipeline.
 *        On client: invokes the response callback with status + body.
 */
static void s_stream_complete(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream)
{
    if (!a_conn || !a_stream)
        return;

    if (a_conn->is_client) {
        /* Client mode: peer sent response, invoke callback */
        if (a_conn->response_cb)
            a_conn->response_cb(a_stream, a_stream->status, a_conn->response_cb_arg);
    } else {
        /* Server mode: peer sent request, dispatch to URL procs */
        s_dispatch_h2_request(a_conn, a_stream);
    }
}

/* --- Stream management -------------------------------------------------- */

dap_h2_stream_t *dap_h2_stream_find(dap_h2_connection_t *a_conn, uint32_t a_stream_id)
{
    if (!a_conn)
        return NULL;
    for (dap_h2_stream_t *l_st = a_conn->streams; l_st; l_st = l_st->next)
        if (l_st->id == a_stream_id)
            return l_st;
    return NULL;
}

dap_h2_stream_t *dap_h2_stream_create(dap_h2_connection_t *a_conn, uint32_t a_stream_id)
{
    if (!a_conn || a_conn->stream_count >= a_conn->local_max_concurrent)
        return NULL;
    dap_h2_stream_t *l_stream = DAP_NEW_Z(dap_h2_stream_t);
    if (!l_stream)
        return NULL;
    l_stream->id = a_stream_id;
    l_stream->state = DAP_H2_SSTATE_OPEN;
    l_stream->send_window = (int32_t)a_conn->peer_initial_window_size;
    l_stream->recv_window = (int32_t)a_conn->local_initial_window_size;
    l_stream->header_buf = DAP_NEW_Z_SIZE(uint8_t, DAP_H2_HEADER_BUF_SIZE);
    l_stream->header_buf_len = 0;
    l_stream->headers_complete = false;

    l_stream->next = a_conn->streams;
    if (a_conn->streams)
        a_conn->streams->prev = l_stream;
    a_conn->streams = l_stream;
    a_conn->stream_count++;

    if (a_stream_id > a_conn->last_peer_stream_id)
        a_conn->last_peer_stream_id = a_stream_id;

    return l_stream;
}

void dap_h2_stream_close(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream)
{
    if (!a_conn || !a_stream)
        return;
    a_stream->state = DAP_H2_SSTATE_CLOSED;

    if (a_stream->prev)
        a_stream->prev->next = a_stream->next;
    if (a_stream->next)
        a_stream->next->prev = a_stream->prev;
    if (a_conn->streams == a_stream)
        a_conn->streams = a_stream->next;
    a_conn->stream_count--;

    while (a_stream->headers)
        dap_http_header_remove(&a_stream->headers, a_stream->headers);
    DAP_DELETE(a_stream->body);
    DAP_DELETE(a_stream->header_buf);
    DAP_DELETE(a_stream);
}

/* --- Connection init/deinit --------------------------------------------- */

int dap_h2_connection_init(dap_h2_connection_t *a_conn, struct dap_http_server *a_http,
                           struct dap_events_socket *a_esocket)
{
    if (!a_conn)
        return -1;
    memset(a_conn, 0, sizeof(*a_conn));
    a_conn->state = DAP_H2_CONN_PREFACE;
    a_conn->http = a_http;
    a_conn->esocket = a_esocket;

    a_conn->peer_header_table_size  = DAP_H2_DEFAULT_HEADER_TABLE_SIZE;
    a_conn->peer_enable_push        = DAP_H2_DEFAULT_ENABLE_PUSH;
    a_conn->peer_max_concurrent     = DAP_H2_DEFAULT_MAX_CONCURRENT;
    a_conn->peer_initial_window_size = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    a_conn->peer_max_frame_size     = DAP_H2_DEFAULT_MAX_FRAME_SIZE;
    a_conn->peer_max_header_list_size = UINT32_MAX;

    a_conn->local_max_frame_size     = DAP_H2_DEFAULT_MAX_FRAME_SIZE;
    a_conn->local_initial_window_size = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    a_conn->local_max_concurrent     = DAP_H2_MAX_STREAMS;

    a_conn->send_window = DAP_H2_DEFAULT_WINDOW_SIZE;
    a_conn->recv_window = DAP_H2_DEFAULT_WINDOW_SIZE;

    if (dap_hpack_context_init(&a_conn->hpack_dec, a_conn->peer_header_table_size) != 0)
        return -1;
    if (dap_hpack_context_init(&a_conn->hpack_enc, DAP_H2_DEFAULT_HEADER_TABLE_SIZE) != 0) {
        dap_hpack_context_deinit(&a_conn->hpack_dec);
        return -1;
    }

    return 0;
}

void dap_h2_connection_deinit(dap_h2_connection_t *a_conn)
{
    if (!a_conn)
        return;
    while (a_conn->streams)
        dap_h2_stream_close(a_conn, a_conn->streams);
    dap_hpack_context_deinit(&a_conn->hpack_dec);
    dap_hpack_context_deinit(&a_conn->hpack_enc);
}

/* --- Preface detection -------------------------------------------------- */

bool dap_h2_detect_preface(const uint8_t *a_data, size_t a_len)
{
    if (a_len < DAP_H2_CONNECTION_PREFACE_LEN)
        return false;
    return memcmp(a_data, DAP_H2_CONNECTION_PREFACE, DAP_H2_CONNECTION_PREFACE_LEN) == 0;
}

/* --- Output helpers ----------------------------------------------------- */

static int s_conn_write(dap_h2_connection_t *a_conn, const uint8_t *a_data, size_t a_len)
{
    if (!a_conn || !a_conn->esocket || !a_len)
        return -1;
    dap_events_socket_write_unsafe(a_conn->esocket, (const void *)a_data, a_len);
    return 0;
}

int dap_h2_connection_send_settings(dap_h2_connection_t *a_conn)
{
    dap_h2_settings_entry_t l_entries[] = {
        { DAP_H2_SETTINGS_MAX_CONCURRENT_STREAMS, a_conn->local_max_concurrent },
        { DAP_H2_SETTINGS_INITIAL_WINDOW_SIZE,    a_conn->local_initial_window_size },
        { DAP_H2_SETTINGS_MAX_FRAME_SIZE,         a_conn->local_max_frame_size },
    };
    uint8_t l_buf[9 + 6 * 3];
    size_t l_n = dap_h2_frame_settings(l_buf, sizeof(l_buf), l_entries, 3);
    if (!l_n)
        return -1;
    return s_conn_write(a_conn, l_buf, l_n);
}

int dap_h2_connection_send_goaway(dap_h2_connection_t *a_conn, uint32_t a_error_code)
{
    uint8_t l_buf[9 + 8];
    size_t l_n = dap_h2_frame_goaway(l_buf, sizeof(l_buf), a_conn->last_peer_stream_id, a_error_code, NULL, 0);
    if (!l_n)
        return -1;
    a_conn->state = DAP_H2_CONN_GOAWAY;
    return s_conn_write(a_conn, l_buf, l_n);
}

static int s_send_settings_ack(dap_h2_connection_t *a_conn)
{
    uint8_t l_buf[9];
    size_t l_n = dap_h2_frame_settings_ack(l_buf, sizeof(l_buf));
    if (!l_n)
        return -1;
    return s_conn_write(a_conn, l_buf, l_n);
}

static int s_send_window_update(dap_h2_connection_t *a_conn, uint32_t a_stream_id, uint32_t a_increment)
{
    uint8_t l_buf[9 + 4];
    size_t l_n = dap_h2_frame_window_update(l_buf, sizeof(l_buf), a_stream_id, a_increment);
    if (!l_n)
        return -1;
    return s_conn_write(a_conn, l_buf, l_n);
}

static int s_send_rst_stream(dap_h2_connection_t *a_conn, uint32_t a_stream_id, uint32_t a_error_code)
{
    uint8_t l_buf[9 + 4];
    size_t l_n = dap_h2_frame_rst_stream(l_buf, sizeof(l_buf), a_stream_id, a_error_code);
    if (!l_n)
        return -1;
    return s_conn_write(a_conn, l_buf, l_n);
}

/* --- HPACK header callback ---------------------------------------------- */

typedef struct {
    dap_h2_stream_t *stream;
} s_hpack_cb_ctx_t;

static void s_hpack_header_cb(const char *a_name, size_t a_name_len,
                               const char *a_value, size_t a_value_len,
                               void *a_userdata)
{
    s_hpack_cb_ctx_t *l_ctx = (s_hpack_cb_ctx_t *)a_userdata;
    dap_h2_stream_t *l_stream = l_ctx->stream;

    if (a_name_len > 0 && a_name[0] == ':') {
        if (a_name_len == 7 && memcmp(a_name, ":method", 7) == 0) {
            size_t l_cl = a_value_len < sizeof(l_stream->method) - 1 ? a_value_len : sizeof(l_stream->method) - 1;
            memcpy(l_stream->method, a_value, l_cl);
            l_stream->method[l_cl] = '\0';
        } else if (a_name_len == 5 && memcmp(a_name, ":path", 5) == 0) {
            size_t l_cl = a_value_len < sizeof(l_stream->path) - 1 ? a_value_len : sizeof(l_stream->path) - 1;
            memcpy(l_stream->path, a_value, l_cl);
            l_stream->path[l_cl] = '\0';
        } else if (a_name_len == 7 && memcmp(a_name, ":scheme", 7) == 0) {
            size_t l_cl = a_value_len < sizeof(l_stream->scheme) - 1 ? a_value_len : sizeof(l_stream->scheme) - 1;
            memcpy(l_stream->scheme, a_value, l_cl);
            l_stream->scheme[l_cl] = '\0';
        } else if (a_name_len == 10 && memcmp(a_name, ":authority", 10) == 0) {
            size_t l_cl = a_value_len < sizeof(l_stream->authority) - 1 ? a_value_len : sizeof(l_stream->authority) - 1;
            memcpy(l_stream->authority, a_value, l_cl);
            l_stream->authority[l_cl] = '\0';
        } else if (a_name_len == 7 && memcmp(a_name, ":status", 7) == 0) {
            l_stream->status = (uint16_t)atoi(a_value);
        }
        return;
    }

    char l_name_buf[DAP_HTTP$SZ_FIELD_NAME];
    char l_val_buf[DAP_HTTP$SZ_FIELD_VALUE];
    size_t l_nl = a_name_len < sizeof(l_name_buf) - 1 ? a_name_len : sizeof(l_name_buf) - 1;
    size_t l_vl = a_value_len < sizeof(l_val_buf) - 1 ? a_value_len : sizeof(l_val_buf) - 1;
    memcpy(l_name_buf, a_name, l_nl);
    l_name_buf[l_nl] = '\0';
    memcpy(l_val_buf, a_value, l_vl);
    l_val_buf[l_vl] = '\0';

    dap_http_header_add(&l_stream->headers, l_name_buf, l_val_buf);
}

/* --- Frame processing --------------------------------------------------- */

static int s_apply_settings(dap_h2_connection_t *a_conn, const uint8_t *a_payload, size_t a_len)
{
    for (size_t l_i = 0; l_i + 5 < a_len; l_i += 6) {
        uint16_t l_id = (uint16_t)((a_payload[l_i] << 8) | a_payload[l_i + 1]);
        uint32_t l_val = ((uint32_t)a_payload[l_i + 2] << 24) | ((uint32_t)a_payload[l_i + 3] << 16) |
                       ((uint32_t)a_payload[l_i + 4] << 8) | (uint32_t)a_payload[l_i + 5];
        switch (l_id) {
            case DAP_H2_SETTINGS_HEADER_TABLE_SIZE:
                a_conn->peer_header_table_size = l_val;
                dap_hpack_context_resize(&a_conn->hpack_dec, l_val);
                break;
            case DAP_H2_SETTINGS_ENABLE_PUSH:
                if (l_val > 1) {
                    dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
                    return -1;
                }
                a_conn->peer_enable_push = l_val;
                break;
            case DAP_H2_SETTINGS_MAX_CONCURRENT_STREAMS:
                a_conn->peer_max_concurrent = l_val;
                break;
            case DAP_H2_SETTINGS_INITIAL_WINDOW_SIZE:
                if (l_val > 0x7fffffffu) {
                    dap_h2_connection_send_goaway(a_conn, DAP_H2_FLOW_CONTROL_ERROR);
                    return -1;
                }
                a_conn->peer_initial_window_size = l_val;
                break;
            case DAP_H2_SETTINGS_MAX_FRAME_SIZE:
                if (l_val < DAP_H2_DEFAULT_MAX_FRAME_SIZE || l_val > DAP_H2_MAX_FRAME_SIZE_LIMIT) {
                    dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
                    return -1;
                }
                a_conn->peer_max_frame_size = l_val;
                break;
            case DAP_H2_SETTINGS_MAX_HEADER_LIST_SIZE:
                a_conn->peer_max_header_list_size = l_val;
                break;
            default:
                break;
        }
    }
    return 0;
}

static int s_process_headers_block(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream)
{
    s_hpack_cb_ctx_t l_ctx = { .stream = a_stream };
    int l_rc = dap_hpack_decode(&a_conn->hpack_dec, a_stream->header_buf, a_stream->header_buf_len,
                              s_hpack_header_cb, &l_ctx);
    a_stream->header_buf_len = 0;
    a_stream->headers_complete = true;
    if (l_rc != 0) {
        log_it(L_ERROR, "HPACK decode failed for stream %u", a_stream->id);
        dap_h2_connection_send_goaway(a_conn, DAP_H2_COMPRESSION_ERROR);
        return -1;
    }
    return 0;
}

static int s_process_frame(dap_h2_connection_t *a_conn, const dap_h2_frame_header_t *a_hdr,
                           const uint8_t *a_payload)
{
    switch (a_hdr->type) {
    case DAP_H2_FRAME_SETTINGS: {
        if (a_hdr->flags & DAP_H2_FLAG_ACK) {
            log_it(L_DEBUG, "SETTINGS ACK received");
            if (a_conn->state == DAP_H2_CONN_SETTINGS)
                a_conn->state = DAP_H2_CONN_ACTIVE;
            break;
        }
        if (s_apply_settings(a_conn, a_payload, a_hdr->length) != 0)
            return -1;
        s_send_settings_ack(a_conn);
        break;
    }

    case DAP_H2_FRAME_PING: {
        if (a_hdr->flags & DAP_H2_FLAG_ACK)
            break;
        uint8_t l_buf[9 + 8];
        size_t l_n = dap_h2_frame_ping(l_buf, sizeof(l_buf), a_payload, true);
        if (l_n)
            s_conn_write(a_conn, l_buf, l_n);
        break;
    }

    case DAP_H2_FRAME_GOAWAY: {
        log_it(L_NOTICE, "GOAWAY received, error=%s",
               a_hdr->length >= 8 ? dap_h2_error_str(
                   ((uint32_t)a_payload[4] << 24) | ((uint32_t)a_payload[5] << 16) |
                   ((uint32_t)a_payload[6] << 8) | a_payload[7]) : "?");
        a_conn->state = DAP_H2_CONN_GOAWAY;
        break;
    }

    case DAP_H2_FRAME_WINDOW_UPDATE: {
        if (a_hdr->length != 4) {
            dap_h2_connection_send_goaway(a_conn, DAP_H2_FRAME_SIZE_ERROR);
            return -1;
        }
        uint32_t l_inc = ((uint32_t)a_payload[0] << 24) | ((uint32_t)a_payload[1] << 16) |
                       ((uint32_t)a_payload[2] << 8) | a_payload[3];
        l_inc &= 0x7fffffffu;
        if (l_inc == 0) {
            if (a_hdr->stream_id == 0) {
                dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
                return -1;
            }
            s_send_rst_stream(a_conn, a_hdr->stream_id, DAP_H2_PROTOCOL_ERROR);
            break;
        }
        if (a_hdr->stream_id == 0) {
            a_conn->send_window += (int32_t)l_inc;
        } else {
            dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_hdr->stream_id);
            if (l_stream)
                l_stream->send_window += (int32_t)l_inc;
        }
        break;
    }

    case DAP_H2_FRAME_RST_STREAM: {
        dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_hdr->stream_id);
        if (l_stream)
            dap_h2_stream_close(a_conn, l_stream);
        break;
    }

    case DAP_H2_FRAME_HEADERS: {
        dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_hdr->stream_id);
        if (!l_stream) {
            if (a_hdr->stream_id <= a_conn->last_peer_stream_id || (a_hdr->stream_id & 1u) == 0) {
                dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
                return -1;
            }
            l_stream = dap_h2_stream_create(a_conn, a_hdr->stream_id);
            if (!l_stream) {
                s_send_rst_stream(a_conn, a_hdr->stream_id, DAP_H2_REFUSED_STREAM);
                break;
            }
        }

        const uint8_t *l_hdr_data = a_payload;
        size_t l_hdr_len = a_hdr->length;

        if (a_hdr->flags & DAP_H2_FLAG_PADDED) {
            if (l_hdr_len < 1)
                break;
            uint8_t l_pad_len = l_hdr_data[0];
            l_hdr_data++;
            l_hdr_len--;
            if (l_pad_len >= l_hdr_len)
                break;
            l_hdr_len -= l_pad_len;
        }

        if (a_hdr->flags & DAP_H2_FLAG_PRIORITY) {
            if (l_hdr_len < 5)
                break;
            l_hdr_data += 5;
            l_hdr_len -= 5;
        }

        if (l_stream->header_buf_len + l_hdr_len > DAP_H2_HEADER_BUF_SIZE) {
            s_send_rst_stream(a_conn, a_hdr->stream_id, DAP_H2_ENHANCE_YOUR_CALM);
            break;
        }
        memcpy(l_stream->header_buf + l_stream->header_buf_len, l_hdr_data, l_hdr_len);
        l_stream->header_buf_len += l_hdr_len;

        if (a_hdr->flags & DAP_H2_FLAG_END_HEADERS) {
            if (s_process_headers_block(a_conn, l_stream) != 0)
                return -1;
            log_it(L_INFO, "H2 stream %u: %s %s", l_stream->id, l_stream->method, l_stream->path);
        }

        if (a_hdr->flags & DAP_H2_FLAG_END_STREAM) {
            l_stream->state = DAP_H2_SSTATE_HALF_CLOSED_REMOTE;
            s_stream_complete(a_conn, l_stream);
        }
        break;
    }

    case DAP_H2_FRAME_CONTINUATION: {
        dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_hdr->stream_id);
        if (!l_stream) {
            dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
            return -1;
        }
        if (l_stream->header_buf_len + a_hdr->length > DAP_H2_HEADER_BUF_SIZE) {
            s_send_rst_stream(a_conn, a_hdr->stream_id, DAP_H2_ENHANCE_YOUR_CALM);
            break;
        }
        memcpy(l_stream->header_buf + l_stream->header_buf_len, a_payload, a_hdr->length);
        l_stream->header_buf_len += a_hdr->length;

        if (a_hdr->flags & DAP_H2_FLAG_END_HEADERS) {
            if (s_process_headers_block(a_conn, l_stream) != 0)
                return -1;
            log_it(L_INFO, "H2 stream %u: %s %s", l_stream->id, l_stream->method, l_stream->path);
        }
        break;
    }

    case DAP_H2_FRAME_DATA: {
        dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_hdr->stream_id);
        if (!l_stream) {
            s_send_rst_stream(a_conn, a_hdr->stream_id, DAP_H2_STREAM_CLOSED);
            break;
        }

        const uint8_t *l_data_ptr = a_payload;
        size_t l_data_len = a_hdr->length;

        if (a_hdr->flags & DAP_H2_FLAG_PADDED) {
            if (l_data_len < 1)
                break;
            uint8_t l_pad_len = l_data_ptr[0];
            l_data_ptr++;
            l_data_len--;
            if (l_pad_len >= l_data_len)
                break;
            l_data_len -= l_pad_len;
        }

        if (l_data_len > 0) {
            size_t l_new_cap = l_stream->body_len + l_data_len;
            if (l_new_cap > l_stream->body_cap) {
                size_t l_cap = l_stream->body_cap ? l_stream->body_cap * 2 : 4096;
                while (l_cap < l_new_cap)
                    l_cap *= 2;
                uint8_t *l_nb = DAP_NEW_Z_SIZE(uint8_t, l_cap);
                if (!l_nb)
                    return -1;
                if (l_stream->body) {
                    memcpy(l_nb, l_stream->body, l_stream->body_len);
                    DAP_DELETE(l_stream->body);
                }
                l_stream->body = l_nb;
                l_stream->body_cap = l_cap;
            }
            memcpy(l_stream->body + l_stream->body_len, l_data_ptr, l_data_len);
            l_stream->body_len += l_data_len;
        }

        a_conn->recv_window -= (int32_t)a_hdr->length;
        l_stream->recv_window -= (int32_t)a_hdr->length;

        if (a_conn->recv_window < (int32_t)(DAP_H2_DEFAULT_WINDOW_SIZE / 2)) {
            uint32_t l_inc = DAP_H2_DEFAULT_WINDOW_SIZE - (uint32_t)a_conn->recv_window;
            s_send_window_update(a_conn, 0, l_inc);
            a_conn->recv_window += (int32_t)l_inc;
        }
        if (l_stream->recv_window < (int32_t)(a_conn->local_initial_window_size / 2)) {
            uint32_t l_inc = a_conn->local_initial_window_size - (uint32_t)l_stream->recv_window;
            s_send_window_update(a_conn, l_stream->id, l_inc);
            l_stream->recv_window += (int32_t)l_inc;
        }

        if (a_hdr->flags & DAP_H2_FLAG_END_STREAM) {
            l_stream->state = DAP_H2_SSTATE_HALF_CLOSED_REMOTE;
            s_stream_complete(a_conn, l_stream);
        }
        break;
    }

    case DAP_H2_FRAME_PRIORITY:
        break;

    case DAP_H2_FRAME_PUSH_PROMISE:
        dap_h2_connection_send_goaway(a_conn, DAP_H2_PROTOCOL_ERROR);
        return -1;

    default:
        break;
    }

    return 0;
}

/* --- Main input processor ----------------------------------------------- */

int dap_h2_connection_input(dap_h2_connection_t *a_conn, const uint8_t *a_data, size_t a_len, size_t *a_consumed)
{
    if (!a_conn || !a_data || !a_consumed)
        return -1;
    *a_consumed = 0;
    size_t l_off = 0;

    if (a_conn->state == DAP_H2_CONN_PREFACE) {
        size_t l_need = DAP_H2_CONNECTION_PREFACE_LEN - a_conn->preface_received;
        size_t l_avail = a_len < l_need ? a_len : l_need;
        if (memcmp(a_data, &DAP_H2_CONNECTION_PREFACE[a_conn->preface_received], l_avail) != 0) {
            log_it(L_ERROR, "Invalid HTTP/2 connection preface");
            return -1;
        }
        a_conn->preface_received += l_avail;
        l_off += l_avail;
        if (a_conn->preface_received < DAP_H2_CONNECTION_PREFACE_LEN) {
            *a_consumed = l_off;
            return 0;
        }
        log_it(L_NOTICE, "HTTP/2 connection preface received");
        a_conn->state = DAP_H2_CONN_SETTINGS;
        dap_h2_connection_send_settings(a_conn);
    }

    while (l_off + DAP_H2_FRAME_HEADER_SIZE <= a_len) {
        dap_h2_frame_header_t l_fhdr;
        if (dap_h2_frame_header_parse(a_data + l_off, a_len - l_off, &l_fhdr) != 0)
            break;

        size_t l_frame_total = DAP_H2_FRAME_HEADER_SIZE + l_fhdr.length;
        if (l_off + l_frame_total > a_len)
            break;

        if (l_fhdr.length > a_conn->local_max_frame_size) {
            dap_h2_connection_send_goaway(a_conn, DAP_H2_FRAME_SIZE_ERROR);
            *a_consumed = l_off;
            return -1;
        }

        const uint8_t *l_payload = a_data + l_off + DAP_H2_FRAME_HEADER_SIZE;

        log_it(L_DEBUG, "H2 frame: type=%s len=%u stream=%u flags=0x%02x",
               dap_h2_frame_type_str(l_fhdr.type), l_fhdr.length, l_fhdr.stream_id, l_fhdr.flags);

        if (s_process_frame(a_conn, &l_fhdr, l_payload) != 0) {
            *a_consumed = l_off + l_frame_total;
            return -1;
        }

        l_off += l_frame_total;
    }

    *a_consumed = l_off;
    return 0;
}

/* --- Response sending --------------------------------------------------- */

int dap_h2_connection_send_response(dap_h2_connection_t *a_conn, uint32_t a_stream_id,
                                     uint16_t a_status, const dap_hpack_header_t *a_headers,
                                     size_t a_header_count, const uint8_t *a_body, size_t a_body_len)
{
    if (!a_conn)
        return -1;

    char l_status_str[8];
    snprintf(l_status_str, sizeof(l_status_str), "%u", (unsigned)a_status);

    size_t l_total_hdrs = 1 + a_header_count;
    dap_hpack_header_t *l_all = DAP_NEW_Z_COUNT(dap_hpack_header_t, l_total_hdrs);
    if (!l_all)
        return -1;

    l_all[0].name = (char *)":status";
    l_all[0].name_len = 7;
    l_all[0].value = l_status_str;
    l_all[0].value_len = strlen(l_status_str);
    if (a_headers)
        memcpy(l_all + 1, a_headers, a_header_count * sizeof(dap_hpack_header_t));

    uint8_t l_hpack_buf[DAP_H2_HEADER_BUF_SIZE];
    size_t l_hpack_len = 0;
    int l_rc = dap_hpack_encode(&a_conn->hpack_enc, l_all, l_total_hdrs,
                              l_hpack_buf, sizeof(l_hpack_buf), &l_hpack_len);
    DAP_DELETE(l_all);
    if (l_rc != 0) {
        log_it(L_ERROR, "Failed to encode response headers for stream %u", a_stream_id);
        return -1;
    }

    uint8_t l_flags = DAP_H2_FLAG_END_HEADERS;
    if (!a_body || a_body_len == 0)
        l_flags |= DAP_H2_FLAG_END_STREAM;

    uint8_t *l_frame = DAP_NEW_Z_SIZE(uint8_t, DAP_H2_FRAME_HEADER_SIZE + l_hpack_len);
    if (!l_frame)
        return -1;
    size_t l_fn = dap_h2_frame_headers(l_frame, DAP_H2_FRAME_HEADER_SIZE + l_hpack_len,
                                     a_stream_id, l_flags, l_hpack_buf, l_hpack_len);
    if (l_fn)
        s_conn_write(a_conn, l_frame, l_fn);
    DAP_DELETE(l_frame);

    if (a_body && a_body_len > 0) {
        size_t l_sent = 0;
        while (l_sent < a_body_len) {
            size_t l_chunk = a_body_len - l_sent;
            if (l_chunk > a_conn->peer_max_frame_size)
                l_chunk = a_conn->peer_max_frame_size;
            uint8_t l_df = (l_sent + l_chunk >= a_body_len) ? DAP_H2_FLAG_END_STREAM : 0;
            uint8_t *l_dframe = DAP_NEW_Z_SIZE(uint8_t, DAP_H2_FRAME_HEADER_SIZE + l_chunk);
            if (!l_dframe)
                return -1;
            size_t l_dn = dap_h2_frame_data(l_dframe, DAP_H2_FRAME_HEADER_SIZE + l_chunk,
                                          a_stream_id, l_df, a_body + l_sent, l_chunk);
            if (l_dn)
                s_conn_write(a_conn, l_dframe, l_dn);
            DAP_DELETE(l_dframe);
            l_sent += l_chunk;
        }
    }

    dap_h2_stream_t *l_stream = dap_h2_stream_find(a_conn, a_stream_id);
    if (l_stream) {
        if (l_stream->state == DAP_H2_SSTATE_HALF_CLOSED_REMOTE)
            dap_h2_stream_close(a_conn, l_stream);
        else
            l_stream->state = DAP_H2_SSTATE_HALF_CLOSED_LOCAL;
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Client-side API                                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief dap_h2_connection_client_init  Initialize an h2 connection in client mode.
 *        Sends the client connection preface (magic + SETTINGS) immediately.
 */
int dap_h2_connection_client_init(dap_h2_connection_t *a_conn, struct dap_events_socket *a_esocket)
{
    if (!a_conn || !a_esocket)
        return -1;

    memset(a_conn, 0, sizeof(*a_conn));
    a_conn->is_client = true;
    a_conn->state = DAP_H2_CONN_SETTINGS;
    a_conn->esocket = a_esocket;

    a_conn->peer_header_table_size    = DAP_H2_DEFAULT_HEADER_TABLE_SIZE;
    a_conn->peer_enable_push          = DAP_H2_DEFAULT_ENABLE_PUSH;
    a_conn->peer_max_concurrent       = DAP_H2_DEFAULT_MAX_CONCURRENT;
    a_conn->peer_initial_window_size  = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    a_conn->peer_max_frame_size       = DAP_H2_DEFAULT_MAX_FRAME_SIZE;
    a_conn->peer_max_header_list_size = 0;

    a_conn->local_max_frame_size      = DAP_H2_DEFAULT_MAX_FRAME_SIZE;
    a_conn->local_initial_window_size = DAP_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    a_conn->local_max_concurrent      = DAP_H2_DEFAULT_MAX_CONCURRENT;

    a_conn->send_window = DAP_H2_DEFAULT_WINDOW_SIZE;
    a_conn->recv_window = DAP_H2_DEFAULT_WINDOW_SIZE;

    a_conn->preface_received = DAP_H2_CONNECTION_PREFACE_LEN; /* client doesn't receive preface */
    a_conn->last_local_stream_id = 0;

    dap_hpack_context_init(&a_conn->hpack_dec, DAP_H2_DEFAULT_HEADER_TABLE_SIZE);
    dap_hpack_context_init(&a_conn->hpack_enc, DAP_H2_DEFAULT_HEADER_TABLE_SIZE);

    /* Send client connection preface: magic + SETTINGS */
    s_conn_write(a_conn, (const uint8_t *)DAP_H2_CONNECTION_PREFACE, DAP_H2_CONNECTION_PREFACE_LEN);
    dap_h2_connection_send_settings(a_conn);

    return 0;
}

/**
 * @brief dap_h2_connection_send_request  Send an HTTP/2 request on a new client-initiated stream.
 * @return stream id (odd, >=1) on success, 0 on error.
 */
uint32_t dap_h2_connection_send_request(dap_h2_connection_t *a_conn, const char *a_method,
                                         const char *a_path, const char *a_authority,
                                         const dap_hpack_header_t *a_headers, size_t a_header_count,
                                         const uint8_t *a_body, size_t a_body_len)
{
    if (!a_conn || !a_method || !a_path)
        return 0;

    /* Allocate a new odd-numbered stream */
    uint32_t l_stream_id = a_conn->last_local_stream_id == 0 ? 1 : a_conn->last_local_stream_id + 2;

    dap_h2_stream_t *l_stream = dap_h2_stream_create(a_conn, l_stream_id);
    if (!l_stream)
        return 0;
    a_conn->last_local_stream_id = l_stream_id;
    l_stream->state = DAP_H2_SSTATE_OPEN;

    /* Build pseudo-headers + user headers for HPACK encoding */
    size_t l_total_hdrs = 4 + a_header_count;
    dap_hpack_header_t *l_all = DAP_NEW_Z_COUNT(dap_hpack_header_t, l_total_hdrs);
    if (!l_all)
        return 0;

    l_all[0].name = ":method";    l_all[0].name_len = 7;
    l_all[0].value = (char *)a_method;  l_all[0].value_len = strlen(a_method);

    l_all[1].name = ":scheme";    l_all[1].name_len = 7;
    l_all[1].value = "https";     l_all[1].value_len = 5;

    l_all[2].name = ":path";      l_all[2].name_len = 5;
    l_all[2].value = (char *)a_path;    l_all[2].value_len = strlen(a_path);

    l_all[3].name = ":authority";  l_all[3].name_len = 10;
    l_all[3].value = a_authority ? (char *)a_authority : "";
    l_all[3].value_len = a_authority ? strlen(a_authority) : 0;

    for (size_t l_i = 0; l_i < a_header_count; l_i++)
        l_all[4 + l_i] = a_headers[l_i];

    uint8_t l_hpack_buf[16384];
    size_t l_hpack_len = 0;
    dap_hpack_encode(&a_conn->hpack_enc, l_all, l_total_hdrs, l_hpack_buf, sizeof(l_hpack_buf), &l_hpack_len);
    DAP_DELETE(l_all);

    /* HEADERS frame */
    uint8_t l_hdr_flags = DAP_H2_FLAG_END_HEADERS;
    if (!a_body || a_body_len == 0)
        l_hdr_flags |= DAP_H2_FLAG_END_STREAM;

    uint8_t *l_frame = DAP_NEW_Z_SIZE(uint8_t, DAP_H2_FRAME_HEADER_SIZE + l_hpack_len);
    if (!l_frame)
        return 0;
    size_t l_fn = dap_h2_frame_headers(l_frame, DAP_H2_FRAME_HEADER_SIZE + l_hpack_len,
                                       l_stream_id, l_hdr_flags, l_hpack_buf, l_hpack_len);
    if (l_fn)
        s_conn_write(a_conn, l_frame, l_fn);
    DAP_DELETE(l_frame);

    /* DATA frames (if body present) */
    if (a_body && a_body_len > 0) {
        size_t l_sent = 0;
        while (l_sent < a_body_len) {
            size_t l_chunk = a_body_len - l_sent;
            if (l_chunk > a_conn->peer_max_frame_size)
                l_chunk = a_conn->peer_max_frame_size;
            uint8_t l_df = (l_sent + l_chunk >= a_body_len) ? DAP_H2_FLAG_END_STREAM : 0;
            uint8_t *l_dframe = DAP_NEW_Z_SIZE(uint8_t, DAP_H2_FRAME_HEADER_SIZE + l_chunk);
            if (!l_dframe)
                return 0;
            size_t l_dn = dap_h2_frame_data(l_dframe, DAP_H2_FRAME_HEADER_SIZE + l_chunk,
                                            l_stream_id, l_df, a_body + l_sent, l_chunk);
            if (l_dn)
                s_conn_write(a_conn, l_dframe, l_dn);
            DAP_DELETE(l_dframe);
            l_sent += l_chunk;
        }
    }

    l_stream->state = DAP_H2_SSTATE_HALF_CLOSED_LOCAL;
    return l_stream_id;
}
