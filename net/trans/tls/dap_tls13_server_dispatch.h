/* Server DAP control dispatch shared by TLS13 socket integration/tests.
 * Transport framing, certificate policy and payment logic remain separate. */
#ifndef DAP_TLS13_SERVER_DISPATCH_H
#define DAP_TLS13_SERVER_DISPATCH_H
#include "dap_tls13_frame.h"

typedef enum dap_tls13_server_phase {
    DAP_TLS13_SERVER_ENC, DAP_TLS13_SERVER_CTL,
    DAP_TLS13_SERVER_STREAM, DAP_TLS13_SERVER_CLOSED
} dap_tls13_server_phase_t;
/* Handler output is malloc-owned. CTL handler must create/bind the DAP stream
 * before returning success; failures must not dispatch later STREAM frames. */
typedef int (*dap_tls13_control_fn)(void *, const void *, size_t, void **, size_t *);
typedef int (*dap_tls13_send_fn)(void *, uint8_t, const void *, size_t);
typedef int (*dap_tls13_data_fn)(void *, const void *, size_t);
typedef struct dap_tls13_server_dispatch {
    dap_tls13_server_phase_t phase;
    dap_tls13_control_fn enc, ctl;
    dap_tls13_send_fn send;
    dap_tls13_data_fn data;
    void *arg;
} dap_tls13_server_dispatch_t;

static int dap_tls13_server_dispatch_frame(void *arg, uint8_t type,
                                         const void *payload, size_t size)
{
    dap_tls13_server_dispatch_t *s = arg;
    if (!s || !payload || !size || s->phase == DAP_TLS13_SERVER_CLOSED) return -1;
    if (s->phase == DAP_TLS13_SERVER_STREAM && type == DAP_TLS13_STREAM) {
        if (s->data && !s->data(s->arg, payload, size)) return 0;
        s->phase = DAP_TLS13_SERVER_CLOSED;
        return -1;
    }
    dap_tls13_control_fn handler = NULL;
    uint8_t response_type = 0;
    dap_tls13_server_phase_t next;
    if (s->phase == DAP_TLS13_SERVER_ENC && type == DAP_TLS13_ENC_REQUEST) {
        handler = s->enc; response_type = DAP_TLS13_ENC_REPLY;
        next = DAP_TLS13_SERVER_CTL;
    } else if (s->phase == DAP_TLS13_SERVER_CTL && type == DAP_TLS13_CTL_REQUEST) {
        handler = s->ctl; response_type = DAP_TLS13_CTL_REPLY;
        next = DAP_TLS13_SERVER_STREAM;
    } else goto fail;
    if (!handler || !s->send) goto fail;
    void *response = NULL;
    size_t length = 0;
    int rc = handler(s->arg, payload, size, &response, &length);
    if (rc || !response || !length || length > DAP_TLS13_FRAME_MAX) {
        free(response);
        goto fail;
    }
    rc = s->send(s->arg, response_type, response, length);
    free(response);
    if (rc) goto fail;
    s->phase = next;
    return 0;
fail:
    s->phase = DAP_TLS13_SERVER_CLOSED;
    return -1;
}
#endif
