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

#include "dap_json_rpc_errors.h"
#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"

#define LOG_TAG "dap_cli_server"

#define MAX_CONSOLE_CLIENTS 16

static dap_server_t *s_cli_server = NULL;
static bool s_debug_cli = false;

static dap_cli_cmd_t *cli_commands = NULL;
static dap_cli_cmd_aliases_t *s_command_alias = NULL;

static inline void s_cmd_add_ex(const char *a_name, dap_cli_server_cmd_callback_ex_t a_func, void *a_arg_func, const char *a_doc, const char *a_doc_ex);

typedef struct cli_cmd_arg {
    dap_worker_t *worker;
    dap_events_socket_uuid_t es_uid;
    size_t buf_size;
    char buf[];
} cli_cmd_arg_t;

static bool s_cli_cmd_exec(void *a_arg);

dap_cli_handler_cl_t *s_json_rpc_handler = NULL;

void dap_json_rpc_cli_handler_add(const char *a_method, handler_func_cli_t* a_fund) {
    dap_cli_handler_cl_t *l_handler = DAP_NEW(dap_cli_handler_cl_t);
    l_handler->func = a_fund;
    l_handler->method = a_method;
    HASH_ADD_STR(s_json_rpc_handler, method, l_handler);
}

DAP_STATIC_INLINE void s_cli_cmd_schedule(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg) {
    static const char l_content_len_str[] = "Content-Length: ";
    char *l_len_token = strstr((char*)a_es->buf_in, l_content_len_str);
#define m_dump_error_and_ret ({ \
    const char l_error_str[] = "{ \"type\": 0, \"result\":\" Invalid request\", \"errors\": null, \"id\": 1 }", \
        l_err_format_str[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: %zu\r\n\r\n%s"; \
    dap_events_socket_write_f_unsafe(a_es, l_err_format_str, sizeof(l_error_str) - 1, l_error_str); \
    char *buf_dump = dap_dump_hex(a_es->buf_in, dap_max(a_es->buf_in_size, (size_t)65536)); \
    log_it(L_DEBUG, "Incomplete cmd request: %s", buf_dump); \
    DAP_DELETE(buf_dump); \
})
    if (!l_len_token || !strpbrk(l_len_token, "\r\n"))
        return m_dump_error_and_ret;
    long l_cmd_len = strtol(l_len_token + sizeof(l_content_len_str) - 1, NULL, 10);
    
    if (!l_cmd_len || l_cmd_len > 65536)
        return m_dump_error_and_ret;

    static const char l_head_end_str[] = "\r\n\r\n";
    char *l_hdr_end_token = strstr(l_len_token, l_head_end_str);
    if (!l_hdr_end_token)
        return m_dump_error_and_ret;
    else
        l_hdr_end_token += ( sizeof(l_head_end_str) - 1 );
    if (a_es->buf_in_size > l_cmd_len + (size_t)(l_hdr_end_token - (char*)a_es->buf_in))
        return m_dump_error_and_ret;
    cli_cmd_arg_t *l_arg = DAP_NEW_Z_SIZE(cli_cmd_arg_t, sizeof(cli_cmd_arg_t) + l_cmd_len + 1);
    *l_arg = (cli_cmd_arg_t){ .worker = a_es->worker, .es_uid = a_es->uuid, .buf_size = l_cmd_len };
    memcpy(l_arg->buf, l_hdr_end_token, l_cmd_len);
    dap_proc_thread_callback_add_pri(NULL, s_cli_cmd_exec, l_arg, DAP_QUEUE_MSG_PRIORITY_HIGH);
    a_es->buf_in_size = 0;
#undef m_dump_error_and_ret
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
    dap_events_socket_callbacks_t l_callbacks = { .read_callback = s_cli_cmd_schedule };
    if (!( s_cli_server = dap_server_new(a_cfg_section, NULL, &l_callbacks) )) {
        log_it(L_ERROR, "CLI server not initialized");
        return -2;
    }
    log_it(L_INFO, "CLI server initialized");
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
 * @brief dap_cli_server_cmd_add
 * @param a_name
 * @param a_func
 * @param a_doc
 * @param a_doc_ex
 */
void dap_cli_server_cmd_add(const char * a_name, dap_cli_server_cmd_callback_t a_func, const char *a_doc, const char *a_doc_ex)
{
    s_cmd_add_ex(a_name, (dap_cli_server_cmd_callback_ex_t)(void *)a_func, NULL, a_doc, a_doc_ex);
}

/**
 * @brief s_cmd_add_ex
 * @param a_name
 * @param a_func
 * @param a_arg_func
 * @param a_doc
 * @param a_doc_ex
 */
static inline void s_cmd_add_ex(const char * a_name, dap_cli_server_cmd_callback_ex_t a_func, void *a_arg_func, const char *a_doc, const char *a_doc_ex)
{
    dap_cli_cmd_t *l_cmd_item = DAP_NEW_Z(dap_cli_cmd_t);
    if (!l_cmd_item) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    snprintf(l_cmd_item->name,sizeof (l_cmd_item->name),"%s",a_name);
    l_cmd_item->doc = strdup( a_doc);
    l_cmd_item->doc_ex = strdup( a_doc_ex);
    if (a_arg_func) {
        l_cmd_item->func_ex = a_func;
        l_cmd_item->arg_func = a_arg_func;
    } else {
        l_cmd_item->func = (dap_cli_server_cmd_callback_t )(void *)a_func;
    }
    HASH_ADD_STR(cli_commands,name,l_cmd_item);
    log_it(L_DEBUG,"Added command %s",l_cmd_item->name);
}

int is_json_clear_method(const char *a_name) {
    static const char *long_method[] = {
            "j_tx_create"
    };
    for (size_t i = 0; i < sizeof(long_method)/sizeof(long_method[0]); i++) {
        if (!strcmp(a_name, long_method[i])) {
            return 1;
        }
    }
    return 0;
}

int json_commands(const char * a_name) {
    static const char* long_cmd[] = {
            "tx_history",
            "wallet",
            "mempool",
            "ledger",
            "tx_create",
            "tx_create_json",
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
            "net",
            "srv_stake",
            "voting",
            "emit_delegate",
            "exec_cmd",
            "file"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return 1;
        }
    }
    return 0;
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

void dap_cli_server_alias_add(const char *a_alias, const char *a_pre_cmd, dap_cli_cmd_t *a_cmd) {
    if (!a_alias || !a_pre_cmd || !a_cmd)
        return;
    dap_cli_cmd_aliases_t *l_alias = DAP_NEW(dap_cli_cmd_aliases_t);
    size_t l_alias_size = dap_strlen(a_alias);
    memcpy(l_alias->alias, a_alias, l_alias_size);
    l_alias->alias[l_alias_size] = '\0';
    size_t l_addition_size = dap_strlen(a_pre_cmd);
    memcpy(l_alias->addition, a_pre_cmd, l_addition_size);
    l_alias->addition[l_addition_size] = '\0';
    l_alias->standard_command = a_cmd;
    HASH_ADD_STR(s_command_alias, alias, l_alias);
}

dap_cli_cmd_t *dap_cli_server_cmd_find_by_alias(const char *a_alias, char **a_append, char **a_ncmd) {
    dap_cli_cmd_aliases_t *l_alias = NULL;
    HASH_FIND_STR(s_command_alias, a_alias, l_alias);
    if (!l_alias)
        return NULL;
    *a_append = dap_strdup(l_alias->addition);
    *a_ncmd = dap_strdup(l_alias->standard_command->name);
    return l_alias->standard_command;
}

static bool s_cli_cmd_exec(void *a_arg) {
    cli_cmd_arg_t *l_arg = (cli_cmd_arg_t*)a_arg;
    char    *l_ret = dap_cli_cmd_exec(l_arg->buf),
            *l_full_ret = dap_strdup_printf("HTTP/1.1 200 OK\r\n"
                                            "Content-Length: %zu\r\n\r\n"
                                            "%s", dap_strlen(l_ret), l_ret);
    dap_events_socket_write(l_arg->worker, l_arg->es_uid, l_full_ret, dap_strlen(l_full_ret));
    // TODO: pagination
    //dap_events_socket_remove_and_delete(l_arg->worker, l_arg->es_uid); // No need...
    DAP_DEL_MULTY(l_ret, /*l_full_ret,*/ a_arg);
    return false;
}

char *dap_cli_cmd_exec(char *a_req_str) {
    dap_json_rpc_request_t *request = dap_json_rpc_request_from_json(a_req_str);
    if ( !request )
        return NULL;
    int l_verbose = 0;
    // command is found
    char *cmd_name = request->method;
    if (is_json_clear_method(cmd_name)) {
        json_object *l_obj_ret = json_object_new_object();
        dap_cli_handler_cl_t *l_handler = NULL;
        HASH_FIND_STR(s_json_rpc_handler, cmd_name, l_handler);
        if (!l_handler) {
            dap_json_rpc_error_add(l_obj_ret, 1, "Can't find handler '%s' method", cmd_name);
        } else {
            dap_json_rpc_params_t *params = request->params;
            if (params->length == 1){
                json_object *l_obj_params = dap_json_rpc_params_get(params, 0);
                l_handler->func(l_obj_params, l_obj_ret);
            }
        }
        dap_json_rpc_response_t *l_response = dap_json_rpc_response_create(l_obj_ret,
                                     TYPE_RESPONSE_JSON, request->id);
        char *response_string = dap_json_rpc_response_to_string(l_response);
        json_object_put(l_obj_ret);
        dap_json_rpc_response_free(l_response);
        dap_json_rpc_request_free(request);
        return response_string;
    } else {
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
        int res = -1;
        char *str_reply = NULL;
        json_object *l_json_arr_reply = json_object_new_array();
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
            int argc = 0;
            // Count argc
            while (l_argv[argc] != NULL) argc++;
            // Support alias
            if (l_finded_by_alias) {
                int l_argc = argc + 1;
                char **al_argv = DAP_NEW_Z_COUNT(char*, l_argc + 1);
                al_argv[0] = l_ncmd;
                al_argv[1] = l_append_cmd;
                for (int i = 1; i < argc; i++)
                    al_argv[i + 1] = l_argv[i];
                cmd_name = l_ncmd;
                DAP_FREE(l_argv[0]);
                DAP_DEL_Z(l_argv);
                l_argv = al_argv;
                argc = l_argc;
            }
            // Call the command function
            if (l_cmd && l_argv && l_cmd->func) {
                if (json_commands(cmd_name)) {
                    res = l_cmd->func(argc, l_argv, (void *) &l_json_arr_reply);
                } else if (l_cmd->arg_func) {
                    res = l_cmd->func_ex(argc, l_argv, l_cmd->arg_func, (void *) &str_reply);
                } else {
                    res = l_cmd->func(argc, l_argv, (void *) &str_reply);
                }
            } else if (l_cmd) {
                log_it(L_WARNING, "NULL arguments for input for command \"%s\"", str_cmd);
                dap_json_rpc_error_add(l_json_arr_reply, -1, "NULL arguments for input for command \"%s\"", str_cmd);
            } else {
                log_it(L_WARNING, "No function for command \"%s\" but it registred?!", str_cmd);
                dap_json_rpc_error_add(l_json_arr_reply, -1, "No function for command \"%s\" but it registred?!", str_cmd);
            }
            // find '-verbose' command
            l_verbose = dap_cli_server_cmd_find_option_val(l_argv, 1, argc, "-verbose", NULL);
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
                json_object *json_res = json_object_new_object();
                json_object_object_add(json_res, "ret_code", json_object_new_int(res));
                json_object_array_add(l_json_arr_reply, json_res);
            }
        } else
            reply_body = str_reply;

        // create response
        dap_json_rpc_response_t *response = reply_body
                ? dap_json_rpc_response_create(reply_body, TYPE_RESPONSE_STRING, request->id)
                : dap_json_rpc_response_create(json_object_get(l_json_arr_reply), TYPE_RESPONSE_JSON, request->id);
        json_object_put(l_json_arr_reply);
        char *response_string = dap_json_rpc_response_to_string(response);
        dap_json_rpc_response_free(response);
        dap_json_rpc_request_free(request);
        return response_string;
    }
}
