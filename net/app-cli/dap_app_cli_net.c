/*
 * Authors:
 * Dmitriy A. Gerasimov <kahovski@gmail.com>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://github.com/demlabsinc
 * Copyright  (c) 2017-2019
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

//#include <dap_client.h>

#include <stdlib.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#endif

#include "dap_common.h"
#include "dap_string.h"
#include "dap_strfuncs.h"
#include "dap_cli_server.h" // for UNIX_SOCKET_FILE
#include "dap_app_cli.h"
#include "dap_app_cli_net.h"
#include "dap_enc_base64.h"

#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"

static int s_status;

//staic function to receive http data
static void dap_app_cli_http_read(dap_app_cli_connect_param_t *socket, dap_app_cli_cmd_state_t *l_cmd)
{
    ssize_t l_recv_len = recv(*socket, &l_cmd->cmd_res[l_cmd->cmd_res_cur], DAP_CLI_HTTP_RESPONSE_SIZE_MAX, 0);
    if (l_recv_len == 0) {
        s_status = DAP_CLI_ERROR_INCOMPLETE;
        return;
    }
    if (l_recv_len == -1) {
#ifdef DAP_OS_WINDOWS
        int l_errno = WSAGetLastError();
        if (l_errno == WSAEWOULDBLOCK) {
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
            s_status = DAP_CLI_ERROR_TIMEOUT;
        } else {
            s_status = DAP_CLI_ERROR_SOCKET;
        }
        return;
    }
    l_cmd->cmd_res_cur +=(size_t) l_recv_len;
    switch (s_status) {
        case 1: {   // Find content length
            const char *l_cont_len_str = "Content-Length: ";
            char *l_str_ptr = strstr(l_cmd->cmd_res, l_cont_len_str);
            if (l_str_ptr && strstr(l_str_ptr, "\r\n")) {
                l_cmd->cmd_res_len = atoi(l_str_ptr + strlen(l_cont_len_str));
                if (l_cmd->cmd_res_len == 0) {
                    s_status = DAP_CLI_ERROR_FORMAT;
                    break;
                }
                else {
                    s_status++;
                }
            } else {
                break;
            }
        }
        case 2: {   // Find header end and throw out header
            const char *l_head_end_str = "\r\n\r\n";
            char *l_str_ptr = strstr(l_cmd->cmd_res, l_head_end_str);
            if (l_str_ptr) {
                l_str_ptr += strlen(l_head_end_str);
                size_t l_head_size = l_str_ptr - l_cmd->cmd_res;
                memmove(l_cmd->cmd_res, l_str_ptr, l_cmd->cmd_res_cur - l_head_size);
                l_cmd->cmd_res_cur -= l_head_size;
                // read rest of data
                if(l_cmd->cmd_res_cur < l_cmd->cmd_res_len) {
                    l_cmd->cmd_res = DAP_REALLOC(l_cmd->cmd_res, l_cmd->cmd_res_len + 1);
                    while((l_cmd->cmd_res_len - l_cmd->cmd_res_cur) > 0) {
                        ssize_t l_recv_len = recv(*socket, &l_cmd->cmd_res[l_cmd->cmd_res_cur], l_cmd->cmd_res_len - l_cmd->cmd_res_cur, 0);
                        if(l_recv_len <= 0)
                            break;
                        l_cmd->cmd_res_cur += l_recv_len;
                    }
                }
                s_status++;
            } else {
                break;
            }
        }
        default:
        case 3: {   // Complete command reply
            if (l_cmd->cmd_res_cur == l_cmd->cmd_res_len) {
                l_cmd->cmd_res[l_cmd->cmd_res_cur] = 0;
                s_status = 0;
            } else {
                s_status = DAP_CLI_ERROR_FORMAT;
            }
        } break;
    }
}

/**
 * @brief dap_app_cli_connect
 * @details Connect to node unix socket server
 * @param a_socket_path
 * @return if connect established, else NULL
 */
dap_app_cli_connect_param_t* dap_app_cli_connect(const char *a_socket_path)
{
    // set socket param
    int buffsize = DAP_CLI_HTTP_RESPONSE_SIZE_MAX;
#if defined(__WIN32) || defined(ANDROID)
    // TODO connect to the named pipe "\\\\.\\pipe\\node_cli.pipe"
    uint16_t l_cli_port = a_socket_path ? strtod(a_socket_path, NULL) : 0;
    if (!l_cli_port)
        return NULL;
    SOCKET l_socket = socket(AF_INET, SOCK_STREAM, 0);
#else
    if (!a_socket_path) {
        return NULL;
    }
    // create socket
    int l_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l_socket < 0) {
        return NULL;
    }
    struct timeval l_to = {DAP_CLI_HTTP_TIMEOUT, 0};
#endif
    // connect
    int l_addr_len;
#ifdef WIN32
    struct sockaddr_in l_remote_addr = {
        .sin_family = AF_INET, .sin_port = htons(l_cli_port), .sin_addr = {{ .S_addr = htonl(INADDR_LOOPBACK) }}
    };
    l_addr_len = sizeof(struct sockaddr_in);
#elif defined(DAP_OS_ANDROID)
    struct sockaddr_in l_remote_addr = {
        .sin_family = AF_INET, .sin_port = htons(l_cli_port), .sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) }
    };
    l_addr_len = sizeof(struct sockaddr_in);
#else
    struct sockaddr_un l_remote_addr;
    l_remote_addr.sun_family =  AF_UNIX;
    strcpy(l_remote_addr.sun_path, a_socket_path);
    l_addr_len = SUN_LEN(&l_remote_addr);
#endif
    if (connect(l_socket, (struct sockaddr *)&l_remote_addr, l_addr_len) == SOCKET_ERROR) {
#ifdef __WIN32
            _set_errno(WSAGetLastError());
#endif
        printf("Socket connection err: %d\n", errno);
        closesocket(l_socket);
        return NULL;
    }
    dap_app_cli_connect_param_t *l_ret = DAP_NEW_Z(dap_app_cli_connect_param_t);
    if (!l_ret) {
        closesocket(l_socket);
        printf("Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    *l_ret = l_socket;
    return l_ret;
}

/* if cli command argument contains one of the following symbol
 argument is going to be encoded to base64 */
static const char* s_dap_app_cli_forbidden_symbols[] = {"\r\n", ";", ""};

bool s_dap_app_cli_cmd_contains_forbidden_symbol(const char * a_cmd_param){
    for(int i = 0; s_dap_app_cli_forbidden_symbols[i][0] != '\0'; i++){
        if(strstr(a_cmd_param, s_dap_app_cli_forbidden_symbols[i]))
            return true;
    }
    return false;
}

/**
 * Send request to kelvin-node
 *
 * return 0 if OK, else error code
 */
int dap_app_cli_post_command( dap_app_cli_connect_param_t *a_socket, dap_app_cli_cmd_state_t *a_cmd )
{
    if(!a_socket || !a_cmd || !a_cmd->cmd_name) {
        assert(0);
        return -1;
    }    
    a_cmd->cmd_res_cur = 0;
    dap_string_t *l_cmd_data = dap_string_new(a_cmd->cmd_name);
    if (a_cmd->cmd_param) {
        for (int i = 0; i < a_cmd->cmd_param_count; i++) {
            if (a_cmd->cmd_param[i]) {
                dap_string_append(l_cmd_data, ";");
                if(s_dap_app_cli_cmd_contains_forbidden_symbol(a_cmd->cmd_param[i])){
                    char * l_cmd_param_base64 = dap_enc_strdup_to_base64(a_cmd->cmd_param[i]);
                    dap_string_append(l_cmd_data, l_cmd_param_base64);
                    DAP_DELETE(l_cmd_param_base64);
                }else{
                    dap_string_append(l_cmd_data, a_cmd->cmd_param[i]);
                }
            }
        }
    }
    dap_json_rpc_params_t * params = dap_json_rpc_params_create();
    dap_json_rpc_params_add_data(params, l_cmd_data->str, TYPE_PARAM_STRING);
    uint64_t l_id_response = dap_json_rpc_response_get_new_id();
    dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(a_cmd->cmd_name, params, l_id_response);
    char * request_str = dap_json_rpc_request_to_json_string(a_request);

    dap_string_t *l_post_data = dap_string_new("");
    dap_string_printf(l_post_data, "POST /connect HTTP/1.1\r\n"
                                   "Host: localhost\r\n"
                                   "Content-Type: text/text\r\n"
                                   "Content-Length: %zu\r\n"
                                   "\r\n"
                                   "%s", strlen(request_str), request_str);
    DAP_DELETE(request_str);
    size_t res = send(*a_socket, l_post_data->str, l_post_data->len, 0);
    if (res != l_post_data->len) {
        dap_json_rpc_request_free(a_request);
        printf("Error sending to server");
        return -1;
    }

    //wait for command execution
    time_t l_start_time = time(NULL);
    s_status = 1;
    a_cmd->cmd_res = DAP_NEW_Z_SIZE(char, DAP_CLI_HTTP_RESPONSE_SIZE_MAX);
    while(s_status > 0) {
        dap_app_cli_http_read(a_socket, a_cmd);
        if ((time(NULL) - l_start_time > DAP_CLI_HTTP_TIMEOUT)&&!a_cmd->cmd_res)
            s_status = DAP_CLI_ERROR_TIMEOUT;
    }
    // process result
    if (!s_status && a_cmd->cmd_res) {
        dap_json_rpc_response_t* response = dap_json_rpc_response_from_string(a_cmd->cmd_res);
        if (l_id_response != response->id) {
            printf("Wrong response from server\n");
            dap_json_rpc_request_free(a_request);
            dap_json_rpc_response_free(response);
            return -1;
        }
        if (dap_json_rpc_response_printf_result(response, a_cmd->cmd_name) != 0) {
            printf("Something wrong with response\n");
        }
        dap_json_rpc_response_free(response);
    }
    DAP_DELETE(a_cmd->cmd_res);
    dap_json_rpc_request_free(a_request);
    dap_string_free(l_cmd_data, true);
    dap_string_free(l_post_data, true);
    return s_status;
}


int dap_app_cli_disconnect(dap_app_cli_connect_param_t *a_socket)
{
    closesocket(*a_socket);
    DAP_DELETE(a_socket);
    return 0;
}
