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
#include "dap_json.h"

typedef int (*dap_cli_server_cmd_callback_ex_t)(int argc, char ** argv, void *arg_func, void **a_str_reply, int a_version);
typedef int (*dap_cli_server_cmd_callback_t)(int argc, char ** argv, void **a_str_reply, int a_version);
typedef void (*dap_cli_server_cmd_stat_callback_t)(int16_t a_cmd_num, int64_t a_call_time);  // use to statistic collect

typedef void (*dap_cli_server_override_log_cmd_callback_t)(const char*);

// Callback for dynamic HTTP header generation
typedef char* (*dap_cli_server_http_header_callback_t)(void);

typedef struct dap_cli_server_http_header {
    char *name;  // Header name (e.g., "Node-Version")
    char *value; // Static value (if callback is NULL)
    dap_cli_server_http_header_callback_t callback; // Dynamic value generator (if not NULL)
    struct dap_cli_server_http_header *next;
} dap_cli_server_http_header_t;

typedef struct dap_cli_server_cmd_override{
    /* use it if you want to prevent logging of some sensetive data */
    dap_cli_server_override_log_cmd_callback_t log_cmd_call;
} dap_cli_server_cmd_override_t;

// Extended command flags
typedef struct dap_cli_server_cmd_flags {
    bool is_json_rpc;        // Is this a JSON-RPC command
    bool is_async;          // Is this an asynchronous command
    bool requires_auth;      // Does this command require authentication
    bool is_deprecated;      // Is this command deprecated
    bool is_experimental;    // Is this command experimental
} dap_cli_server_cmd_flags_t;

// Extended command parameters
typedef struct dap_cli_server_cmd_params {
    const char *name;                           // Command name
    dap_cli_server_cmd_callback_t func;         // Command callback function
    const char *doc;                           // Documentation
    int16_t id;                               // Command ID
    const char *doc_ex;                       // Extended documentation
    dap_cli_server_cmd_override_t overrides;  // Command overrides
    dap_cli_server_cmd_flags_t flags;         // Command flags
} dap_cli_server_cmd_params_t;

typedef struct dap_cli_cmd{
    char name[32]; /* User printable name of the function. */
    union {
        dap_cli_server_cmd_callback_t func; /* Function to call to do the job. */
        dap_cli_server_cmd_callback_ex_t func_ex; /* Function with additional arg to call to do the job. */
    };
    void *arg_func; /* additional argument of function*/
    char *doc; /* Documentation for this function.  */
    char *doc_ex; /* Full documentation for this function.  */
    dap_cli_server_cmd_override_t overrides; /* Used to change default behaviour */
    dap_cli_server_cmd_flags_t flags; /* Command flags */
    int16_t id;
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

dap_cli_cmd_t *dap_cli_server_cmd_add(const char *a_name, dap_cli_server_cmd_callback_t a_func, const char *a_doc, int16_t a_id, const char *a_doc_ex);
dap_cli_cmd_t *dap_cli_server_cmd_add_ext(const dap_cli_server_cmd_params_t *a_params);
DAP_PRINTF_ATTR(2, 3) void dap_cli_server_cmd_set_reply_text(void **a_str_reply, const char *str, ...);
int dap_cli_server_cmd_find_option_val( char** argv, int arg_start, int arg_end, const char *opt_name, const char **opt_value);
int dap_cli_server_cmd_check_option( char** argv, int arg_start, int arg_end, const char *opt_name);
void dap_cli_server_cmd_apply_overrides(const char * a_name, const dap_cli_server_cmd_override_t a_overrides);

dap_cli_cmd_t* dap_cli_server_cmd_get_first();
dap_cli_cmd_t* dap_cli_server_cmd_find(const char *a_name);

dap_cli_cmd_aliases_t *dap_cli_server_alias_add(dap_cli_cmd_t *a_cmd, const char *a_pre_cmd, const char *a_alias);
dap_cli_cmd_t *dap_cli_server_cmd_find_by_alias(const char *a_cli, char **a_append, char **a_ncmd);
int32_t dap_cli_get_cmd_thread_count();

//for json
char *dap_cli_cmd_exec(char *a_req_str);

/* For using clear json_rpc */
typedef void (handler_func_cli_t)(dap_json_t *a_params, dap_json_t *a_reply);

typedef struct dap_cli_handler_cl {
    const char *method;
    handler_func_cli_t *func;
    UT_hash_handle hh;
}dap_cli_handler_cl_t;

void dap_json_rpc_cli_handler_add(const char *a_method, handler_func_cli_t* a_fund);

//void dap_json_rpo_handler_cli_run(const char *a_method, dap_json_t *a_params, dap_json_t **obj_result);
void dap_cli_server_statistic_callback_add(dap_cli_server_cmd_stat_callback_t a_callback);

// HTTP header management functions
void dap_cli_server_http_header_add_static(const char *a_name, const char *a_value);
void dap_cli_server_http_header_add_dynamic(const char *a_name, dap_cli_server_http_header_callback_t a_callback);
void dap_cli_server_http_header_remove(const char *a_name);
void dap_cli_server_http_headers_clear(void);

void dap_cli_server_set_allowed_cmd_check(const char **a_cmd_array);
int dap_cli_server_get_version();