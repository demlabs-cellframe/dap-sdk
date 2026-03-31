/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * dap_client_trans_ctx - Minimal client IO context for trans_ctx._inheritor
 *
 * Provides just enough identity to navigate from IO callbacks back to the
 * owning client and FSM. All crypto, protocol state, and IO resources
 * live in dap_net_trans_ctx_t (owned by FSM) and dap_client_fsm_t.
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
#include "dap_common.h"
#include "dap_time.h"
#include "dap_events_socket.h"
#include "uthash.h"

typedef struct dap_client dap_client_t;

/**
 * @brief Minimal client IO context stored in trans_ctx._inheritor.
 *
 * Only holds identity for async callback navigation.
 * Navigation: esocket -> stream -> trans_ctx -> _inheritor (this) -> client -> FSM
 */
typedef struct dap_client_trans_ctx {
    uint64_t uuid;
    dap_client_t *client;
    uint64_t fsm_uuid;
    uint32_t fsm_thread_idx;
    dap_time_t ts_last_active;
    UT_hash_handle hh;
} dap_client_trans_ctx_t;

typedef struct dap_client_pkt_queue_elm {
    char ch_id;
    uint8_t type;
    size_t data_size;
    byte_t data[];
} dap_client_pkt_queue_elm_t;

int dap_client_trans_ctx_init(void);
void dap_client_trans_ctx_deinit(void);

dap_client_trans_ctx_t *dap_client_trans_ctx_find(uint64_t a_uuid);
void dap_client_trans_ctx_register(dap_client_trans_ctx_t *a_ctx);
void dap_client_trans_ctx_unregister(dap_client_trans_ctx_t *a_ctx);

void dap_client_trans_ctx_new(dap_client_trans_ctx_t *a_ctx);

void dap_client_trans_ctx_get_stream_callbacks(dap_events_socket_callbacks_t *a_callbacks);

void dap_client_trans_ctx_queue_add(dap_client_trans_ctx_t *a_ctx,
                                     const char a_ch_id, uint8_t a_type,
                                     void *a_data, size_t a_data_size);
int dap_client_trans_ctx_queue_clear(dap_client_trans_ctx_t *a_ctx);

void dap_client_trans_ctx_clean_unsafe(dap_client_trans_ctx_t *a_ctx);
void dap_client_trans_ctx_delete_unsafe(dap_client_trans_ctx_t *a_ctx);
