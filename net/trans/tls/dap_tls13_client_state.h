/* Client protocol state, independent of socket/reactor and payment policy. */
#ifndef DAP_TLS13_CLIENT_STATE_H
#define DAP_TLS13_CLIENT_STATE_H
#include "dap_tls13_frame.h"
typedef enum dap_tls13_client_phase {
    DAP_TLS13_CONNECTING, DAP_TLS13_WAIT_ENC, DAP_TLS13_ENC_DONE,
    DAP_TLS13_WAIT_CTL, DAP_TLS13_STREAMING, DAP_TLS13_CLOSED
} dap_tls13_client_phase_t;
typedef int (*dap_tls13_reply_fn)(void *, const void *, size_t);
typedef struct dap_tls13_client_state {
    dap_tls13_client_phase_t phase;
    dap_tls13_reply_fn enc_reply, ctl_reply, stream_data;
    void *arg;
} dap_tls13_client_state_t;

/* Transitions happen before invoking callbacks: a synchronous callback can
 * queue the next request or close the connection. Never overwrite its state. */
static int dap_tls13_client_dispatch(void *arg, uint8_t type, const void *data, size_t size)
{
    dap_tls13_client_state_t *s = arg;
    if (!s || !data || !size || s->phase == DAP_TLS13_CLOSED) return -1;
    dap_tls13_reply_fn callback = NULL;
    if (type == DAP_TLS13_ENC_REPLY && s->phase == DAP_TLS13_WAIT_ENC) {
        callback = s->enc_reply;
        s->enc_reply = NULL;
        s->phase = DAP_TLS13_ENC_DONE;
    } else if (type == DAP_TLS13_CTL_REPLY && s->phase == DAP_TLS13_WAIT_CTL) {
        callback = s->ctl_reply;
        s->ctl_reply = NULL;
        s->phase = DAP_TLS13_STREAMING;
    } else if (type == DAP_TLS13_STREAM && s->phase == DAP_TLS13_STREAMING) {
        callback = s->stream_data;
    } else {
        s->phase = DAP_TLS13_CLOSED;
        return -1;
    }
    if (!callback) { s->phase = DAP_TLS13_CLOSED; return -1; }
    void *user = s->arg;
    /* Owner must defer destruction until frame dispatch returns. */
    int rc = callback(user, data, size);
    if (rc) s->phase = DAP_TLS13_CLOSED;
    return rc;
}
#endif
