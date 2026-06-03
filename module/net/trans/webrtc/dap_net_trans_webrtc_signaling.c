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
 * @file dap_net_trans_webrtc_signaling.c
 * @brief WebRTC signaling HTTP endpoints (server-side)
 *
 * Registers /rtc/offer and /rtc/ice on the DAP HTTP server.
 * Delegates actual WebRTC peer handling to a callback interface
 * provided by the native transport (libdatachannel).
 */

#ifndef __EMSCRIPTEN__

#include <string.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http_server.h"
#include "dap_http_simple.h"
#include "dap_http_client.h"
#include "dap_http_status_code.h"
#include "dap_server.h"
#include "dap_net_trans_webrtc.h"

#define LOG_TAG "webrtc_signaling"

typedef int (*dap_webrtc_offer_cb_t)(const char *a_sdp_offer, size_t a_offer_len,
                                      char **a_sdp_answer, size_t *a_answer_len,
                                      void *a_user_data);

typedef int (*dap_webrtc_ice_cb_t)(uint32_t a_session_id,
                                    const char *a_ice_json, size_t a_ice_len,
                                    char **a_response_json, size_t *a_response_len,
                                    void *a_user_data);

static dap_webrtc_offer_cb_t s_offer_cb = NULL;
static dap_webrtc_ice_cb_t   s_ice_cb = NULL;
static void                  *s_user_data = NULL;

void dap_net_trans_webrtc_signaling_set_callbacks(
    dap_webrtc_offer_cb_t a_offer_cb,
    dap_webrtc_ice_cb_t a_ice_cb,
    void *a_user_data)
{
    s_offer_cb = a_offer_cb;
    s_ice_cb = a_ice_cb;
    s_user_data = a_user_data;
}

static void s_http_rtc_offer(dap_http_simple_t *a_simple, void *a_arg)
{
    dap_http_status_code_t *l_rc = (dap_http_status_code_t *)a_arg;
    dap_http_simple_set_flag_generate_default_header(a_simple, true);

    if (!a_simple->request || !a_simple->request_size) {
        dap_strncpy(a_simple->reply_mime, "application/json", sizeof(a_simple->reply_mime) - 1);
        dap_http_simple_reply_f(a_simple, "{\"error\":\"empty SDP offer\"}");
        *l_rc = DAP_HTTP_STATUS_BAD_REQUEST;
        return;
    }

    if (!s_offer_cb) {
        dap_strncpy(a_simple->reply_mime, "application/json", sizeof(a_simple->reply_mime) - 1);
        dap_http_simple_reply_f(a_simple, "{\"error\":\"WebRTC not configured on server\"}");
        *l_rc = DAP_HTTP_STATUS_SERVICE_UNAVAILABLE;
        return;
    }

    char *l_answer = NULL;
    size_t l_answer_len = 0;
    int l_ret = s_offer_cb((const char *)a_simple->request_byte, a_simple->request_size,
                            &l_answer, &l_answer_len, s_user_data);
    if (l_ret != 0 || !l_answer) {
        dap_strncpy(a_simple->reply_mime, "application/json", sizeof(a_simple->reply_mime) - 1);
        dap_http_simple_reply_f(a_simple, "{\"error\":\"failed to create SDP answer\"}");
        *l_rc = DAP_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return;
    }

    dap_strncpy(a_simple->reply_mime, "application/sdp", sizeof(a_simple->reply_mime) - 1);
    dap_http_simple_reply(a_simple, l_answer, l_answer_len);
    DAP_DELETE(l_answer);
    *l_rc = DAP_HTTP_STATUS_OK;

    log_it(L_INFO, "WebRTC /rtc/offer: SDP answer sent (%zu bytes)", l_answer_len);
}

static void s_http_rtc_ice(dap_http_simple_t *a_simple, void *a_arg)
{
    dap_http_status_code_t *l_rc = (dap_http_status_code_t *)a_arg;
    dap_http_simple_set_flag_generate_default_header(a_simple, true);
    dap_strncpy(a_simple->reply_mime, "application/json", sizeof(a_simple->reply_mime) - 1);

    if (!a_simple->request || !a_simple->request_size) {
        dap_http_simple_reply_f(a_simple, "{\"error\":\"empty ICE data\"}");
        *l_rc = DAP_HTTP_STATUS_BAD_REQUEST;
        return;
    }

    if (!s_ice_cb) {
        dap_http_simple_reply_f(a_simple, "{\"error\":\"WebRTC not configured\"}");
        *l_rc = DAP_HTTP_STATUS_SERVICE_UNAVAILABLE;
        return;
    }

    uint32_t l_session_id = 0;
    dap_http_client_t *l_cl = a_simple->http_client;
    if (l_cl && l_cl->in_query_string_len > 0) {
        const char *l_sid_str = strstr(l_cl->in_query_string, "session_id=");
        if (l_sid_str)
            l_session_id = (uint32_t)strtoul(l_sid_str + 11, NULL, 10);
    }

    char *l_response = NULL;
    size_t l_response_len = 0;
    int l_ret = s_ice_cb(l_session_id,
                          (const char *)a_simple->request_byte, a_simple->request_size,
                          &l_response, &l_response_len, s_user_data);
    if (l_ret != 0) {
        dap_http_simple_reply_f(a_simple, "{\"error\":\"ICE processing failed\"}");
        *l_rc = DAP_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return;
    }

    if (l_response && l_response_len > 0) {
        dap_http_simple_reply(a_simple, l_response, l_response_len);
        DAP_DELETE(l_response);
    } else {
        dap_http_simple_reply_f(a_simple, "[]");
    }
    *l_rc = DAP_HTTP_STATUS_OK;

    log_it(L_DEBUG, "WebRTC /rtc/ice: processed for session_id=%u", l_session_id);
}

int dap_net_trans_webrtc_signaling_init(dap_http_server_t *a_http)
{
    if (!a_http) {
        log_it(L_ERROR, "Cannot register WebRTC signaling: no HTTP server");
        return -1;
    }

    dap_http_simple_proc_add(a_http, "/rtc/offer", 65536, s_http_rtc_offer);
    dap_http_simple_proc_add(a_http, "/rtc/ice",   65536, s_http_rtc_ice);

    log_it(L_NOTICE, "WebRTC signaling endpoints registered: /rtc/offer, /rtc/ice");
    return 0;
}

#endif /* !__EMSCRIPTEN__ */
