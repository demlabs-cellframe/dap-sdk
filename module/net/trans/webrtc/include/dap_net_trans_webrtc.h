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
 * @file dap_net_trans_webrtc.h
 * @brief WebRTC Data Channel Transport
 *
 * WASM: Uses browser RTCPeerConnection + RTCDataChannel via EM_JS.
 * Native: Uses libdatachannel for DTLS/SCTP/ICE/STUN/TURN.
 *
 * DAP encryption runs ON TOP of WebRTC's DTLS — dap_stream_pkt encrypts
 * before calling trans->ops->write(), so data channel carries DAP-encrypted
 * payloads.
 *
 * Signaling:
 *   - Initial connection: HTTP REST (/rtc/offer, /rtc/ice)
 *   - Reconnections: DAP stream channel DAP_STREAM_CH_WEBRTC_SIGNAL
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dap_net_trans;
struct dap_stream;

typedef enum dap_webrtc_state {
    DAP_WEBRTC_STATE_NEW          = 0,
    DAP_WEBRTC_STATE_CONNECTING   = 1,
    DAP_WEBRTC_STATE_CONNECTED    = 2,
    DAP_WEBRTC_STATE_DISCONNECTED = 3,
    DAP_WEBRTC_STATE_FAILED       = 4,
    DAP_WEBRTC_STATE_CLOSED       = 5
} dap_webrtc_state_t;

typedef struct dap_net_trans_webrtc_config {
    const char *stun_server;
    const char *turn_server;
    const char *turn_username;
    const char *turn_credential;
    bool        ordered;
    int         max_retransmits;
} dap_net_trans_webrtc_config_t;

int dap_net_trans_webrtc_register(void);
int dap_net_trans_webrtc_unregister(void);
dap_net_trans_webrtc_config_t dap_net_trans_webrtc_config_default(void);
bool dap_net_trans_is_webrtc(const struct dap_stream *a_stream);

struct dap_http_server;
int dap_net_trans_webrtc_signaling_init(struct dap_http_server *a_http);

typedef int (*dap_webrtc_offer_cb_t)(const char *a_sdp_offer, size_t a_offer_len,
                                      char **a_sdp_answer, size_t *a_answer_len,
                                      void *a_user_data);
typedef int (*dap_webrtc_ice_cb_t)(uint32_t a_session_id,
                                    const char *a_ice_json, size_t a_ice_len,
                                    char **a_response_json, size_t *a_response_len,
                                    void *a_user_data);

void dap_net_trans_webrtc_signaling_set_callbacks(
    dap_webrtc_offer_cb_t a_offer_cb,
    dap_webrtc_ice_cb_t a_ice_cb,
    void *a_user_data);

#ifdef __cplusplus
}
#endif
