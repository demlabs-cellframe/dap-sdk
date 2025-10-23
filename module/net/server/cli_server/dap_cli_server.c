/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe  https://cellframe.net
 * Copyright  (c) 2019-2021
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

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#ifndef DAP_OS_WINDOWS
#include <poll.h>
#endif

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_list.h"
#include "dap_net.h"
#include "dap_cli_server.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_server.h"

#include "../json_rpc/include/dap_json_rpc_errors.h"
#include "../json_rpc/include/dap_json_rpc_request.h"
#include "../json_rpc/include/dap_json_rpc_response.h"



#define LOG_TAG "dap_cli_server"

#define MAX_CONSOLE_CLIENTS 16

static dap_server_t *s_cli_server = NULL;
static bool s_debug_cli = false;
static atomic_int_fast32_t s_cmd_thread_count = 0;
static bool s_allowed_cmd_control = false;
static const char **s_allowed_cmd_array = NULL;
static int s_cli_version = 1;

static dap_cli_cmd_t *cli_commands = NULL;
static dap_cli_cmd_aliases_t *s_command_alias = NULL;

static dap_cli_server_cmd_stat_callback_t s_stat_callback = NULL;

// HTTP headers list
static dap_cli_server_http_header_t *s_http_headers = NULL;
static pthread_rwlock_t s_http_headers_rwlock = PTHREAD_RWLOCK_INITIALIZER;

typedef struct cli_cmd_arg {
    dap_worker_t *worker;
    dap_events_socket_uuid_t es_uid;
    size_t buf_size;
    char *buf, status;

    time_t time_start;
} cli_cmd_arg_t;

static void* s_cli_cmd_exec(void *a_arg);
static char* s_generate_additional_headers(void);

static bool s_allowed_cmd_check(const char *a_buf) {
    if (!s_allowed_cmd_array)
        return false;
    dap_json_tokener_error_t jterr;
    const char *l_method;
    dap_json_t *jobj = dap_json_tokener_parse_verbose(a_buf, &jterr),
                *jobj_method = NULL;
    if ( jterr != DAP_JSON_TOKENER_SUCCESS ) 
        return log_it(L_ERROR, "Can't parse json command, error %s", dap_json_tokener_error_desc(jterr)), false;
    if ( dap_json_object_get_ex(jobj, "method", &jobj_method) )
        l_method = dap_json_object_get_string(jobj_method, NULL);
    else {
        log_it(L_ERROR, "Invalid command request, dump it");
        dap_json_object_free(jobj);
        return false;
    }

    bool l_allowed = !!dap_str_find( s_allowed_cmd_array, l_method );
    return debug_if(!l_allowed, L_ERROR, "Command %s is restricted", l_method), dap_json_object_free(jobj), l_allowed;
}

DAP_STATIC_INLINE void s_cli_cmd_schedule(dap_events_socket_t *a_es, void *a_arg) {
    cli_cmd_arg_t *l_arg = a_arg ? (cli_cmd_arg_t*)a_arg : DAP_NEW_Z(cli_cmd_arg_t);
    switch (l_arg->status) {
    case 0: {
        a_es->callbacks.arg = l_arg;
        ++l_arg->status;
    }
    case 1: {
        static const char l_content_len_str[] = "Content-Length: ";
        l_arg->buf = strstr((char*)a_es->buf_in, l_content_len_str);
        if ( !l_arg->buf || !strpbrk(l_arg->buf, "\r\n") )
            return;
        if (( l_arg->buf_size = (size_t)strtol(l_arg->buf + sizeof(l_content_len_str) - 1, NULL, 10) ))
            ++l_arg->status;
        else
            break;
    }
    case 2: { // Find header end and throw out header
        static const char l_head_end_str[] = "\r\n\r\n";
        char *l_hdr_end_token = strstr(l_arg->buf, l_head_end_str);
        if (!l_hdr_end_token)
            return;
        l_arg->buf = l_hdr_end_token + sizeof(l_head_end_str) - 1;
        ++l_arg->status;
    }
    case 3:
    default: {
        size_t l_hdr_len = (size_t)(l_arg->buf - (char*)a_es->buf_in);
        if ( a_es->buf_in_size < l_arg->buf_size + l_hdr_len )
            return;

        if (!(   
#ifdef DAP_OS_UNIX
            a_es->addr_storage.ss_family == AF_UNIX ||
#endif
            ( ((struct sockaddr_in*)&a_es->addr_storage)->sin_addr.s_addr == htonl(INADDR_LOOPBACK) && !s_allowed_cmd_control) ||
            (((struct sockaddr_in*)&a_es->addr_storage)->sin_addr.s_addr && s_allowed_cmd_control && s_allowed_cmd_check(l_arg->buf)))
        ) {
                dap_events_socket_write_f_unsafe(a_es, "HTTP/1.1 403 Forbidden\r\n");
                a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                return;
            }

        l_arg->buf = strndup(l_arg->buf, l_arg->buf_size);
        l_arg->worker = a_es->worker;
        l_arg->es_uid = a_es->uuid;
        l_arg->time_start = dap_nanotime_now();

        
        pthread_t l_tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&l_tid, &attr, s_cli_cmd_exec, l_arg);

        //dap_proc_thread_callback_add_pri(NULL, s_cli_cmd_exec, l_arg, DAP_QUEUE_MSG_PRIORITY_HIGH);
        a_es->buf_in_size = 0;
        a_es->callbacks.arg = NULL;
    } return;
    }

    dap_events_socket_write_f_unsafe(a_es, "HTTP/1.1 500 Internal Server Error\r\n");
    char *buf_dump = dap_dump_hex(a_es->buf_in, dap_min(a_es->buf_in_size, (size_t)65536));
    log_it(L_DEBUG, "Incomplete cmd request:\r\n%s", buf_dump);
    DAP_DELETE(buf_dump);
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

static void s_cli_cmd_delete(dap_events_socket_t *a_es, void UNUSED_ARG *a_arg) {
    DAP_DELETE(a_es->callbacks.arg);
}


/**
 * @brief dap_cli_server_init
 * @param a_debug_more
 * @param a_socket_path_or_address
 * @param a_port
 * @param a_permissions
 * @return
 */
int dap_cli_server_init(bool a_debug_more, const char *a_cfg_section)
{
    s_debug_cli = a_debug_more;
    dap_events_socket_callbacks_t l_callbacks = { .read_callback = s_cli_cmd_schedule, .delete_callback = s_cli_cmd_delete };
    if (!( s_cli_server = dap_server_new(a_cfg_section, NULL, &l_callbacks) )) {
        log_it(L_ERROR, "CLI server not initialized");
        return -2;
    }
    s_allowed_cmd_control = dap_config_get_item_bool_default(g_config, a_cfg_section, "allowed_cmd_control", s_allowed_cmd_control);
    log_it(L_INFO, "CLI server initialized");
    s_cli_version = dap_config_get_item_int32_default(g_config, "cli-server", "version", s_cli_version);
    log_it(L_INFO, "CLI server initialized with protocol version %d", s_cli_version);
    return 0;
}

/**
 * @brief dap_cli_server_deinit
 */
void dap_cli_server_deinit()
{
    dap_server_delete(s_cli_server);
}

/**
 * @brief s_cmd_add_ex
 * @param a_name
 * @param a_func
 * @param a_arg_func
 * @param a_doc
 * @param a_doc_ex
 */
DAP_STATIC_INLINE dap_cli_cmd_t *s_cmd_add_ex(const char * a_name, dap_cli_server_cmd_callback_ex_t a_func, void *a_arg_func, const char *a_doc, const char *a_doc_ex, int16_t a_id)
{
    dap_cli_cmd_t *l_cmd_item = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_cli_cmd_t, NULL);

    snprintf(l_cmd_item->name,sizeof (l_cmd_item->name),"%s",a_name);
    l_cmd_item->doc = strdup( a_doc);
    l_cmd_item->doc_ex = strdup( a_doc_ex);
    if (a_arg_func) {
        l_cmd_item->func_ex = a_func;
        l_cmd_item->arg_func = a_arg_func;
    } else {
        l_cmd_item->func = (dap_cli_server_cmd_callback_t )(void *)a_func;
    }
    l_cmd_item->id = a_id;
    // Initialize flags with default values
    memset(&l_cmd_item->flags, 0, sizeof(l_cmd_item->flags));
    HASH_ADD_STR(cli_commands,name,l_cmd_item);
    log_it(L_DEBUG,"Added command %s",l_cmd_item->name);
    return l_cmd_item;
}

/**
 * @brief dap_cli_server_cmd_add
 * @param a_name
 * @param a_func
 * @param a_doc
 * @param a_doc_ex
 */
dap_cli_cmd_t *dap_cli_server_cmd_add(const char * a_name, dap_cli_server_cmd_callback_t a_func, const char *a_doc, int16_t a_id, const char *a_doc_ex)
{
    return s_cmd_add_ex(a_name, (dap_cli_server_cmd_callback_ex_t)(void *)a_func, NULL, a_doc, a_doc_ex, a_id);
}

/**
 * @brief dap_cli_server_cmd_add_ext
 * Extended command addition with flags and parameters
 * @param a_params Extended command parameters structure
 * @return Pointer to created command or NULL on error
 */
dap_cli_cmd_t *dap_cli_server_cmd_add_ext(const dap_cli_server_cmd_params_t *a_params)
{
    if (!a_params || !a_params->name || !a_params->func) {
        log_it(L_ERROR, "Invalid parameters for dap_cli_server_cmd_add_ext");
        return NULL;
    }

    dap_cli_cmd_t *l_cmd_item = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_cli_cmd_t, NULL);

    snprintf(l_cmd_item->name, sizeof(l_cmd_item->name), "%s", a_params->name);
    l_cmd_item->doc = strdup(a_params->doc ? a_params->doc : "");
    l_cmd_item->doc_ex = strdup(a_params->doc_ex ? a_params->doc_ex : "");
    l_cmd_item->func = a_params->func;
    l_cmd_item->id = a_params->id;
    l_cmd_item->overrides = a_params->overrides;
    l_cmd_item->flags = a_params->flags;

    HASH_ADD_STR(cli_commands, name, l_cmd_item);
    log_it(L_DEBUG, "Added extended command %s (JSON-RPC: %s)", 
           l_cmd_item->name, l_cmd_item->flags.is_json_rpc ? "yes" : "no");
    
    return l_cmd_item;
}



/**
 * @brief dap_cli_server_cmd_set_reply_text
 * Write text to reply string
 * @param str_reply
 * @param str
 * @param ...
 */
void dap_cli_server_cmd_set_reply_text(void **a_str_reply, const char *str, ...)
{
    char **l_str_reply = (char **)a_str_reply;
    if (l_str_reply) {
        if (*l_str_reply) {
            DAP_DELETE(*l_str_reply);
            *l_str_reply = NULL;
        }
        va_list args;
        va_start(args, str);
        *l_str_reply = dap_strdup_vprintf(str, args);
        va_end(args);
    }
}

/**
 * @brief dap_cli_server_cmd_check_option
 * @param argv
 * @param arg_start
 * @param arg_end
 * @param opt_name
 * @return
 */
int dap_cli_server_cmd_check_option( char** argv, int arg_start, int arg_end, const char *opt_name)
{
    int arg_index = arg_start;
    const char *arg_string;

    while(arg_index < arg_end)
    {
        char * l_argv_cur = argv[arg_index];
        arg_string = l_argv_cur;
        // find opt_name
        if(arg_string && opt_name && arg_string[0] && opt_name[0] && !strcmp(arg_string, opt_name)) {
                return arg_index;
        }
        arg_index++;
    }
    return -1;
}


/**
 * @brief dap_cli_server_cmd_find_option_val
 * return index of string in argv, or 0 if not found
 * @param argv
 * @param arg_start
 * @param arg_end
 * @param opt_name
 * @param opt_value
 * @return int
 */
int dap_cli_server_cmd_find_option_val( char** argv, int arg_start, int arg_end, const char *opt_name, const char **opt_value)
{
    assert(argv);
    int arg_index = arg_start;
    const char *arg_string;
    int l_ret_pos = 0;

    while(arg_index < arg_end)
    {
        char * l_argv_cur = argv[arg_index];
        arg_string = l_argv_cur;
        // find opt_name
        if(arg_string && opt_name && arg_string[0] && opt_name[0] && !strcmp(arg_string, opt_name)) {
            // find opt_value
            if(opt_value) {
                arg_string = argv[++arg_index];
                if(arg_string) {
                    *opt_value = arg_string;
                    return arg_index;
                }
                // for case if opt_name exist without value
                else
                    l_ret_pos = arg_index;
            }
            else
                // need only opt_name
                return arg_index;
        }
        arg_index++;
    }
    return l_ret_pos;
}


/**
 * @brief dap_cli_server_cmd_apply_overrides
 *
 * @param a_name
 * @param a_overrides
 */
void dap_cli_server_cmd_apply_overrides(const char * a_name, const dap_cli_server_cmd_override_t a_overrides)
{
    dap_cli_cmd_t *l_cmd_item = dap_cli_server_cmd_find(a_name);
    if(l_cmd_item)
        l_cmd_item->overrides = a_overrides;
}

/**
 * @brief dap_cli_server_cmd_get_first
 * @return
 */
dap_cli_cmd_t* dap_cli_server_cmd_get_first()
{
    return cli_commands;
}

/**
 * @brief dap_cli_server_cmd_find
 * @param a_name
 * @return
 */
dap_cli_cmd_t* dap_cli_server_cmd_find(const char *a_name)
{
    dap_cli_cmd_t *l_cmd_item = NULL;
    HASH_FIND_STR(cli_commands,a_name,l_cmd_item);
    return l_cmd_item;
}

dap_cli_cmd_aliases_t *dap_cli_server_alias_add(dap_cli_cmd_t *a_cmd, const char *a_pre_cmd, const char *a_alias)
{
    if (!a_alias || !a_cmd)
        return NULL;
    dap_cli_cmd_aliases_t *l_alias = DAP_NEW_Z(dap_cli_cmd_aliases_t);
    size_t l_alias_size = dap_strlen(a_alias);
    memcpy(l_alias->alias, a_alias, l_alias_size);
    if (a_pre_cmd) {
        size_t l_addition_size = dap_strlen(a_pre_cmd);
        memcpy(l_alias->addition, a_pre_cmd, l_addition_size);
    }
    l_alias->standard_command = a_cmd;
    HASH_ADD_STR(s_command_alias, alias, l_alias);
    return l_alias;
}

dap_cli_cmd_t *dap_cli_server_cmd_find_by_alias(const char *a_alias, char **a_append, char **a_ncmd)
{
    dap_cli_cmd_aliases_t *l_alias = NULL;
    HASH_FIND_STR(s_command_alias, a_alias, l_alias);
    if (!l_alias)
        return NULL;
    *a_append = l_alias->addition[0] ? dap_strdup(l_alias->addition) : NULL;
    *a_ncmd = dap_strdup(l_alias->standard_command->name);
    return l_alias->standard_command;
}

static void *s_cli_cmd_exec(void *a_arg) {
    atomic_fetch_add(&s_cmd_thread_count, 1);
    cli_cmd_arg_t *l_arg = (cli_cmd_arg_t*)a_arg;
    char *l_ret = dap_cli_cmd_exec(l_arg->buf);
    char *l_additional_headers = s_generate_additional_headers();
    char *l_full_ret = dap_strdup_printf("HTTP/1.1 200 OK\r\n"
                                         "Content-Length: %"DAP_UINT64_FORMAT_U"\r\n"
                                         "Processing-Time: %zu\r\n"
                                         "%s\r\n"
                                         "%s", 
                                         dap_strlen(l_ret), 
                                         dap_nanotime_now() - l_arg->time_start, 
                                         l_additional_headers,
                                         l_ret);
    dap_events_socket_write(l_arg->worker, l_arg->es_uid, l_full_ret, dap_strlen(l_full_ret));
    DAP_DELETE(l_additional_headers);
    DAP_DELETE(l_ret);
    // TODO: pagination and output optimizations
    DAP_DEL_MULTY(l_arg->buf, l_full_ret, l_arg);
    atomic_fetch_sub(&s_cmd_thread_count, 1);
    return NULL;
}

int json_commands(const char * a_name) {
    static const char* long_cmd[] = {
            "tx_history",
            "wallet",
            "mempool",
            "ledger",
            "tx_create",
            "tx_create_json",
            "mempool_add",
            "tx_verify",
            "tx_cond_create",
            "tx_cond_remove",
            "tx_cond_unspent_find",
            "chain_ca_copy",
            "dag",
            "block",
            "dag",
            "token",
            "esbocs",
            "global_db",
            "net_srv",
            "net",
            "srv_stake",
            "poll",
            "srv_xchange",
            "emit_delegate",
            "token_decl",
            "token_update",
            "token_update_sign",
            "token_decl_sign",
            "chain_ca_pub",
            "token_emit",
            "find",
            "version",
            "remove",
            "gdb_import",
            "stats",
            "print_log",
            "stake_lock",
            "exec_cmd",
            "policy"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return 1;
        }
    }
    return 0;
}


char *dap_cli_cmd_exec(char *a_req_str) {
    dap_json_rpc_request_t *request = dap_json_rpc_request_from_json(a_req_str, s_cli_version);
    if ( !request )
        return NULL;
    int l_verbose = 0;
    // command is found
    char *cmd_name = request->method;
    dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find(cmd_name);
    bool l_finded_by_alias = false;
    char *l_append_cmd = NULL;
    char *l_ncmd = NULL;
    if (!l_cmd) {
        l_cmd = dap_cli_server_cmd_find_by_alias(cmd_name, &l_append_cmd, &l_ncmd);
        l_finded_by_alias = true;
    }
    dap_json_rpc_params_t *params = request->params;

    char *str_cmd = dap_json_rpc_params_get(params, 0);
    if (!str_cmd)
        str_cmd = cmd_name;
    int res = -1;
    char *str_reply = NULL;
    dap_json_t *l_json_arr_reply = dap_json_array_new();
    if (l_cmd) {
        if (l_cmd->overrides.log_cmd_call)
            l_cmd->overrides.log_cmd_call(str_cmd);
        else {
            char *l_str_cmd = dap_strdup(str_cmd);
            char *l_ptr = strstr(l_str_cmd, "-password");
            if (l_ptr) {
                l_ptr += 10;
                while (l_ptr[0] != '\0' && l_ptr[0] != ';') {
                    *l_ptr = '*';
                    l_ptr += 1;
                }
            }
            debug_if(dap_config_get_item_bool_default(g_config, "cli-server", "debug-more", false),
                        L_DEBUG, "execute command=%s", l_str_cmd);
            DAP_DELETE(l_str_cmd);
        }

        char **l_argv = dap_strsplit(str_cmd, ";", -1);
        int l_argc = 0;
        // Count argc
        while (l_argv[l_argc] != NULL)
            l_argc++;
        // Support alias
        if (l_finded_by_alias) {
            cmd_name = l_ncmd;
            DAP_FREE(l_argv[0]);
            l_argv[0] = l_ncmd;
            if (l_append_cmd) {
                l_argc++;
                char **al_argv = DAP_NEW_Z_COUNT(char*, l_argc + 1);
                al_argv[1] = l_ncmd;
                al_argv[1] = l_append_cmd;
                for (int i = 1; i < l_argc; i++)
                    al_argv[i + 1] = l_argv[i];
                DAP_DEL_Z(l_argv);
                l_argv = al_argv;
            }
        }
        // Call the command function
        if(l_cmd &&  l_argv && l_cmd->func) {
            dap_time_t l_call_time = 0;
            if (s_stat_callback) {
                l_call_time = dap_nanotime_now();
            }
            // Check if this is JSON-RPC command based on flags
            if (json_commands(cmd_name)) {
                res = l_cmd->func(l_argc, l_argv, (void *)&l_json_arr_reply, request->version);
            } else if (l_cmd->arg_func) {
                res = l_cmd->func_ex(l_argc, l_argv, l_cmd->arg_func, (void *)&str_reply, request->version);
            } else {
                res = l_cmd->func(l_argc, l_argv, (void *)&str_reply, request->version);
            }
            if (s_stat_callback) {
                s_stat_callback(l_cmd->id, (dap_nanotime_now() - l_call_time) / 1000000);
            }
        } else if (l_cmd) {
            log_it(L_WARNING, "NULL arguments for input for command \"%s\"", str_cmd);
            dap_json_rpc_error_add(l_json_arr_reply, -1, "NULL arguments for input for command \"%s\"", str_cmd);
        } else {
            log_it(L_WARNING, "No function for command \"%s\" but it registred?!", str_cmd);
            dap_json_rpc_error_add(l_json_arr_reply, -1, "No function for command \"%s\" but it registred?!", str_cmd);
        }
        // find '-verbose' command
        l_verbose = dap_cli_server_cmd_find_option_val(l_argv, 1, l_argc, "-verbose", NULL);
        dap_strfreev(l_argv);
    } else {
        dap_json_rpc_error_add(l_json_arr_reply, -1, "can't recognize command=%s", str_cmd);
        log_it(L_ERROR, "Reply string: \"%s\"", str_reply);
    }
    char *reply_body = NULL;
    // -verbose
    if (l_verbose) {
        if (str_reply) {
            reply_body = dap_strdup_printf("%d\r\nret_code: %d\r\n%s\r\n", res, res, str_reply);
            DAP_DELETE(str_reply);
        } else {
            dap_json_t *json_res = dap_json_object_new();
            dap_json_object_add_int64(json_res, "ret_code", res);
            dap_json_array_add(l_json_arr_reply, json_res);
        }
    } else
        reply_body = str_reply;

    // create response
    dap_json_rpc_response_t* response = reply_body
            ? dap_json_rpc_response_create(reply_body, TYPE_RESPONSE_STRING, request->id, request->version)
            : dap_json_rpc_response_create(l_json_arr_reply, TYPE_RESPONSE_JSON, request->id, request->version);
    // Note: l_json_arr_reply will be freed by dap_json_rpc_response_free if it was used in response
    if (reply_body) {
        dap_json_object_free(l_json_arr_reply);
    }
    char *response_string = dap_json_rpc_response_to_string(response);
    dap_json_rpc_response_free(response);
    dap_json_rpc_request_free(request);
    return response_string ? response_string : dap_strdup("Error");
}

DAP_INLINE int32_t dap_cli_get_cmd_thread_count()
{
    return atomic_load(&s_cmd_thread_count);
}

/**
 * @brief dap_cli_server_cmd_add
 * @param a_callback callback to statistic collect
 */
void dap_cli_server_statistic_callback_add(dap_cli_server_cmd_stat_callback_t a_callback)
{
    if (a_callback && s_stat_callback)
        log_it(L_ERROR, "Dap cli server statistic callback already added");
    else
        s_stat_callback = a_callback;
}

DAP_INLINE void dap_cli_server_set_allowed_cmd_check(const char **a_cmd_array)
{
    dap_return_if_pass_err(s_allowed_cmd_array, "Allowed cmd array already exist");
    s_allowed_cmd_array = a_cmd_array;
    s_allowed_cmd_control = true;
}

DAP_INLINE int dap_cli_server_get_version()
{
    return s_cli_version;
}

// HTTP header management functions
void dap_cli_server_http_header_add_static(const char *a_name, const char *a_value) {
    dap_return_if_fail(a_name && a_value);
    
    pthread_rwlock_wrlock(&s_http_headers_rwlock);
    
    // Check if header already exists and update it
    dap_cli_server_http_header_t *l_header = s_http_headers;
    while (l_header) {
        if (dap_strcmp(l_header->name, a_name) == 0) {
            // Update existing header
            DAP_DELETE(l_header->value);
            l_header->value = dap_strdup(a_value);
            if (l_header->callback) {
                l_header->callback = NULL; // Switch to static mode
            }
            pthread_rwlock_unlock(&s_http_headers_rwlock);
            return;
        }
        l_header = l_header->next;
    }
    
    // Create new header
    l_header = DAP_NEW_Z(dap_cli_server_http_header_t);
    if (!l_header) {
        pthread_rwlock_unlock(&s_http_headers_rwlock);
        return;
    }
    
    l_header->name = dap_strdup(a_name);
    l_header->value = dap_strdup(a_value);
    l_header->callback = NULL;
    l_header->next = s_http_headers;
    s_http_headers = l_header;
    
    pthread_rwlock_unlock(&s_http_headers_rwlock);
}

void dap_cli_server_http_header_add_dynamic(const char *a_name, dap_cli_server_http_header_callback_t a_callback) {
    dap_return_if_fail(a_name && a_callback);
    
    pthread_rwlock_wrlock(&s_http_headers_rwlock);
    
    // Check if header already exists and update it
    dap_cli_server_http_header_t *l_header = s_http_headers;
    while (l_header) {
        if (dap_strcmp(l_header->name, a_name) == 0) {
            // Update existing header
            DAP_DELETE(l_header->value);
            l_header->value = NULL;
            l_header->callback = a_callback;
            pthread_rwlock_unlock(&s_http_headers_rwlock);
            return;
        }
        l_header = l_header->next;
    }
    
    // Create new header
    l_header = DAP_NEW_Z(dap_cli_server_http_header_t);
    if (!l_header) {
        pthread_rwlock_unlock(&s_http_headers_rwlock);
        return;
    }
    
    l_header->name = dap_strdup(a_name);
    l_header->value = NULL;
    l_header->callback = a_callback;
    l_header->next = s_http_headers;
    s_http_headers = l_header;
    
    pthread_rwlock_unlock(&s_http_headers_rwlock);
}

void dap_cli_server_http_header_remove(const char *a_name) {
    dap_return_if_fail(a_name);
    
    pthread_rwlock_wrlock(&s_http_headers_rwlock);
    
    dap_cli_server_http_header_t **l_current = &s_http_headers;
    while (*l_current) {
        if (dap_strcmp((*l_current)->name, a_name) == 0) {
            dap_cli_server_http_header_t *l_to_remove = *l_current;
            *l_current = l_to_remove->next;
            
            DAP_DELETE(l_to_remove->name);
            DAP_DELETE(l_to_remove->value);
            DAP_DELETE(l_to_remove);
            break;
        }
        l_current = &((*l_current)->next);
    }
    
    pthread_rwlock_unlock(&s_http_headers_rwlock);
}

void dap_cli_server_http_headers_clear(void) {
    pthread_rwlock_wrlock(&s_http_headers_rwlock);
    
    while (s_http_headers) {
        dap_cli_server_http_header_t *l_to_remove = s_http_headers;
        s_http_headers = s_http_headers->next;
        
        DAP_DELETE(l_to_remove->name);
        DAP_DELETE(l_to_remove->value);
        DAP_DELETE(l_to_remove);
    }
    
    pthread_rwlock_unlock(&s_http_headers_rwlock);
}

// Generate additional HTTP headers string
static char* s_generate_additional_headers(void) {
    char *l_headers_str = NULL;
    
    pthread_rwlock_rdlock(&s_http_headers_rwlock);
    
    if (!s_http_headers) {
        pthread_rwlock_unlock(&s_http_headers_rwlock);
        return dap_strdup("");
    }
    
    // Calculate total length needed
    size_t l_total_len = 0;
    dap_cli_server_http_header_t *l_header = s_http_headers;
    while (l_header) {
        const char *l_value = NULL;
        char *l_temp_value = NULL;
        
        if (l_header->callback) {
            l_temp_value = l_header->callback();
            l_value = l_temp_value;
        } else {
            l_value = l_header->value;
        }
        
        if (l_value) {
            l_total_len += strlen(l_header->name) + strlen(l_value) + 4; // ": \r\n"
        }
        
        if (l_temp_value) {
            DAP_DELETE(l_temp_value);
        }
        
        l_header = l_header->next;
    }
    
    if (l_total_len == 0) {
        pthread_rwlock_unlock(&s_http_headers_rwlock);
        return dap_strdup("");
    }
    
    // Allocate and build headers string
    l_headers_str = DAP_NEW_SIZE(char, l_total_len + 1);
    l_headers_str[0] = '\0';
    
    l_header = s_http_headers;
    while (l_header) {
        const char *l_value = NULL;
        char *l_dynamic_value = NULL;
        
        if (l_header->callback) {
            l_dynamic_value = l_header->callback();
            l_value = l_dynamic_value;
        } else {
            l_value = l_header->value;
        }
        
        if (l_value) {
            char *l_header_line = dap_strdup_printf("%s: %s\r\n", l_header->name, l_value);
            strcat(l_headers_str, l_header_line);
            DAP_DELETE(l_header_line);
        }
        
        if (l_dynamic_value) {
            DAP_DELETE(l_dynamic_value);
        }
        
        l_header = l_header->next;
    }
    
    pthread_rwlock_unlock(&s_http_headers_rwlock);
    return l_headers_str;
}