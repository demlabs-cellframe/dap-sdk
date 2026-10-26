/* DAP client sequencing over an already authenticated/upgraded WSS channel.
 * TLS and HTTP Upgrade must complete before init; callbacks borrow data and
 * must defer destruction until dispatch returns. */
#ifndef DAP_WSS_CLIENT_SESSION_H
#define DAP_WSS_CLIENT_SESSION_H
#include "dap_wss_dap.h"
#include "dap_tls13_client_state.h"

typedef struct dap_wss_client_session {
    dap_wss_dap_t wire;
    dap_tls13_client_state_t state;
} dap_wss_client_session_t;

static int dap_wss_client_session_init(dap_wss_client_session_t *s,
    dap_tls13_reply_fn enc, dap_tls13_reply_fn ctl, dap_tls13_reply_fn data,
    void *arg, dap_wss_wire_write_fn write, void *write_arg)
{
    if (!s || s->wire.dispatch || !enc || !ctl || !data) return -1;
    s->state = (dap_tls13_client_state_t){.phase=DAP_TLS13_CONNECTING,
        .enc_reply=enc, .ctl_reply=ctl, .stream_data=data, .arg=arg};
    return dap_wss_dap_init(&s->wire, false, dap_tls13_client_dispatch,
                            &s->state, write, write_arg);
}

static int dap_wss_client_session_send(dap_wss_client_session_t *s,
    uint8_t type, const void *data, size_t size)
{
    if (!s) return -1;
    if (type == DAP_TLS13_ENC_REQUEST && s->state.phase == DAP_TLS13_CONNECTING)
        s->state.phase = DAP_TLS13_WAIT_ENC;
    else if (type == DAP_TLS13_CTL_REQUEST && s->state.phase == DAP_TLS13_ENC_DONE)
        s->state.phase = DAP_TLS13_WAIT_CTL;
    else if (!(type == DAP_TLS13_STREAM && s->state.phase == DAP_TLS13_STREAMING))
        return -1;
    if (dap_wss_dap_send(&s->wire, type, data, size)) {
        s->state.phase = DAP_TLS13_CLOSED;
        return -1;
    }
    return 0;
}

static void dap_wss_client_session_clear(dap_wss_client_session_t *s)
{
    if (!s) return;
    dap_wss_dap_clear(&s->wire);
    memset(&s->state, 0, sizeof(s->state));
    s->state.phase = DAP_TLS13_CLOSED;
}
#endif
