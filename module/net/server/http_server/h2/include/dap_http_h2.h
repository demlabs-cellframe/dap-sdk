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
 * HTTP/2 connection handler for DAP SDK (RFC 7540).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dap_http_h2_frame.h"
#include "dap_http_h2_hpack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_H2_MAX_STREAMS       128
#define DAP_H2_HEADER_BUF_SIZE   (64 * 1024)

typedef enum {
    DAP_H2_SSTATE_IDLE = 0,
    DAP_H2_SSTATE_OPEN,
    DAP_H2_SSTATE_HALF_CLOSED_REMOTE,
    DAP_H2_SSTATE_HALF_CLOSED_LOCAL,
    DAP_H2_SSTATE_CLOSED
} dap_h2_stream_state_t;

typedef struct dap_h2_stream {
    uint32_t               id;                    /* Stream identifier */
    dap_h2_stream_state_t  state;               /* RFC 7540 stream state */
    int32_t                send_window;           /* Send-side flow-control window */
    int32_t                recv_window;           /* Receive-side flow-control window */
    char                   method[32];            /* :method pseudo-header */
    char                   path[1024];          /* :path pseudo-header */
    char                   scheme[16];          /* :scheme pseudo-header */
    char                   authority[256];        /* :authority pseudo-header */
    uint16_t               status;               /* :status pseudo-header (response) */
    struct dap_http_header *headers;            /* Decoded request headers */
    uint8_t               *body;                  /* Request body buffer */
    size_t                 body_len;              /* Used length of body */
    size_t                 body_cap;              /* Allocated capacity of body */
    uint8_t               *header_buf;          /* Accumulated HPACK block */
    size_t                 header_buf_len;        /* Bytes used in header_buf */
    bool                   headers_complete;      /* HPACK decode completed */
    struct dap_h2_stream  *next;                  /* Intrusive list next */
    struct dap_h2_stream  *prev;                  /* Intrusive list prev */
} dap_h2_stream_t;

typedef enum {
    DAP_H2_CONN_PREFACE = 0,
    DAP_H2_CONN_SETTINGS,
    DAP_H2_CONN_ACTIVE,
    DAP_H2_CONN_GOAWAY,
    DAP_H2_CONN_CLOSED
} dap_h2_conn_state_t;

struct dap_http_server;
struct dap_events_socket;

typedef struct dap_h2_connection {
    dap_h2_conn_state_t    state;               /* Connection lifecycle state */
    uint32_t               peer_header_table_size;   /* SETTINGS_HEADER_TABLE_SIZE */
    uint32_t               peer_enable_push;         /* SETTINGS_ENABLE_PUSH */
    uint32_t               peer_max_concurrent;      /* SETTINGS_MAX_CONCURRENT_STREAMS */
    uint32_t               peer_initial_window_size; /* SETTINGS_INITIAL_WINDOW_SIZE */
    uint32_t               peer_max_frame_size;      /* SETTINGS_MAX_FRAME_SIZE */
    uint32_t               peer_max_header_list_size; /* SETTINGS_MAX_HEADER_LIST_SIZE */
    uint32_t               local_max_frame_size;     /* Our MAX_FRAME_SIZE */
    uint32_t               local_initial_window_size; /* Our INITIAL_WINDOW_SIZE */
    uint32_t               local_max_concurrent;     /* Our MAX_CONCURRENT_STREAMS */
    int32_t                send_window;           /* Connection send window */
    int32_t                recv_window;           /* Connection receive window */
    dap_h2_stream_t       *streams;             /* Head of stream list */
    uint32_t               stream_count;        /* Number of active streams */
    uint32_t               last_peer_stream_id; /* Last opened peer-initiated stream id */
    uint32_t               last_local_stream_id; /* Last opened local stream id (reserved) */
    dap_hpack_context_t    hpack_dec;           /* Decoder dynamic table */
    dap_hpack_context_t    hpack_enc;           /* Encoder dynamic table */
    size_t                 preface_received;    /* Bytes of connection preface consumed */
    bool                   is_client;          /* true for client-initiated connections */
    bool                   settings_acked;     /* Server's SETTINGS ACK received */
    struct dap_http_server   *http;             /* Owning HTTP server (NULL on client) */
    struct dap_events_socket *esocket;          /* Underlying socket */

    /* Client-side response callback */
    void (*response_cb)(dap_h2_stream_t *a_stream, uint16_t a_status, void *a_arg);
    void *response_cb_arg;
} dap_h2_connection_t;

/* Invoked when a request on a stream is ready to be handled */
typedef void (*dap_h2_request_cb_t)(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream);

/**
 * @defgroup DAP_HTTP_H2_STREAM Stream helpers
 * @{
 */

/**
 * @brief Find a stream by id on a connection.
 */
dap_h2_stream_t *dap_h2_stream_find(dap_h2_connection_t *a_conn, uint32_t a_stream_id);

/**
 * @brief Create and link a new stream (respects local concurrency limit).
 */
dap_h2_stream_t *dap_h2_stream_create(dap_h2_connection_t *a_conn, uint32_t a_stream_id);

/**
 * @brief Remove a stream and free its resources.
 */
void dap_h2_stream_close(dap_h2_connection_t *a_conn, dap_h2_stream_t *a_stream);

/** @} */

/**
 * @defgroup DAP_HTTP_H2_CONN Connection lifecycle
 * @{
 */

/**
 * @brief Initialize HPACK contexts, defaults, and connection state.
 */
int dap_h2_connection_init(dap_h2_connection_t *a_conn, struct dap_http_server *a_http,
                           struct dap_events_socket *a_esocket);

/**
 * @brief Tear down all streams and HPACK state.
 */
void dap_h2_connection_deinit(dap_h2_connection_t *a_conn);

/** @} */

/**
 * @defgroup DAP_HTTP_H2_IO Framed I/O
 * @{
 */

/**
 * @brief Consume input bytes (preface, then frames); sets @a a_consumed.
 */
int dap_h2_connection_input(dap_h2_connection_t *a_conn, const uint8_t *a_data, size_t a_len, size_t *a_consumed);

/**
 * @brief Send local SETTINGS frame to the peer.
 */
int dap_h2_connection_send_settings(dap_h2_connection_t *a_conn);

/**
 * @brief Send a complete response (HEADERS + optional DATA) on @a a_stream_id.
 */
int dap_h2_connection_send_response(dap_h2_connection_t *a_conn, uint32_t a_stream_id,
                                     uint16_t a_status, const dap_hpack_header_t *a_headers,
                                     size_t a_header_count, const uint8_t *a_body, size_t a_body_len);

/**
 * @brief Send GOAWAY with @a a_error_code and enter GOAWAY state.
 */
int dap_h2_connection_send_goaway(dap_h2_connection_t *a_conn, uint32_t a_error_code);

/** @} */

/**
 * @defgroup DAP_HTTP_H2_UTIL Utilities
 * @{
 */

/**
 * @brief True if @a a_data begins with the HTTP/2 connection preface.
 */
bool dap_h2_detect_preface(const uint8_t *a_data, size_t a_len);

/** @} */

/**
 * @defgroup DAP_HTTP_H2_CLIENT Client-side API
 * @{
 */

/**
 * @brief Initialize as HTTP/2 client: sends connection preface + SETTINGS.
 */
int dap_h2_connection_client_init(dap_h2_connection_t *a_conn, struct dap_events_socket *a_esocket);

/**
 * @brief Send an HTTP/2 request (HEADERS + optional DATA) on a new stream.
 * @return stream id on success, 0 on error.
 */
uint32_t dap_h2_connection_send_request(dap_h2_connection_t *a_conn, const char *a_method,
                                         const char *a_path, const char *a_authority,
                                         const dap_hpack_header_t *a_headers, size_t a_header_count,
                                         const uint8_t *a_body, size_t a_body_len);

/** @} */

#ifdef __cplusplus
}
#endif
