/*
 * Authors:
 * Alexey V. Stratulat <alexey.stratulat@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe/cellframe-sdk
 * Copyright  (c) 2017-2020
 * All rights reserved.

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
#include "dap_http_simple.h"
#include "http_status_code.h"
#include "dap_strfuncs.h"
#include "dap_json_rpc_request.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_config.h"
#include "dap_hash.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef enum dap_json_rpc_version{
    RPC_VERSION_1
}dap_json_rpc_version_t;

int dap_json_rpc_init(dap_server_t* a_http_server, dap_config_t *a_config);
void dap_json_rpc_deinit();

/*
 * Standalone JSON-RPC service on the plain dap HTTP server - a separate listener with its own
 * configuration section, next to the CLI port and the signed /exec_cmd endpoint.
 *
 * [rpc-server]
 *   enable=true                 start the service (default: off)
 *   listen-address=[0.0.0.0:12400]
 *   allowed_cmd_control=true    true  - only the commands in allowed_cmd are callable from
 *                                        non-loopback addresses, everything is callable from
 *                                        loopback (127.0.0.1 / ::1, default)
 *                               false - the whole command set is callable by anyone (open
 *                                        endpoint, expose only behind your own gatekeeper)
 *   allowed_cmd=[net,version]   the public command list
 *   max_request_size=1048576    request body cap in bytes
 *
 * Requests are plain JSON-RPC over HTTP (no encryption/signature handshake, which is what makes it
 * fast): POST a JSON object with "method" (the command name, as on the CLI) and its "params", e.g.
 *   curl -s -H 'Content-Type: application/json' -d '{"method":"version","params":[],"id":1}' URL
 * A GET without a body answers the command index - every registered command with its doc and
 * whether this endpoint would run it for a non-loopback caller. GET /health reports readiness the
 * same way the CLI port does (200/503 + Retry-After). Execution goes through the same
 * inflight/heavy backpressure gate as the CLI port and the signed path, and the per-source budget
 * is shared with them, so a busy node answers 429/503 instead of queueing.
 */
int dap_json_rpc_service_init(dap_config_t *a_config);
void dap_json_rpc_service_deinit(void);
void dap_json_rpc_http_plain_proc(dap_http_simple_t *a_http_simple, void *a_arg);
// Whether a command name may be run by a restricted (non-loopback) caller of an endpoint whose
// public list is a_allowed_cmds (NULL-terminated; NULL or empty = nothing is public).
bool dap_json_rpc_method_is_public(const char *a_method, const char **a_allowed_cmds);
void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg);
void dap_json_rpc_add_proc_http(struct dap_http_server*sh, const char *URL);
bool dap_check_node_pkey_in_map(dap_hash_fast_t *a_pkey);
bool dap_json_rpc_exec_cmd_inited();
dap_client_http_callback_error_t * dap_json_rpc_error_callback();
bool dap_json_rpc_get_int64(struct json_object *a_json, const char *a_key, int64_t *a_out);
bool dap_json_rpc_get_uint64(struct json_object *a_json, const char *a_key, uint64_t *a_out);
const char* dap_json_rpc_get_text(struct json_object *a_json, const char *a_key);

#ifdef __cplusplus
}
#endif
