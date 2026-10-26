/* DAP framing inside an established WebSocket. Upgrade/TLS/session policy
 * belongs to the owner. Both directions retain the existing TLS13 DAP frames. */
#ifndef DAP_WSS_DAP_H
#define DAP_WSS_DAP_H
#include "dap_tls13_frame.h"
#include "../websocket/include/dap_net_trans_websocket_stream.h"
#include "../websocket/dap_ws_frame_role.h"
#include "../websocket/dap_ws_close.h"
#include "dap_http_input.h"

typedef int (*dap_wss_wire_write_fn)(void *, const void *, size_t);
typedef struct dap_wss_dap {
    dap_http_input_t incoming;
    dap_tls13_frame_reader_t dap;
    dap_tls13_frame_callback_t dispatch;
    void *dispatch_arg;
    dap_wss_wire_write_fn write; /* Atomic enqueue: 0 accepted, nonzero fatal. */
    void *write_arg;
    bool server, fragmented, closed;
    size_t message_bytes;
} dap_wss_dap_t;

static int dap_wss_dap_init(dap_wss_dap_t *c, bool server,
    dap_tls13_frame_callback_t dispatch, void *dispatch_arg,
    dap_wss_wire_write_fn write, void *write_arg)
{
    if (!c || !dispatch || !write || c->incoming.data || c->dispatch) return -1;
    if (dap_http_input_init(&c->incoming, 2U * DAP_WS_RECEIVE_FRAME_MAX)) return -1;
    c->server=server; c->dispatch=dispatch; c->dispatch_arg=dispatch_arg;
    c->write=write; c->write_arg=write_arg;
    return 0;
}

static int dap_wss_dap_frame(dap_wss_dap_t *c, unsigned opcode,
                             const void *data, size_t size)
{
    if (!c || !c->write || c->closed || size > DAP_WS_RECEIVE_FRAME_MAX) return -1;
    uint8_t *wire=malloc(size+14);
    if (!wire) return -1;
    size_t n=0;
    int rc=dap_net_trans_websocket_build_frame(wire,size+14,(dap_ws_opcode_t)opcode,
                                              true,!c->server,data,size,&n);
    if (!rc) rc=c->write(c->write_arg,wire,n);
    free(wire);
    return rc;
}

static int dap_wss_dap_send(dap_wss_dap_t *c, uint8_t type, const void *data, size_t size)
{
    uint8_t header[6];
    if (!c || !data || dap_tls13_frame_header(header,type,size) ||
        size > DAP_WS_RECEIVE_FRAME_MAX-sizeof(header)) return -1;
    uint8_t *frame=malloc(size+sizeof(header));
    if (!frame) return -1;
    memcpy(frame,header,sizeof(header)); memcpy(frame+sizeof(header),data,size);
    int rc=dap_wss_dap_frame(c,2,frame,size+sizeof(header));
    free(frame);
    return rc;
}

static int dap_wss_dap_input(dap_wss_dap_t *c, const void *data, size_t size)
{
    if (!c || c->closed || dap_http_input_append(&c->incoming,data,size)) return -1;
    while (c->incoming.size) {
        const uint8_t *bytes=c->incoming.data;
        size_t n=c->incoming.size, payload_size=0, consumed=0;
        uint8_t *payload=NULL; dap_ws_opcode_t opcode; bool fin;
        int rc=dap_ws_frame_role_check(bytes,n,c->server);
        if (!rc) rc=dap_ws_frame_budget_check(bytes,n,DAP_WS_RECEIVE_FRAME_MAX);
        if (!rc) rc=dap_net_trans_websocket_parse_frame(bytes,n,&opcode,&fin,
                                                       &payload,&payload_size,&consumed);
        if (rc == -2) return 0;
        if (rc) return -1;
        if (dap_ws_fragment_accept(&c->fragmented,opcode,fin) ||
            dap_ws_message_budget(&c->message_bytes,opcode,fin,payload_size)) rc=-1;
        else if (opcode == 8) {
            if (!dap_ws_close_valid(payload,payload_size) || c->dap.header_size || c->dap.payload) rc=-1;
            else rc=dap_wss_dap_frame(c,8,payload,payload_size);
            c->closed=true;
        } else if (opcode == 9) rc=dap_wss_dap_frame(c,10,payload,payload_size);
        else if (opcode != 10)
            rc=dap_tls13_frame_feed(&c->dap,payload,payload_size,c->dispatch,c->dispatch_arg);
        free(payload);
        if (rc) return -1;
        dap_http_input_consume(&c->incoming,consumed);
        if (c->closed) return c->incoming.size ? -1 : 0;
    }
    return 0;
}

static void dap_wss_dap_clear(dap_wss_dap_t *c)
{
    if (!c) return;
    dap_http_input_clear(&c->incoming); dap_tls13_frame_clear(&c->dap);
    memset(c,0,sizeof(*c));
}
#endif
