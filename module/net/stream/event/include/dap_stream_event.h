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

#pragma once

#include <stdbool.h>
#include "dap_net_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stream Event Module
 * 
 * Provides event notification system for stream lifecycle events.
 * Uses Dependency Inversion Principle to break stream → link_manager dependency.
 * 
 * Architecture:
 * - stream module calls dap_stream_event_notify_*() when events occur
 * - link_manager (or other modules) register callbacks via dap_stream_event_callbacks_register()
 * - No direct dependency between stream and link_manager
 */

// ============================================================================
// Stream Event Callback Types
// ============================================================================

/**
 * @brief Callback for stream add event
 * @param a_addr Node address of added stream
 * @param a_is_uplink Whether stream is uplink
 * @param a_user_data User data passed during registration
 */
typedef void (*dap_stream_event_add_callback_t)(dap_stream_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);

/**
 * @brief Callback for stream replace event
 * @param a_addr Node address of replaced stream
 * @param a_is_uplink Whether stream is uplink
 * @param a_user_data User data passed during registration
 */
typedef void (*dap_stream_event_replace_callback_t)(dap_stream_node_addr_t *a_addr, bool a_is_uplink, void *a_user_data);

/**
 * @brief Callback for stream delete event
 * @param a_addr Node address of deleted stream
 * @param a_user_data User data passed during registration
 */
typedef void (*dap_stream_event_delete_callback_t)(dap_stream_node_addr_t *a_addr, void *a_user_data);

// ============================================================================
// Stream Event API
// ============================================================================

/**
 * @brief Register stream event callbacks
 * @param a_add_cb Callback for stream add event (can be NULL)
 * @param a_replace_cb Callback for stream replace event (can be NULL)
 * @param a_delete_cb Callback for stream delete event (can be NULL)
 * @param a_user_data User data passed to callbacks
 * @return 0 on success, negative on error
 */
int dap_stream_event_callbacks_register(dap_stream_event_add_callback_t a_add_cb,
                                        dap_stream_event_replace_callback_t a_replace_cb,
                                        dap_stream_event_delete_callback_t a_delete_cb,
                                        void *a_user_data);

/**
 * @brief Unregister stream event callbacks
 */
void dap_stream_event_callbacks_unregister(void);

/**
 * @brief Notify about stream add event
 * Called by stream module when new stream is added
 * @param a_addr Stream node address
 * @param a_is_uplink Is uplink flag
 */
void dap_stream_event_notify_add(dap_stream_node_addr_t *a_addr, bool a_is_uplink);

/**
 * @brief Notify about stream replace event
 * Called by stream module when stream is replaced
 * @param a_addr Stream node address
 * @param a_is_uplink Is uplink flag
 */
void dap_stream_event_notify_replace(dap_stream_node_addr_t *a_addr, bool a_is_uplink);

/**
 * @brief Notify about stream delete event
 * Called by stream module when stream is deleted
 * @param a_addr Stream node address
 */
void dap_stream_event_notify_delete(dap_stream_node_addr_t *a_addr);

#ifdef __cplusplus
}
#endif

