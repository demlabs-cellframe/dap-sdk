/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
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

/**
 * @file dap_net_trans_webrtc_native.c
 * @brief WebRTC Data Channel transport — Native implementation (libdatachannel)
 *
 * Uses libdatachannel (https://github.com/paullouisageneau/libdatachannel) C API.
 * Supports both client and server modes:
 *   - Client: creates PeerConnection, generates offer, sends via HTTP signaling
 *   - Server: accepts offers via HTTP /rtc/offer, creates answer
 *
 * Architecture:
 *   - libdatachannel callbacks fire on its internal thread
 *   - onMessage → pipe/eventfd → DAP worker poll → dap_stream_data_proc_read_ext
 *   - write → rtcSendMessage (thread-safe in libdatachannel)
 *   - Signaling: HTTP REST (/rtc/offer, /rtc/ice) + DAP stream channel
 */

#ifndef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_cbuf.h"
#include "dap_net_trans.h"
#include "dap_net_trans_types.h"
#include "dap_net_trans_webrtc.h"
#include "dap_stream.h"
#include "dap_stream_session.h"

#define LOG_TAG "webrtc_native"

#ifdef DAP_HAVE_LIBDATACHANNEL

#include <rtc/rtc.h>

#define RTC_RECV_BUF_SIZE (256 * 1024)

typedef struct rtc_native_conn {
    int                     peer_id;
    int                     dc_id;
    dap_webrtc_state_t      state;
    dap_cbuf_t              recv_buf;

    dap_stream_t           *stream;
    dap_net_trans_connect_cb_t connect_cb;

    char                   *host;
    pthread_mutex_t         recv_mutex;
    pthread_t               recv_thread;
    uint16_t                port;
    bool                    recv_running;
    int                     notify_pipe[2];

    uint64_t                bytes_sent;
    uint64_t                bytes_received;
} rtc_native_conn_t;

/* ── libdatachannel callbacks ─────────────────────────────────────── */

static void s_on_state_change(int a_peer_id, rtcState a_state, void *a_user)
{
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_user;
    switch (a_state) {
    case RTC_CONNECTED:
        l_conn->state = DAP_WEBRTC_STATE_CONNECTED;
        log_it(L_NOTICE, "libdatachannel: peer connected (id=%d)", a_peer_id);
        break;
    case RTC_DISCONNECTED:
        l_conn->state = DAP_WEBRTC_STATE_DISCONNECTED;
        break;
    case RTC_FAILED:
        l_conn->state = DAP_WEBRTC_STATE_FAILED;
        break;
    case RTC_CLOSED:
        l_conn->state = DAP_WEBRTC_STATE_CLOSED;
        break;
    default:
        break;
    }
}

static void s_on_dc_open(int a_dc_id, void *a_user)
{
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_user;
    l_conn->state = DAP_WEBRTC_STATE_CONNECTED;
    log_it(L_NOTICE, "libdatachannel: data channel open (dc=%d)", a_dc_id);
    if (l_conn->notify_pipe[1] >= 0)
        write(l_conn->notify_pipe[1], "O", 1);
}

static void s_on_dc_closed(int a_dc_id, void *a_user)
{
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_user;
    l_conn->state = DAP_WEBRTC_STATE_CLOSED;
    if (l_conn->notify_pipe[1] >= 0)
        write(l_conn->notify_pipe[1], "C", 1);
}

static void s_on_dc_message(int a_dc_id, const char *a_data, int a_size, void *a_user)
{
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_user;
    if (a_size <= 0) return;

    pthread_mutex_lock(&l_conn->recv_mutex);
    dap_cbuf_push(l_conn->recv_buf, a_data, (size_t)a_size);
    l_conn->bytes_received += (uint64_t)a_size;
    pthread_mutex_unlock(&l_conn->recv_mutex);

    if (l_conn->notify_pipe[1] >= 0)
        write(l_conn->notify_pipe[1], "D", 1);
}

/* ── Recv thread ──────────────────────────────────────────────────── */

static void *s_recv_thread(void *a_arg)
{
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_arg;
    uint8_t l_buf[64 * 1024];
    char l_sig;

    while (l_conn->recv_running) {
        ssize_t l_r = read(l_conn->notify_pipe[0], &l_sig, 1);
        if (l_r <= 0) continue;
        if (l_sig == 'C') break;
        if (l_sig != 'D') continue;

        for (;;) {
            pthread_mutex_lock(&l_conn->recv_mutex);
            size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
            if (l_avail == 0) { pthread_mutex_unlock(&l_conn->recv_mutex); break; }
            size_t l_chunk = l_avail < sizeof(l_buf) ? l_avail : sizeof(l_buf);
            dap_cbuf_pop(l_conn->recv_buf, l_chunk, l_buf);
            pthread_mutex_unlock(&l_conn->recv_mutex);
            if (l_conn->stream)
                dap_stream_data_proc_read_ext(l_conn->stream, l_buf, l_chunk);
        }
    }
    return NULL;
}

/* ── Transport ops ────────────────────────────────────────────────── */

static int s_rtc_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    (void)a_trans; (void)a_config;
    rtcInitLogger(RTC_LOG_WARNING, NULL);
    log_it(L_NOTICE, "WebRTC transport initialized (native, libdatachannel)");
    return 0;
}

static void s_rtc_deinit(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    rtcCleanup();
}

static int s_rtc_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                         dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) return -1;

    rtc_native_conn_t *l_conn = DAP_NEW_Z(rtc_native_conn_t);
    if (!l_conn) return -1;

    l_conn->recv_buf = dap_cbuf_create(RTC_RECV_BUF_SIZE);
    pthread_mutex_init(&l_conn->recv_mutex, NULL);
    if (pipe(l_conn->notify_pipe) != 0) {
        DAP_DELETE(l_conn);
        return -1;
    }

    l_conn->host = dap_strdup(a_host);
    l_conn->port = a_port;
    l_conn->stream = a_stream;
    l_conn->connect_cb = a_callback;
    a_stream->_server_session = l_conn;

    if (!a_stream->session)
        a_stream->session = dap_stream_session_pure_new();

    rtcConfiguration l_config;
    memset(&l_config, 0, sizeof(l_config));
    const char *l_stun = "stun:stun.l.google.com:19302";
    l_config.iceServers = &l_stun;
    l_config.iceServersCount = 1;

    l_conn->peer_id = rtcCreatePeerConnection(&l_config);
    if (l_conn->peer_id < 0) {
        log_it(L_ERROR, "rtcCreatePeerConnection failed");
        if (a_callback) a_callback(a_stream, -1);
        return -1;
    }

    rtcSetUserPointer(l_conn->peer_id, l_conn);
    rtcSetStateChangeCallback(l_conn->peer_id, s_on_state_change);

    l_conn->dc_id = rtcCreateDataChannelEx(l_conn->peer_id, "dap-stream", NULL);
    if (l_conn->dc_id < 0) {
        log_it(L_ERROR, "rtcCreateDataChannel failed");
        rtcDeletePeerConnection(l_conn->peer_id);
        if (a_callback) a_callback(a_stream, -2);
        return -1;
    }

    rtcSetOpenCallback(l_conn->dc_id, s_on_dc_open);
    rtcSetClosedCallback(l_conn->dc_id, s_on_dc_closed);
    rtcSetMessageCallback(l_conn->dc_id, s_on_dc_message);

    char l_sdp[16384];
    if (rtcGetLocalDescription(l_conn->peer_id, l_sdp, sizeof(l_sdp)) < 0) {
        log_it(L_ERROR, "Failed to get local SDP offer");
        rtcDeletePeerConnection(l_conn->peer_id);
        if (a_callback) a_callback(a_stream, -3);
        return -1;
    }

    /* TODO: HTTP POST SDP offer to /rtc/offer, get answer, set remote description,
     * exchange ICE candidates. For now, the signaling path is stubbed —
     * full integration requires HTTP client (dap_client_http) or sync XHR equivalent. */

    l_conn->recv_running = true;
    pthread_create(&l_conn->recv_thread, NULL, s_recv_thread, l_conn);

    log_it(L_NOTICE, "WebRTC peer connection created (peer_id=%d, dc_id=%d)",
           l_conn->peer_id, l_conn->dc_id);
    return 0;
}

static ssize_t s_rtc_read(dap_stream_t *a_stream, void *a_buf, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_stream->_server_session;

    pthread_mutex_lock(&l_conn->recv_mutex);
    size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
    if (l_avail == 0) { pthread_mutex_unlock(&l_conn->recv_mutex); return 0; }
    size_t l_r = a_size < l_avail ? a_size : l_avail;
    size_t l_read = dap_cbuf_pop(l_conn->recv_buf, l_r, a_buf);
    pthread_mutex_unlock(&l_conn->recv_mutex);
    return (ssize_t)l_read;
}

static ssize_t s_rtc_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_stream->_server_session;
    if (l_conn->state != DAP_WEBRTC_STATE_CONNECTED || l_conn->dc_id < 0) return -1;

    int l_sent = rtcSendMessage(l_conn->dc_id, (const char *)a_data, (int)a_size);
    if (l_sent == RTC_ERR_SUCCESS) {
        l_conn->bytes_sent += (uint64_t)a_size;
        return (ssize_t)a_size;
    }
    return -1;
}

static void s_rtc_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return;
    rtc_native_conn_t *l_conn = (rtc_native_conn_t *)a_stream->_server_session;

    if (l_conn->recv_running) {
        l_conn->recv_running = false;
        write(l_conn->notify_pipe[1], "C", 1);
        pthread_join(l_conn->recv_thread, NULL);
    }

    if (l_conn->dc_id >= 0) rtcDeleteDataChannel(l_conn->dc_id);
    if (l_conn->peer_id >= 0) rtcDeletePeerConnection(l_conn->peer_id);

    close(l_conn->notify_pipe[0]);
    close(l_conn->notify_pipe[1]);
    dap_cbuf_delete(l_conn->recv_buf);

    log_it(L_INFO, "WebRTC closed (peer_id=%d, sent=%" PRIu64 ", recv=%" PRIu64 ")",
           l_conn->peer_id, l_conn->bytes_sent, l_conn->bytes_received);

    DAP_DEL_Z(l_conn->host);
    pthread_mutex_destroy(&l_conn->recv_mutex);
    DAP_DELETE(l_conn);
    a_stream->_server_session = NULL;
}

static uint32_t s_rtc_get_caps(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL
         | DAP_NET_TRANS_CAP_LOW_LATENCY;
}

static dap_net_trans_ops_t s_rtc_ops = {
    .init             = s_rtc_init,
    .deinit           = s_rtc_deinit,
    .connect          = s_rtc_connect,
    .read             = s_rtc_read,
    .write            = s_rtc_write,
    .close            = s_rtc_close,
    .get_capabilities = s_rtc_get_caps,
};

int dap_net_trans_webrtc_register(void)
{
    return dap_net_trans_register("webrtc", DAP_NET_TRANS_WEBRTC,
                                  &s_rtc_ops, DAP_NET_TRANS_SOCKET_OTHER, NULL);
}

int dap_net_trans_webrtc_unregister(void)
{
    return dap_net_trans_unregister(DAP_NET_TRANS_WEBRTC);
}

#else /* !DAP_HAVE_LIBDATACHANNEL */

/* Stub: compiles without libdatachannel but register/unregister are no-ops */

int dap_net_trans_webrtc_register(void)
{
    log_it(L_WARNING, "WebRTC transport not available: libdatachannel not found");
    return -1;
}

int dap_net_trans_webrtc_unregister(void)
{
    return 0;
}

#endif /* DAP_HAVE_LIBDATACHANNEL */

dap_net_trans_webrtc_config_t dap_net_trans_webrtc_config_default(void)
{
    return (dap_net_trans_webrtc_config_t) {
        .stun_server     = "stun:stun.l.google.com:19302",
        .turn_server     = NULL,
        .turn_username   = NULL,
        .turn_credential = NULL,
        .ordered         = true,
        .max_retransmits = -1
    };
}

bool dap_net_trans_is_webrtc(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans && a_stream->trans->type == DAP_NET_TRANS_WEBRTC;
}

#endif /* !__EMSCRIPTEN__ */
