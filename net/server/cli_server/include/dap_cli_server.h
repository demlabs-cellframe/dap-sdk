/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe  https://cellframe.net
 * Copyright  (c) 2019-2022
 * All rights reserved.

 This file is part of Cellframe SDK

 Cellframe SDK is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Cellframe SDK is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any Cellframe SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "dap_events_socket.h"
#include "dap_common.h"
#include "dap_config.h"
#include "uthash.h"
#include "dap_json_rpc_response.h"

typedef int (*dap_cli_server_cmd_callback_func_json)(dap_json_rpc_response_t* response, char ** cmd_param, int cmd_cnt);
typedef int (*dap_cli_server_cmd_callback_ex_t)(int argc, char ** argv, void *arg_func, void **a_str_reply, int a_version);
typedef int (*dap_cli_server_cmd_callback_t)(int argc, char ** argv, void **a_str_reply, int a_version);

typedef void (*dap_cli_server_override_log_cmd_callback_t)(const char*);

typedef struct dap_cli_server_cmd_override{
    /* use it if you want to prevent logging of some sensetive data */
    dap_cli_server_override_log_cmd_callback_t log_cmd_call;
} dap_cli_server_cmd_override_t;

// Per-command backpressure classification. A service opts in by calling
// dap_cli_server_cmd_flags_set() for its own command name; the dispatcher
// (dap_cli_server.c) only ever looks at this bitmask and never hardcodes
// any particular service (DEX, xchange, stake, ...), since any service's
// command can turn out to be the one a crawler hits hard enough to exhaust
// node resources.
#define DAP_CLI_CMD_FLAG_HEAVY (1U << 0) /* subject to a tighter concurrency cap than regular commands */

typedef struct dap_cli_cmd{
    char name[32]; /* User printable name of the function. */
    union {
        dap_cli_server_cmd_callback_t func; /* Function to call to do the job. */
        dap_cli_server_cmd_callback_ex_t func_ex; /* Function with additional arg to call to do the job. */
    };
    dap_cli_server_cmd_callback_func_json func_rpc;
    void *arg_func_rpc;
    void *arg_func; /* additional argument of function*/
    char *doc; /* Documentation for this function.  */
    char *doc_ex; /* Full documentation for this function.  */
    dap_cli_server_cmd_override_t overrides; /* Used to change default behaviour */
    uint32_t flags; /* DAP_CLI_CMD_FLAG_* bitmask, see above */
    UT_hash_handle hh;
} dap_cli_cmd_t;

typedef struct dap_cli_cmd_aliases{
    char alias[32];
    char addition[32];
    dap_cli_cmd_t *standard_command;
    UT_hash_handle hh;
} dap_cli_cmd_aliases_t;


int dap_cli_server_init(bool a_debug_more, const char *a_cfg_section);
void dap_cli_server_deinit();

dap_cli_cmd_t *dap_cli_server_cmd_add(const char * a_name, dap_cli_server_cmd_callback_t a_func, dap_cli_server_cmd_callback_func_json a_func_rpc,
                                                                                                            const char *a_doc, const char *a_doc_ex);
bool dap_cli_server_cmd_remove(const char *a_name);
DAP_PRINTF_ATTR(2, 3) void dap_cli_server_cmd_set_reply_text(void **a_str_reply, const char *str, ...);
int dap_cli_server_cmd_find_option_val( char** argv, int arg_start, int arg_end, const char *opt_name, const char **opt_value);
int dap_cli_server_cmd_check_option( char** argv, int arg_start, int arg_end, const char *opt_name);
void dap_cli_server_cmd_apply_overrides(const char * a_name, const dap_cli_server_cmd_override_t a_overrides);
void dap_cli_server_cmd_flags_set(const char *a_name, uint32_t a_flags);

// Shared backpressure gate for every entry point that ends up calling
// dap_cli_cmd_exec() — the unix/tcp CLI port (dap_cli_server.c) and the
// signed HTTP /exec_cmd path (dap_json_rpc.c) both execute the exact same
// command set and must not bypass each other's concurrency limits. Acquire
// before running the command, release exactly once afterwards (both success
// and error paths). a_out_is_heavy tells the caller which counter to
// release; it is only meaningful when acquire returned true.
bool dap_cli_server_backpressure_acquire(const char *a_req_str, bool *a_out_is_heavy);
void dap_cli_server_backpressure_release(bool a_is_heavy);

// Cooperative cancellation for long-running HEAVY commands. Call this
// periodically (e.g. every few hundred/thousand iterations, not on every
// single one — it takes a mutex) from inside a hot loop in a command handler
// (DEX history/OHLCV building, ledger UTXO scans, block dump/list, tx_history
// -all, ...). Once it returns false the requesting client is already gone —
// the handler should unwind and return as soon as convenient instead of
// continuing to spend CPU/memory on a reply nobody will receive. This is an
// optimization on top of, not a replacement for, the point-in-time dead-client
// check done before a command starts: that one catches a client gone before
// execution; this one catches a client that leaves while execution is still
// in progress.
// Only meaningful while running on a CLI-port detached command thread
// (dap_cli_server.c), which is the only execution context with per-request
// client-liveness tracking today. Called from any other context (HTTP
// /exec_cmd on the shared proc-thread pool, direct dap_cli_cmd_exec() calls
// from tests or dap_app_cli) it always returns true — best-effort only, never
// a correctness requirement, so it is always safe to sprinkle into shared
// hot-path helper code regardless of caller.
bool dap_cli_server_client_is_alive(void);

dap_cli_cmd_t* dap_cli_server_cmd_get_first();
dap_cli_cmd_t* dap_cli_server_cmd_find(const char *a_name);

dap_cli_cmd_aliases_t *dap_cli_server_alias_add(dap_cli_cmd_t *a_cmd, const char *a_pre_cmd, const char *a_alias);
dap_cli_cmd_t *dap_cli_server_cmd_find_by_alias(const char *a_cli, char **a_append, char **a_ncmd);

//for json
int json_commands(const char * a_name);
char *dap_cli_cmd_exec(char *a_req_str);
int dap_cli_server_get_version();