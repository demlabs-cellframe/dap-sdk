#pragma once

#include "dap_events_socket.h"
#include "dap_stream.h"

// Esocket callbacks for stream-based transports
// Transport modules assign these to their dap_server/dap_events_socket callbacks

void dap_stream_esocket_read_cb(dap_events_socket_t *a_esocket, void *a_arg);
bool dap_stream_esocket_write_cb(dap_events_socket_t *a_esocket, void *a_arg);
void dap_stream_esocket_delete_cb(dap_events_socket_t *a_esocket, void *a_arg);
void dap_stream_esocket_error_cb(dap_events_socket_t *a_esocket, int a_error);
void dap_stream_esocket_worker_assign_cb(dap_events_socket_t *a_esocket, dap_worker_t *a_worker);
void dap_stream_esocket_worker_unassign_cb(dap_events_socket_t *a_esocket, dap_worker_t *a_worker);

// Stream states update — used by transport modules after setup
void dap_stream_states_update(dap_stream_t *a_stream);
