/* WSS carrier + existing DAP server sequencing. The listener supplies the
 * existing ENC/CTL/stream handlers; no payment or key validation lives here. */
#ifndef DAP_WSS_SERVER_SESSION_H
#define DAP_WSS_SERVER_SESSION_H
#include "dap_wss_dap.h"
#include "dap_tls13_server_dispatch.h"

typedef struct dap_wss_server_session {
    dap_wss_dap_t wire;
    dap_tls13_server_dispatch_t dispatch;
    dap_tls13_control_fn enc, ctl;
    dap_tls13_data_fn data;
    void *handler_arg;
} dap_wss_server_session_t;

static int dap_wss_server_enc(void *arg, const void *data, size_t size, void **out, size_t *out_size)
{
    dap_wss_server_session_t *s = arg;
    return s->enc(s->handler_arg, data, size, out, out_size);
}
static int dap_wss_server_ctl(void *arg, const void *data, size_t size, void **out, size_t *out_size)
{
    dap_wss_server_session_t *s = arg;
    return s->ctl(s->handler_arg, data, size, out, out_size);
}
static int dap_wss_server_data(void *arg, const void *data, size_t size)
{
    dap_wss_server_session_t *s = arg;
    return s->data(s->handler_arg, data, size);
}
static int dap_wss_server_reply(void *arg, uint8_t type, const void *data, size_t size)
{
    dap_wss_server_session_t *s = arg;
    return dap_wss_dap_send(&s->wire, type, data, size);
}
static int dap_wss_server_session_init(dap_wss_server_session_t *s,
    dap_tls13_control_fn enc, dap_tls13_control_fn ctl, dap_tls13_data_fn data,
    void *handler_arg, dap_wss_wire_write_fn write, void *write_arg)
{
    if (!s || !enc || !ctl || !data || s->wire.dispatch) return -1;
    s->enc = enc; s->ctl = ctl; s->data = data; s->handler_arg = handler_arg;
    s->dispatch = (dap_tls13_server_dispatch_t){.enc=dap_wss_server_enc,
        .ctl=dap_wss_server_ctl, .data=dap_wss_server_data,
        .send=dap_wss_server_reply, .arg=s};
    return dap_wss_dap_init(&s->wire, true, dap_tls13_server_dispatch_frame,
                            &s->dispatch, write, write_arg);
}
static int dap_wss_server_session_write(dap_wss_server_session_t *s,
                                       const void *data, size_t size)
{
    if (!s || s->dispatch.phase != DAP_TLS13_SERVER_STREAM) return -1;
    return dap_wss_dap_send(&s->wire, DAP_TLS13_STREAM, data, size);
}
static void dap_wss_server_session_clear(dap_wss_server_session_t *s)
{
    if (!s) return;
    dap_wss_dap_clear(&s->wire);
    memset(s, 0, sizeof(*s));
}
#endif
