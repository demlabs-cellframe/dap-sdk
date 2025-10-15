/*
* Authors:
* Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2025
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

#include "dap_stream_event.h"
#include "dap_common.h"
#include <pthread.h>

#define LOG_TAG "dap_stream_event"

// ============================================================================
// Stream Event Callbacks Registry (Inversion of Control Pattern)
// ============================================================================

static dap_stream_event_add_callback_t s_stream_add_callback = NULL;
static dap_stream_event_replace_callback_t s_stream_replace_callback = NULL;
static dap_stream_event_delete_callback_t s_stream_delete_callback = NULL;
static void *s_stream_callbacks_user_data = NULL;
static pthread_rwlock_t s_stream_callbacks_lock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief Register stream event callbacks
 * @param a_add_cb Callback for stream add event
 * @param a_replace_cb Callback for stream replace event
 * @param a_delete_cb Callback for stream delete event
 * @param a_user_data User data passed to callbacks
 * @return 0 on success, negative on error
 */
int dap_stream_event_callbacks_register(dap_stream_event_add_callback_t a_add_cb,
                                        dap_stream_event_replace_callback_t a_replace_cb,
                                        dap_stream_event_delete_callback_t a_delete_cb,
                                        void *a_user_data)
{
    pthread_rwlock_wrlock(&s_stream_callbacks_lock);
    
    if (s_stream_add_callback) {
        log_it(L_WARNING, "Stream event callbacks already registered, replacing");
    }
    
    s_stream_add_callback = a_add_cb;
    s_stream_replace_callback = a_replace_cb;
    s_stream_delete_callback = a_delete_cb;
    s_stream_callbacks_user_data = a_user_data;
    
    pthread_rwlock_unlock(&s_stream_callbacks_lock);
    
    log_it(L_INFO, "Stream event callbacks registered");
    return 0;
}

/**
 * @brief Unregister stream event callbacks
 */
void dap_stream_event_callbacks_unregister(void)
{
    pthread_rwlock_wrlock(&s_stream_callbacks_lock);
    s_stream_add_callback = NULL;
    s_stream_replace_callback = NULL;
    s_stream_delete_callback = NULL;
    s_stream_callbacks_user_data = NULL;
    pthread_rwlock_unlock(&s_stream_callbacks_lock);
    
    log_it(L_INFO, "Stream event callbacks unregistered");
}

/**
 * @brief Notify about stream add event
 * @param a_addr Stream node address
 * @param a_is_uplink Is uplink flag
 */
void dap_stream_event_notify_add(dap_stream_node_addr_t *a_addr, bool a_is_uplink)
{
    pthread_rwlock_rdlock(&s_stream_callbacks_lock);
    if (s_stream_add_callback) {
        s_stream_add_callback(a_addr, a_is_uplink, s_stream_callbacks_user_data);
    }
    pthread_rwlock_unlock(&s_stream_callbacks_lock);
}

/**
 * @brief Notify about stream replace event
 * @param a_addr Stream node address
 * @param a_is_uplink Is uplink flag
 */
void dap_stream_event_notify_replace(dap_stream_node_addr_t *a_addr, bool a_is_uplink)
{
    pthread_rwlock_rdlock(&s_stream_callbacks_lock);
    if (s_stream_replace_callback) {
        s_stream_replace_callback(a_addr, a_is_uplink, s_stream_callbacks_user_data);
    }
    pthread_rwlock_unlock(&s_stream_callbacks_lock);
}

/**
 * @brief Notify about stream delete event
 * @param a_addr Stream node address
 */
void dap_stream_event_notify_delete(dap_stream_node_addr_t *a_addr)
{
    pthread_rwlock_rdlock(&s_stream_callbacks_lock);
    if (s_stream_delete_callback) {
        s_stream_delete_callback(a_addr, s_stream_callbacks_user_data);
    }
    pthread_rwlock_unlock(&s_stream_callbacks_lock);
}

