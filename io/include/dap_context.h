/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * Demlabs Ltd.   https://demlabs.net
 * Copyright  (c) 2022
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <pthread.h>
#include <uthash.h>
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_proc_thread.h"
#include "dap_worker.h"

typedef struct dap_context dap_context_t;
 // Callback for specific client operations like custom init/deinit
typedef int (*dap_context_callback_t)(dap_context_t *a_context, void *a_arg);
typedef struct dap_context_msg_callback {
    dap_context_t *context;
    dap_context_callback_t callback;
    void *arg;
} dap_context_msg_callback_t;

typedef struct dap_context_msg_run{
    dap_context_t * context;
    dap_context_callback_t callback_started;
    dap_context_callback_t callback_stopped;
    int priority;
    int sched_policy;
    int cpu_id;
    int flags;
    void * callback_arg;
} dap_context_msg_run_t;

enum dap_context_type {
    DAP_CONTEXT_TYPE_WORKER,
    DAP_CONTEXT_TYPE_PROC_THREAD
};

typedef struct dap_context {
    uint32_t id;  // Context ID
    int cpu_id; // CPU id (if assigned)      
    pthread_t thread_id; // Thread id

    int type; // Context type

    // pthread-related fields
    pthread_cond_t started_cond; // Fires when thread started and pre-loop callback executes
    pthread_mutex_t started_mutex; // related with started_cond
    bool started;

    // Signal to exit
    bool signal_exit;
    // Flags
    bool is_running; // Is running
    uint32_t running_flags; // Flags passed for _run function

    // Inheritor
    void * _inheritor;  // dap_proc_thread_t, dap_worker_t
} dap_context_t;

// Waiting for started before exit _run function/
// ATTENTION: callback_started() executed just before exit a _run function

#define DAP_CONTEXT_FLAG_WAIT_FOR_STARTED  0x00000001
#define DAP_CONTEXT_FLAG_EXIT_IF_ERROR     0x00000100

// Usual policies
#define DAP_CONTEXT_POLICY_DEFAULT         0
#define DAP_CONTEXT_POLICY_TIMESHARING     1
// Real-time policies.
#define DAP_CONTEXT_POLICY_FIFO            2
#define DAP_CONTEXT_POLICY_ROUND_ROBIN     3

// If set DAP_CONTEXT_FLAG_WAIT_FOR_STARTED thats time for waiting for (in seconds)
#define DAP_CONTEXT_WAIT_FOR_STARTED_TIME   15

#ifdef DAP_OS_WINDOWS
#define DAP_CONTEXT_PRIORITY_NORMAL THREAD_PRIORITY_NORMAL
#define DAP_CONTEXT_PRIORITY_HIGH   THREAD_PRIORITY_HIGHEST
#define DAP_CONTEXT_PRIORITY_LOW    THREAD_PRIORITY_LOWEST
#else
#define DAP_CONTEXT_PRIORITY_NORMAL -1
#define DAP_CONTEXT_PRIORITY_HIGH   -2
#define DAP_CONTEXT_PRIORITY_LOW    -3
#endif

// pthread kernel object for current context pointer
extern pthread_key_t g_dap_context_pth_key;


/// Next functions are thread-safe
int dap_context_init(); // Init
void dap_context_deinit(); // Deinit

// New context create
dap_context_t * dap_context_new(int a_type);

// Run new context in dedicated thread.
// ATTENTION: after running the context nobody have to access it outside its own running thread
// Please use queues for access if need it
int dap_context_run(dap_context_t * a_context,int a_cpu_id, int a_sched_policy, int a_priority,uint32_t a_flags,
                    dap_context_callback_t a_callback_started,
                    dap_context_callback_t a_callback_stopped,
                    void * a_callback_arg );

void dap_context_stop_n_kill(dap_context_t * a_context);
void dap_context_wait(dap_context_t * a_context);

/**
 * @brief dap_context_current Get current context
 * @return Returns current context(if present, if not returns NULL)
 */
static inline dap_context_t * dap_context_current()
{
    return (dap_context_t*) pthread_getspecific(g_dap_context_pth_key);
}

/// ALL THIS FUNCTIONS ARE UNSAFE ! CALL THEM ONLY INSIDE THEIR OWN CONTEXT!!

int dap_context_add(dap_context_t * a_context, dap_events_socket_t * a_es );
int dap_context_remove( dap_events_socket_t * a_es);
int dap_context_poll_update(dap_events_socket_t * a_es);
dap_events_socket_t *dap_context_find(dap_context_t * a_context, dap_events_socket_uuid_t a_es_uuid );
dap_events_socket_t * dap_context_create_queue(dap_context_t * a_context, dap_events_socket_callback_queue_ptr_t a_callback);
dap_events_socket_t * dap_context_create_event(dap_context_t * a_context, dap_events_socket_callback_event_t a_callback);
dap_events_socket_t * dap_context_create_pipe(dap_context_t * a_context, dap_events_socket_callback_t a_callback, uint32_t a_flags);

// Create queues and inputs for them for all contexts
void dap_context_create_queues( dap_events_socket_callback_queue_ptr_t a_callback);
