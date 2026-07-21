/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe  https://cellframe.net
 * Copyright  (c) 2026
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

#include "dap_events_socket.h"

/**
 * @brief Initialize RPC HTTP documentation serving
 * @param a_cfg_section Config section name (cli-server)
 * @return 0 on success, negative value if docs path is unavailable
 */
int dap_cli_http_docs_init(const char *a_cfg_section);

/**
 * @brief Release RPC HTTP documentation resources
 */
void dap_cli_http_docs_deinit(void);

/**
 * @brief Try to serve buffered HTTP GET request with RPC documentation
 * @param a_es Client socket with request data in buf_in
 * @param a_arg CLI command argument storage (freed when request is handled)
 * @return 0 if not a GET or request is incomplete, 1 if response was sent
 */
int dap_cli_http_docs_try_get(dap_events_socket_t *a_es, void **a_arg);
