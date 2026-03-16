#pragma once

#ifndef DAP_OS_WASM
#include "dap_events_socket.h"
#endif
#include "dap_enc_key.h"
#include "dap_net_trans.h"

typedef struct dap_stream dap_stream_t;
#ifndef DAP_OS_WASM
typedef struct dap_http_client dap_http_client_t;
#endif

typedef struct dap_net_trans_ctx {
#ifndef DAP_OS_WASM
    dap_events_socket_t *esocket;
    dap_events_socket_uuid_t esocket_uuid;
    dap_worker_t *esocket_worker;
#endif

    struct dap_net_trans *trans;
    dap_stream_t *stream;
#ifndef DAP_OS_WASM
    dap_http_client_t *http_client;
#endif

    dap_enc_key_t *session_key;
    char *session_key_id;
    uint32_t uplink_protocol_version;
    uint32_t remote_protocol_version;

    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_trans_session_cb_t session_create_cb;
    bool session_create_sent;

#ifndef DAP_OS_WASM
    char remote_addr_str[INET6_ADDRSTRLEN];
    uint16_t remote_port;
#endif

    void *_inheritor;
} dap_net_trans_ctx_t;