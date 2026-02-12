/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * dap_client_fsm - Client connection state machine
 *
 * Runs on a dedicated FSM thread pool (sticky binding: uuid % pool_size).
 * Dispatches IO operations to workers and processes results.
 * Heavy crypto operations (key generation, signing) run on FSM threads,
 * keeping worker threads free for IO only.
 *
 * Architecture:
 *   dap_client_t (public API, any thread)
 *       -> dap_client_fsm_t (FSM + crypto, dedicated FSM thread)
 *           -> dap_client_esocket_t (IO, worker thread)
 *
 * This file is part of DAP SDK the open source project
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "dap_client.h"
#include "dap_enc_key.h"
#include "dap_net_trans.h"
#include "uthash.h"

// Forward declarations
typedef struct dap_client_pvt dap_client_esocket_t;
typedef struct dap_worker dap_worker_t;

/**
 * @brief Client FSM - manages connection state machine
 *
 * Lifecycle: created by dap_client_new(), deleted by dap_client_delete_mt().
 * Thread model: FSM state is only modified from the bound FSM thread.
 * The esocket pointer is valid only while FSM is active.
 */
typedef struct dap_client_fsm {
    uint64_t uuid;                    // UTHASH key

    // References
    dap_client_t *client;             // Owning client
    dap_client_esocket_t *esocket;    // Current esocket (NULL if not connected)
    dap_worker_t *worker;             // Worker for IO dispatch

    // FSM state (modified only from bound FSM thread)
    dap_client_stage_t stage;
    dap_client_stage_status_t stage_status;
    dap_client_error_t last_error;
    dap_client_callback_t stage_status_done_callback;

    // Atomic copies for cross-thread reads (updated alongside stage/stage_status)
    _Atomic int stage_readable;
    _Atomic int stage_status_readable;

    // Reconnect state
    int reconnect_attempts;
    bool reconnect_pending;

    // Transport fallback
    dap_net_trans_type_t *tried_transports;
    size_t tried_transport_count;
    size_t tried_transport_capacity;

    // Crypto parameters (for key generation, not actual keys)
    dap_enc_key_type_t session_key_type;
    dap_enc_key_type_t session_key_open_type;
    size_t session_key_block_size;

    // FSM thread binding
    uint32_t fsm_thread_idx;         // = uuid % fsm_thread_count

    // Lifecycle
    bool is_removing;

    UT_hash_handle hh;
} dap_client_fsm_t;

// ===== Convenience macros =====

/**
 * @brief Get FSM from client pointer
 */
#define DAP_CLIENT_FSM(a) ((a) ? (dap_client_fsm_t *)(a)->_internal : NULL)

// ===== Module lifecycle =====

int dap_client_fsm_init(void);
void dap_client_fsm_deinit(void);

// ===== Instance lifecycle =====

/**
 * @brief Create new FSM for a client
 * @param a_client Owning client
 * @return New FSM or NULL on error
 */
dap_client_fsm_t *dap_client_fsm_new(dap_client_t *a_client);

/**
 * @brief Delete FSM and its esocket (unsafe - must be called from correct context)
 * @param a_fsm FSM to delete
 */
void dap_client_fsm_delete_unsafe(dap_client_fsm_t *a_fsm);

// ===== UUID lookup (thread-safe) =====

dap_client_fsm_t *dap_client_fsm_find(uint64_t a_uuid);
void dap_client_fsm_register(dap_client_fsm_t *a_fsm);
void dap_client_fsm_unregister(dap_client_fsm_t *a_fsm);

// ===== FSM operations =====

/**
 * @brief Start stage transition (dispatches to FSM thread)
 * @param a_fsm FSM instance
 * @param a_stage_target Target stage to reach
 * @param a_done_callback Callback when target stage is reached
 */
void dap_client_fsm_go_stage(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage_target,
                              dap_client_callback_t a_done_callback);

/**
 * @brief Begin a stage transaction (called on FSM thread)
 * @param a_fsm FSM instance
 * @param a_stage_next Next stage to transition to
 * @param a_done_callback Callback when stage completes
 */
void dap_client_fsm_stage_transaction_begin(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage_next,
                                             dap_client_callback_t a_done_callback);

/**
 * @brief Advance FSM to next stage towards target (called on FSM thread)
 * Used as stage_status_done_callback.
 */
void dap_client_fsm_advance(dap_client_t *a_client, void *a_arg);

// ===== FSM notification from worker =====

/**
 * @brief Notify FSM of IO completion (called from worker thread)
 * Schedules FSM processing on the bound FSM thread.
 * @param a_fsm_uuid UUID of the FSM to notify
 * @param a_fsm_thread_idx FSM thread index
 * @param a_status New stage status (DONE or ERROR)
 * @param a_error Error code (only meaningful if a_status == ERROR)
 */
void dap_client_fsm_notify(uint64_t a_fsm_uuid, uint32_t a_fsm_thread_idx,
                            dap_client_stage_status_t a_status, dap_client_error_t a_error);

/**
 * @brief Notify FSM that reconnect timer has fired (called from worker thread)
 */
void dap_client_fsm_notify_timer_fired(uint64_t a_fsm_uuid, uint32_t a_fsm_thread_idx);

// ===== Backward compatibility =====

// These redirect to FSM functions
#define dap_client_pvt_stage_transaction_begin(esocket, stage, cb) \
    dap_client_fsm_stage_transaction_begin(DAP_CLIENT_FSM((esocket)->client), (stage), (cb))

void dap_client_pvt_stage_fsm_advance(dap_client_t *a_client, void *a_arg);
