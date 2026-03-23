/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * dap_client_esocket - Client IO/transport layer
 *
 * Manages IO resources (stream, esocket, encryption keys) on worker threads.
 * All _unsafe functions must be called from the owning worker thread.
 * Communicates with dap_client_fsm_t via notifications.
 *
 * This file is part of DAP SDK the open source project
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dap_client.h"
#include "dap_client_fsm.h"
#include "dap_stream.h"
#include "dap_events_socket.h"
#include "dap_cert.h"
#include "dap_ht.h"

typedef struct dap_enc_key dap_enc_key_t;

/**
 * @brief Client esocket context - manages IO, transport and cryptographic session
 *
 * Lives on a worker thread. All fields are accessed from the worker only.
 * FSM notification is sent via dap_client_fsm_notify() when IO completes.
 */
typedef struct dap_client_pvt {
    uint64_t uuid;               // UTHASH key
    dap_client_t *client;        // Owning client (for transport callbacks)

    // FSM reference (for sending notifications back)
    uint64_t fsm_uuid;
    uint32_t fsm_thread_idx;

    // IO resources
    dap_stream_t *stream;
    dap_stream_worker_t *stream_worker;
    dap_events_socket_t *stream_es;
    dap_worker_t *worker;

    // Cryptographic session
    dap_enc_key_type_t session_key_type;
    dap_enc_key_type_t session_key_open_type;
    size_t session_key_block_size;
    dap_enc_key_t *session_key_open; // Asymmetric key exchange
    dap_enc_key_t *session_key;      // Symmetric session key
    dap_enc_key_t *stream_key;       // Stream encryption key
    uint32_t stream_id;
    char *session_key_id;

    // Protocol
    uint32_t uplink_protocol_version;
    uint32_t remote_protocol_version;
    bool authorized;

    // Flags
    bool is_encrypted;
    bool is_encrypted_headers;
    bool is_close_session;
    bool is_closed_by_timeout;
    bool is_removing;
    dap_time_t ts_last_active;

    // Callbacks (for HTTP request/response)
    dap_client_callback_data_size_t request_response_callback;
    dap_client_callback_int_t request_error_callback;
    void *callback_arg;

    // Packet queue (for connect-on-demand)
    dap_list_t *pkt_queue;

    // Hash table entry
    dap_ht_handle_t hh;
} dap_client_esocket_t;

typedef struct dap_client_pkt_queue_elm {
    char ch_id;
    uint8_t type;
    size_t data_size;
    byte_t data[];
} dap_client_pkt_queue_elm_t;

/**
 * @brief Get esocket from client pointer (goes through FSM)
 */
#define DAP_CLIENT_ESOCKET(a) (DAP_CLIENT_FSM(a) ? DAP_CLIENT_FSM(a)->esocket : NULL)

// Module lifecycle
int dap_client_esocket_init(void);
void dap_client_esocket_deinit(void);

/**
 * @brief Get stream esocket callbacks for client-side stream connections.
 * These callbacks handle read/write/error/delete/connected for streaming stage.
 */
void dap_client_esocket_get_stream_callbacks(dap_events_socket_callbacks_t *a_callbacks);

// Instance lifecycle
void dap_client_esocket_new(dap_client_esocket_t *a_esocket_ctx);
void dap_client_esocket_delete_unsafe(dap_client_esocket_t *a_esocket_ctx);

/**
 * @brief Find client esocket by UUID (thread-safe)
 */
dap_client_esocket_t *dap_client_esocket_find(uint64_t a_uuid);
void dap_client_esocket_register(dap_client_esocket_t *a_esocket_ctx);
void dap_client_esocket_unregister(dap_client_esocket_t *a_esocket_ctx);

// Packet queue
void dap_client_esocket_queue_add(dap_client_esocket_t *a_esocket_ctx,
                                   const char a_ch_id, uint8_t a_type,
                                   void *a_data, size_t a_data_size);
int dap_client_esocket_queue_clear(dap_client_esocket_t *a_esocket_ctx);

/**
 * @brief Clean esocket internal resources (called on worker thread)
 * Cleans stream, keys, timers. Does not delete the esocket itself.
 */
void dap_client_esocket_clean_unsafe(dap_client_esocket_t *a_esocket_ctx);

// Backward compatibility macro aliases (names only)
#define dap_client_pvt_init          dap_client_esocket_init
#define dap_client_pvt_deinit        dap_client_esocket_deinit
#define dap_client_pvt_new           dap_client_esocket_new
#define dap_client_pvt_delete_unsafe dap_client_esocket_delete_unsafe
#define dap_client_pvt_queue_add     dap_client_esocket_queue_add
#define dap_client_pvt_queue_clear   dap_client_esocket_queue_clear
