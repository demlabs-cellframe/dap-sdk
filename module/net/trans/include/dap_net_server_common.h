/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * Common net server helpers (6.0 compatibility)
 */

#pragma once

#include "dap_server.h"
#include "dap_events_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add listen address to server with optional callbacks
 * Wrapper around dap_server_listen_addr_add for trans servers.
 * @param a_server Server instance
 * @param a_addr Address (e.g. "0.0.0.0")
 * @param a_port Port number
 * @param a_type Socket type (e.g. DESCRIPTOR_TYPE_SOCKET_LISTENING)
 * @param a_pre_worker_added Optional callback (unused, pass NULL)
 * @param a_arg Optional argument (unused, pass NULL)
 * @return 0 on success, non-zero on error
 */
int dap_net_server_listen_addr_add_with_callback(dap_server_t *a_server,
                                                const char *a_addr,
                                                uint16_t a_port,
                                                dap_events_desc_type_t a_type,
                                                void (*a_pre_worker_added)(void *),
                                                void *a_arg);

#ifdef __cplusplus
}
#endif
