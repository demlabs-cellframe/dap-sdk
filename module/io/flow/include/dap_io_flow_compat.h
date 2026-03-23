#pragma once

#include "dap_worker.h"
#include "dap_events_socket.h"

#ifndef dap_worker_exec_callback_on_sync
#define dap_worker_exec_callback_on_sync(w, cb, arg) dap_worker_exec_callback_on(w, cb, arg)
#endif

#ifndef dap_events_socket_remove_and_delete_mt
#define dap_events_socket_remove_and_delete_mt(w, uuid) dap_events_socket_remove_and_delete_unsafe(dap_events_socket_find_unsafe(uuid), false)
#endif
