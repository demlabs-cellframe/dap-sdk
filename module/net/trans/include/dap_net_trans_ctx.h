#pragma once

#include "dap_events_socket.h"
#include "dap_enc_key.h"
#include "dap_net_trans.h"

// Forward declarations
typedef struct dap_stream dap_stream_t;
typedef struct dap_http_client dap_http_client_t;

typedef struct dap_net_trans_ctx {
    // Esocket reference - CRITICAL ARCHITECTURE:
    // For thread-safe access, ALWAYS check if dap_worker_get_current() == esocket_worker
    // If different worker, use UUID-based access (_mt methods)
    dap_events_socket_t *esocket; // UNSAFE: Only access in esocket's worker context!
    dap_events_socket_uuid_t esocket_uuid; // SAFE: UUID for cross-thread references
    dap_worker_t *esocket_worker; // Worker that owns the esocket
    
    struct dap_net_trans *trans; // Pointer to shared trans configuration
    dap_stream_t *stream;        // Back-reference to owning stream
    dap_http_client_t *http_client; // HTTP client (for HTTP path cleanup, NULL for UDP/DNS)
    
    // Encryption ctx
    dap_enc_key_t *session_key;
    char *session_key_id;
    uint32_t uplink_protocol_version;
    uint32_t remote_protocol_version;
    
    // Pending callbacks
    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_trans_session_cb_t session_create_cb;
    bool session_create_sent;  // Duplicate protection: true if SESSION_CREATE already sent
    
    // Cached remote address info (snapshot at connection time, safe for cross-thread reads)
    char remote_addr_str[INET6_ADDRSTRLEN];
    uint16_t remote_port;

    // Trans-specific private data (e.g., UDP session context, client context)
    void *_inheritor;
} dap_net_trans_ctx_t;