/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net

 This file is part of DAP (Distributed Applications Platform) the open source project

 DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 DAP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "dap_common.h"
#include "dap_http2_stream.h"
#include "dap_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_http2_stream_manager dap_http2_stream_manager_t;

typedef struct dap_http2_stream_info {
    uint64_t stream_id;
    dap_http2_stream_t *stream;
    time_t ts_created;
    time_t ts_last_activity;
    char *remote_addr;
    uint16_t remote_port;
    char *protocol;
    bool is_active;
    size_t bytes_sent;
    size_t bytes_received;
} dap_http2_stream_info_t;

typedef void (*dap_http2_stream_cleanup_cb_t)(dap_http2_stream_t *a_stream, void *a_arg);

// Global manager initialization
int dap_http2_stream_manager_init(void);
void dap_http2_stream_manager_deinit(void);

// Stream registration and management
int dap_http2_stream_manager_register(dap_http2_stream_t *a_stream);
int dap_http2_stream_manager_unregister(uint64_t a_stream_id);
dap_http2_stream_t *dap_http2_stream_manager_find(uint64_t a_stream_id);

// Stream lifecycle management
int dap_http2_stream_manager_set_cleanup_callback(dap_http2_stream_cleanup_cb_t a_callback, void *a_arg);
void dap_http2_stream_manager_cleanup_inactive(time_t a_max_idle_time);
void dap_http2_stream_manager_cleanup_all(void);

// Stream enumeration and statistics
size_t dap_http2_stream_manager_get_count(void);
size_t dap_http2_stream_manager_get_active_count(void);
dap_http2_stream_info_t *dap_http2_stream_manager_get_list(size_t *a_count);
void dap_http2_stream_manager_free_list(dap_http2_stream_info_t *a_list, size_t a_count);

// Stream search and filtering
dap_http2_stream_t **dap_http2_stream_manager_find_by_protocol(const char *a_protocol, size_t *a_count);
dap_http2_stream_t **dap_http2_stream_manager_find_by_addr(const char *a_addr, uint16_t a_port, size_t *a_count);
dap_http2_stream_t **dap_http2_stream_manager_find_inactive(time_t a_max_idle_time, size_t *a_count);

// Utility functions
void dap_http2_stream_manager_update_activity(uint64_t a_stream_id);
bool dap_http2_stream_manager_is_registered(uint64_t a_stream_id);

#ifdef __cplusplus
}
#endif 