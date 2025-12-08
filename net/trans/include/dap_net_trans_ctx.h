#pragma once

#include "dap_events_socket.h"
#include "dap_enc_key.h"
#include "dap_net_trans.h"

typedef struct dap_net_trans_ctx {
    dap_events_socket_t *esocket; // Socket specific to this connection (e.g. TCP) or reference to shared (UDP)
    struct dap_net_trans *trans; // Pointer to shared trans configuration
    
    // Encryption ctx
    dap_enc_key_t *session_key;
    char *session_key_id;
    uint32_t uplink_protocol_version;
    uint32_t remote_protocol_version;
    
    // Pending callbacks
    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_trans_session_cb_t session_create_cb;
} dap_net_trans_ctx_t;