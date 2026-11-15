/**
 * @file dap_carrier_dap.h
 * @brief Shared HTTP-DAP phase machine for TLS-based VPN transport adapters.
 *
 * Extracted from the REALITY adapter (plan phase 0). This is the protocol-
 * independent part of a transport adapter: it drives the node's HTTP-DAP
 * wire format (POST /enc_init, POST /stream_ctl, GET /stream + takeover) over
 * a bidirectional byte pipe (the carrier bridge's data fd wrapped in a
 * dap_events_socket), then runs the raw DAP stream.
 *
 * A transport adapter (reality/trojan/anytls/xhttp/shadowtls) only provides:
 *   - stage_prepare: trust resolution + bridge start + esocket wrap + stream
 *     creation, filling a dap_carrier_dap_ctx_t embedded as the FIRST member
 *     of its own context and installing the hooks below;
 *   - caps + registration.
 * The ops vtable (handshake_init/session_create/session_start/write/close)
 * and the esocket callbacks point directly at the shared functions here.
 *
 * SDK-aware (this is adapter-support, NOT the SDK-free carrier core).
 */

#ifndef DAP_CARRIER_DAP_H
#define DAP_CARRIER_DAP_H

#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_client.h"
#include "dap_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAP_CARRIER_PHASE_CONNECTING = 0,   /* bridge handshake in flight */
    DAP_CARRIER_PHASE_ENC_WAIT,         /* POST /enc_init sent, awaiting reply */
    DAP_CARRIER_PHASE_CTL_IDLE,         /* enc done, FSM has not called session_create */
    DAP_CARRIER_PHASE_CTL_WAIT,         /* POST /stream_ctl sent */
    DAP_CARRIER_PHASE_STREAM_IDLE,      /* ctl done, FSM has not called session_start */
    DAP_CARRIER_PHASE_STREAM_200_WAIT,  /* GET /stream sent, awaiting "HTTP/1.1 200" */
    DAP_CARRIER_PHASE_STREAMING,        /* raw bidirectional pipe */
    DAP_CARRIER_PHASE_CLOSED,
} dap_carrier_phase_t;

/**
 * Transport-specific hooks. user points at the transport context (whose first
 * member is dap_carrier_dap_ctx_t, so the pointers coincide).
 *   signal_stop: non-blocking bridge stop (fail/close paths; may run on FSM thread)
 *   stop:        bridge stop + join (data esocket delete, worker thread)
 *   destroy:     free the transport context after common cleanup
 */
typedef struct dap_carrier_dap_hooks {
    void *user;
    void (*signal_stop)(void *a_user);
    void (*stop)(void *a_user);
    void (*destroy)(void *a_user);
} dap_carrier_dap_hooks_t;

typedef struct dap_carrier_dap_ctx {
    dap_events_socket_t *es;        /* data esocket (FSM/stream owns the fd) */
    dap_events_socket_t *ctrl_es;   /* ctrl esocket (adapter-owned) */
    dap_stream_t *stream;
    dap_client_t *client;
    dap_carrier_phase_t phase;

    /* FSM-provided callbacks (installed at stage_prepare); wrappers chain into
     * them with the ORIGINAL arg so FSM bookkeeping keeps working. */
    dap_events_socket_callbacks_t base_cbs;

    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_trans_session_cb_t session_cb;
    dap_net_trans_ready_cb_t ready_cb;

    /* handshake request (ownership of alice_pub_key taken on success) */
    uint8_t *alice;

    /* HTTP response accumulation (headers + Content-Length body) */
    uint8_t *http;
    size_t http_size;
    size_t headers_size;
    size_t content_len;
    bool have_headers;
    int http_status;

    /* pre-takeover stream writes (keepalive/channel pings) */
    uint8_t *pending;
    size_t pending_size;

    /* stream plaintext tail (STREAMING) */
    uint8_t *tail;
    size_t tail_size;

    /* transport glue */
    dap_carrier_dap_hooks_t hooks;
    const char *log_tag;        /* e.g. "REALITY", "TROJAN" (static string) */
    const char *default_host;   /* Host header fallback (e.g. "127.0.0.1") */
} dap_carrier_dap_ctx_t;

const char *dap_carrier_phase_str(dap_carrier_phase_t a_phase);

/* esocket callbacks — install on the data / ctrl esockets at stage_prepare. */
void dap_carrier_dap_read_cb(dap_events_socket_t *a_es, void *a_arg);
void dap_carrier_dap_ctrl_read_cb(dap_events_socket_t *a_es, void *a_arg);
void dap_carrier_dap_ctrl_delete(dap_events_socket_t *a_es, void *a_arg);
void dap_carrier_dap_es_error(dap_events_socket_t *a_es, int a_err);
void dap_carrier_dap_es_delete(dap_events_socket_t *a_es, void *a_arg);

/* Tear the tunnel down (marks data esocket for close; FSM learns via delete). */
void dap_carrier_dap_fail(dap_carrier_dap_ctx_t *a_ctx, const char *a_why);

/* Transport ops — point the ops vtable directly at these. They fetch the ctx
 * from stream->trans_ctx->transport_priv (== embedded dap_carrier_dap_ctx_t). */
int dap_carrier_dap_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                   dap_net_trans_handshake_cb_t a_callback);
int dap_carrier_dap_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                   dap_net_trans_session_cb_t a_callback);
int dap_carrier_dap_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                  dap_net_trans_ready_cb_t a_callback);
ssize_t dap_carrier_dap_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
void dap_carrier_dap_close(dap_stream_t *a_stream);

#ifdef __cplusplus
}
#endif

#endif /* DAP_CARRIER_DAP_H */