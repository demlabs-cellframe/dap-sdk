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

#include "dap_json_rpc_errors.h"
#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"

#define LOG_TAG "dap_cli_server"

#define MAX_CONSOLE_CLIENTS 16

static SOCKET server_sockfd = -1; // network or local unix
static uint32_t l_listen_port = 0;
static bool s_debug_cli = false;

#ifdef _WIN32
  #define poll WSAPoll
#endif

static dap_cli_cmd_t * s_commands = NULL;
static dap_cli_cmd_aliases_t * s_command_alias = NULL;

static void* s_thread_main_func(void *args);
static inline void s_cmd_add_ex(const char * a_name, dap_cli_server_cmd_callback_ex_t a_func, void *a_arg_func, const char *a_doc, const char *a_doc_ex);


#ifdef _WIN32

/**
 * @brief p_get_next_str
 *
 * @param hPipe
 * @param dwLen
 * @param stop_str
 * @param del_stop_str
 * @param timeout
 * @return char*
 */
char *p_get_next_str( HANDLE hPipe, int *dwLen, const char *stop_str, bool del_stop_str, int timeout )
{
    UNUSED(timeout);
    bool bSuccess = false;
    long nRecv = 0; // count of bytes received
    size_t stop_str_len = (stop_str) ? strlen(stop_str) : 0;
    // if there is nothing to look for

    if(!stop_str_len)
        return NULL;

    size_t lpszBuffer_len = 256;
    char *lpszBuffer = DAP_NEW_Z_SIZE(char, lpszBuffer_len);
    // received string will not be larger than MAX_REPLY_LEN

    while( 1 ) //nRecv < MAX_REPLY_LEN)
    {
      long ret = 0;
        // read one byte
//        long ret = s_recv( nSocket, (unsigned char *) (lpszBuffer + nRecv), 1, timeout);

      bSuccess = ReadFile( hPipe, lpszBuffer + nRecv,
         lpszBuffer_len - nRecv, (LPDWORD)&ret, NULL );

        //int ret = recv(nSocket,lpszBuffer+nRecv,1, 0);
        if ( ret <= 0 || !bSuccess )
            break;

        nRecv += ret;
        //printf("**debug** socket=%d read  %d bytes '%0s'",nSocket, ret, (lpszBuffer + nRecv));

        while((nRecv + 1) >= (long) lpszBuffer_len)
        {
            lpszBuffer_len *= 2;
            lpszBuffer = (char*) realloc(lpszBuffer, lpszBuffer_len);
        }

        // search for the required string
        if(nRecv >=  (long) stop_str_len) {
            // found the required string
            if(!strncasecmp(lpszBuffer + nRecv - stop_str_len, stop_str, stop_str_len)) {
                bSuccess = true;
                break;
            }
        }
    };

    // end reading

    if(bSuccess) {
        // delete the searched string
        if(del_stop_str) {
            lpszBuffer[nRecv -  (long) stop_str_len] = '\0';
            if(dwLen)
                *dwLen =(int) nRecv - (int) stop_str_len;
        }
        else {
            lpszBuffer[nRecv] = '\0';
            if(dwLen)
                *dwLen = (int) nRecv;
        }
        lpszBuffer = DAP_REALLOC(lpszBuffer,(size_t) *dwLen + 1);
        return lpszBuffer;
    }

    // in case of an error or missing string

    if(dwLen)
        *dwLen = 0;

    free(lpszBuffer);

    return NULL;
}

/**
 * @brief thread_pipe_client_func
 * threading function for processing a request from a client
 * @param args
 * @return void*
 */
static void *thread_pipe_client_func( void *args )
{
    HANDLE hPipe = (HANDLE)args;

//    SOCKET newsockfd = (SOCKET) (intptr_t) args;
    if(s_debug_cli)
        log_it(L_INFO, "new connection pipe = %p", hPipe);

    int str_len, marker = 0;
    int timeout = 5000; // 5 sec
    int argc = 0;

    dap_list_t *cmd_param_list = NULL;

    while( 1 )
    {
        // wait data from client
//        int is_data = s_poll( newsockfd, timeout );
        // timeout
//        if(!is_data)
//            continue;
        // error (may be socket closed)
//        if(is_data < 0)
//            break;

//        int is_valid = is_valid_socket(newsockfd);
//        if(!is_valid)
//        {
//            break;
//        }

        // receiving http header
        char *str_header = p_get_next_str( hPipe, &str_len, "\r\n", true, timeout );

        // bad format
        if(!str_header)
            break;

        if ( str_header && strlen(str_header) == 0) {
            marker++;
            if(marker == 1)
                continue;
        }

        // filling parameters of command
        if ( marker == 1 ) {
            cmd_param_list = dap_list_append( cmd_param_list, str_header );
            //printf("g_list_append argc=%d command=%s ", argc, str_header);
            argc ++;
        }
        else
            free( str_header );

        if ( marker == 2 ) {

            dap_list_t *list = cmd_param_list;
            // form command

            unsigned int argc = dap_list_length( list );
            // command is found

            if ( argc >= 1) {

                int l_verbose = 0;
                char *cmd_name = list->data;
                list = dap_list_next( list );

                // execute command
                char *str_cmd = dap_strdup_printf( "%s", cmd_name );
                dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find( cmd_name );
                int res = -1;
                char *str_reply = NULL;

                if ( l_cmd ) {
                    bool l_pass_in_cmd = false;
                    while( list ) {
                                                l_pass_in_cmd |= !strcmp((char *)list->data, "-password") || !strcmp((char *)list->data, "password");
                        str_cmd = dap_strdup_printf( "%s;%s", str_cmd, (char *)list->data );
                        list = dap_list_next(list);
                    }

                    log_it(L_INFO, "execute command=%s", l_pass_in_cmd ? "Command hide, password used" : str_cmd );
                    // exec command

                    char **l_argv = dap_strsplit( str_cmd, ";", -1 );
                    // Call the command function

                    if ( l_cmd &&  l_argv && l_cmd->func ) {
                        if (l_cmd->arg_func) {
                            res = l_cmd->func_ex(argc, l_argv, l_cmd->arg_func, (void **)&str_reply);
                        } else {
                            res = l_cmd->func(argc, l_argv, (void **)&str_reply);
                        }
                    }

                    else if ( l_cmd ) {
                        log_it(L_WARNING,"NULL arguments for input for command \"%s\"", str_cmd );
                    }else {
                        log_it(L_WARNING,"No function for command \"%s\" but it registred?!", str_cmd );
                    }

                    // find '-verbose' command
                    l_verbose = dap_cli_server_cmd_find_option_val( l_argv, 1, argc, "-verbose", NULL );
                    dap_strfreev( l_argv );

                } else {
                    str_reply = dap_strdup_printf("can't recognize command = %s", str_cmd );
                    log_it( L_ERROR, str_reply );
                }

                char *reply_body;

                if(l_verbose)
                  reply_body = dap_strdup_printf("%d\r\nret_code: %d\r\n%s\r\n", res, res, (str_reply) ? str_reply : "");
                else
                  reply_body = dap_strdup_printf("%d\r\n%s\r\n", res, (str_reply) ? str_reply : "");

                // return the result of the command function
                char *reply_str = dap_strdup_printf( "HTTP/1.1 200 OK\r\n"
                                                    "Content-Length: %d\r\n\r\n"
                                                    "%s",
                        strlen(reply_body), reply_body );

                int ret;// = send( newsockfd, reply_str, strlen(reply_str) ,0 );

                WriteFile( hPipe, reply_str, strlen(reply_str), (LPDWORD)&ret, NULL );

                DAP_DELETE(str_reply);
                DAP_DELETE(reply_str);
                DAP_DELETE(reply_body);

                DAP_DELETE(str_cmd);
            }
            dap_list_free_full(cmd_param_list, free);
            break;
        }
    }

    // close connection
//    int cs = closesocket(newsockfd);

    log_it( L_INFO, "close connection pipe = %p", hPipe );

    FlushFileBuffers( hPipe );
    DisconnectNamedPipe( hPipe );
    CloseHandle( hPipe );

    return NULL;
}


/**
 * @brief thread_pipe_func
 * main threading server function pipe win32
 * @param args
 * @return void*
 */
static void* thread_pipe_func( void *args )
{
   UNUSED(args);
   BOOL   fConnected = FALSE;
   pthread_t threadId;
   HANDLE hPipe = INVALID_HANDLE_VALUE;
   static const char *cPipeName = "\\\\.\\pipe\\node_cli.pipe";

   for (;;)
   {
///      printf( "\nPipe Server: Main thread awaiting client connection on %s\n", lpszPipename );

      hPipe = CreateNamedPipe(
          cPipeName,                // pipe name
          PIPE_ACCESS_DUPLEX,       // read/write access
          PIPE_TYPE_MESSAGE |       // message type pipe
          PIPE_READMODE_MESSAGE |   // message-read mode
          PIPE_WAIT,                // blocking mode
          PIPE_UNLIMITED_INSTANCES, // max. instances
          4096,                     // output buffer size
          4096,                     // input buffer size
          0,                        // client time-out
          NULL );                   // default security attribute

      if ( hPipe == INVALID_HANDLE_VALUE ) {
          log_it( L_ERROR, "CreateNamedPipe failed, GLE = %lu.\n", GetLastError() );
          return NULL;
      }

      fConnected = ConnectNamedPipe( hPipe, NULL ) ? TRUE : ( GetLastError() == ERROR_PIPE_CONNECTED );

      if ( fConnected )
      {
        log_it( L_INFO, "Client %p connected, creating a processing thread.\n", hPipe );

        pthread_create( &threadId, NULL, thread_pipe_client_func, hPipe );
        pthread_detach( threadId );
      }
      else
         CloseHandle( hPipe );
    }

    return NULL;
}
#endif


/**
 * @brief dap_cli_server_init
 * @param a_debug_more
 * @param a_socket_path_or_address
 * @param a_port
 * @param a_permissions
 * @return
 */
int dap_cli_server_init(bool a_debug_more,const char * a_socket_path_or_address, uint16_t a_port, const char * a_permissions)
{
    s_debug_cli = a_debug_more;
#ifndef _WIN32
    struct sockaddr_un l_server_addr={0};
    l_server_addr.sun_family =  AF_UNIX;
    snprintf(l_server_addr.sun_path,sizeof(l_server_addr.sun_path), "%s", a_socket_path_or_address);
#else
   pthread_t threadId;
#endif

    SOCKET sockfd = -1;

    // create thread for waiting of clients
    pthread_t l_thread_id;

    l_listen_port = dap_config_get_item_uint16_default( g_config, "conserver", "listen_port_tcp",0);

    const char * l_listen_unix_socket_path = dap_config_get_item_str( g_config, "conserver", "listen_unix_socket_path");

    mode_t l_listen_unix_socket_permissions = 0770;

    if ( l_listen_unix_socket_path && l_listen_unix_socket_permissions ) {
        if ( a_permissions ) {
            uint16_t l_perms;
            sscanf(a_permissions, "%ho", &l_perms);
            l_listen_unix_socket_permissions = l_perms;
        }
        log_it( L_INFO, "Console interace on path %s (%04o) ", l_listen_unix_socket_path, l_listen_unix_socket_permissions );

      #ifndef DAP_OS_WINDOWS

        if ( server_sockfd >= 0 ) {
            dap_cli_server_deinit();
            server_sockfd = 0;
        }

        // create socket
        sockfd = socket( AF_UNIX, SOCK_STREAM, 0 );
        if( sockfd == INVALID_SOCKET )
            return -1;

        //int gdsg = sizeof(struct sockaddr_un);

        // Creatuing directory if not created
        char * l_listen_unix_socket_path_dir = dap_path_get_dirname(l_listen_unix_socket_path);
        dap_mkdir_with_parents(l_listen_unix_socket_path_dir);
        DAP_DELETE(l_listen_unix_socket_path_dir);

        if ( access( l_listen_unix_socket_path , R_OK) != -1 )
            unlink( l_listen_unix_socket_path );


        // connecting the address with a socket
        if( bind(sockfd, (const struct sockaddr*) &l_server_addr, sizeof(struct sockaddr_un)) == SOCKET_ERROR) {
            // errno = EACCES  13  Permission denied
            if ( errno == EACCES ) // EACCES=13
                log_it( L_ERROR, "Server can't start(err=%d). Can't create file=%s [Permission denied]", errno,
                        l_listen_unix_socket_path );
            else
                log_it( L_ERROR, "Server can't start(err=%d). May be problem with file=%s?", errno, l_listen_unix_socket_path );
            closesocket( sockfd );
            return -2;
        }
        chmod(l_listen_unix_socket_path,l_listen_unix_socket_permissions);

      #else

//    Sleep( 3000 );

        if( pthread_create(&threadId, NULL, thread_pipe_func, (void*) (intptr_t) sockfd) != 0 ) {
            closesocket( sockfd );
            return -7;
        }

        return 0;
      #endif

    }
    else if (l_listen_port ){

        const char *l_listen_addr_str = dap_config_get_item_str(g_config, "conserver", "listen_address");

        log_it( L_INFO, "Console interace on addr %s port %u ", l_listen_addr_str, l_listen_port );

        struct sockaddr_in server_addr = (struct sockaddr_in) {
            .sin_family = AF_INET,
            .sin_port   = htons((uint16_t)l_listen_port)
        };
#ifdef DAP_OS_WINDOWS

        server_addr.sin_addr = (struct in_addr){{ .S_addr = htonl(INADDR_LOOPBACK) }};
#else
        inet_pton( AF_INET, l_listen_addr_str, &server_addr.sin_addr );
#endif
        // create socket
        if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET ) {
#ifdef __WIN32
            _set_errno(WSAGetLastError());
#endif
            log_it( L_ERROR, "Console Server: can't create socket, err %d", errno );
            return -3;
        }

        // connecting the address with a socket
        if ( bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == SOCKET_ERROR ) {
#ifdef __WIN32
            _set_errno(WSAGetLastError());
#endif
            log_it( L_ERROR, "Console Server: can't bind socket, err %d", errno );
            closesocket( sockfd );
            return -4;
        }
    }else {
        log_it (L_INFO, "Not defined console interface");
        return 0;
    }

    // turn on reception of connections
    if( listen(sockfd, MAX_CONSOLE_CLIENTS) == SOCKET_ERROR )
        return -5;

    if( pthread_create(&l_thread_id, NULL, s_thread_main_func, (void*) (intptr_t) sockfd) != 0 ) {
        closesocket( sockfd );
        return -6;
    }

    // in order to thread not remain in state "dead" after completion
    pthread_detach( l_thread_id );
    server_sockfd = sockfd;

    return 0;
}

/**
 * @brief dap_cli_server_deinit
 */
void dap_cli_server_deinit()
{
    if(server_sockfd != INVALID_SOCKET)
        closesocket(server_sockfd);
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
        log_it(L_CRITICAL, "Memory allocation error");
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
    HASH_ADD_STR(s_commands,name,l_cmd_item);
    log_it(L_DEBUG,"Added command %s",l_cmd_item->name);
}

/**
 * @brief int s_poll
 * Wait for data
 * timeout -  timeout in ms
 * [Specifying a negative value in timeout means an infinite timeout.]
 * [Specifying a timeout of zero causes poll() to return immediately, even if no file descriptors are ready.]
 * return zero if the time limit expired
 * return: >0 if data is present to read
 * return: -1 if error
 * @param socket
 * @param timeout
 * @return int
 */
static int s_poll( int sd, int timeout )
{
struct pollfd fds = {.fd = sd, .events = POLLIN};
int res;

    res = poll(&fds, 1, timeout);

    return  (res == 1 && !(fds.revents & POLLIN)) ? -1 : res;
}


/**
 * @brief is_valid_socket
 * Check socket for validity
 * @param sock
 * @return true
 * @return false
 */
static int is_valid_socket(SOCKET sd)
{
struct pollfd fds = {.fd = sd, .events = POLLIN};
int res;

    if ( 0 > (res = poll(&fds, 1, 0)) )
        return false;

    // event with an error code
    if(res > 0)
    {
        // feature of disconnection under Windows
        // under Windows, with socket closed fds.revents=POLLHUP, in Unix fds.events = POLLIN
        if(fds.revents & (POLLERR | POLLHUP | POLLNVAL))
            return false;

        // feature of disconnection under Unix (QNX)
        // under Windows, with socket closed res = 0, in Unix res = -1
        char buf[2];
        if ( 0 > (res = recv(sd, buf, 1, MSG_PEEK)) ) // MSG_PEEK  The data is treated as unread and the next recv() function shall still return this data.
            return false;

        // data in the buffer must be(count_desc>0), but read 0 bytes(res=0)
        if(!res && (fds.revents & POLLIN))
            return false;
    }

    return true;
}


/**
 * @brief s_get_next_str
 * Reading from the socket till arrival the specified string
 *
 * stop_str - string to which reading will continue
 * del_stop_str - удалять ли строку для поиска в конце
 * timeout - in ms
 * return: string (if waited for final characters) or NULL, if the string requires deletion
 * @param nSocket
 * @param dwLen
 * @param stop_str
 * @param del_stop_str
 * @param timeout
 * @return char*
 */
char* s_get_next_str( SOCKET nSocket, int *dwLen, const char *stop_str, bool del_stop_str, int timeout )
{
    bool bSuccess = false;
    long nRecv = 0; // count of bytes received
    size_t stop_str_len = (stop_str) ? strlen(stop_str) : 0;
    // if there is nothing to look for
    if(!stop_str_len)
        return NULL;
    size_t lpszBuffer_len = 256;
    char *lpszBuffer = DAP_NEW_Z_SIZE(char, lpszBuffer_len);
    if (!lpszBuffer) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    // received string will not be larger than MAX_REPLY_LEN

    while(1) //nRecv < MAX_REPLY_LEN)
    {
        // read one byte
        long ret = dap_net_recv(nSocket, (unsigned char *) (lpszBuffer + nRecv), 1, timeout);
        //int ret = recv(nSocket,lpszBuffer+nRecv,1, 0);
        if(ret <= 0)
                {
            break;
        }
        nRecv += ret;
        //printf("**debug** socket=%d read  %d bytes '%0s'",nSocket, ret, (lpszBuffer + nRecv));
        while((nRecv + 1) >= (long) lpszBuffer_len)
        {
            lpszBuffer_len *= 2;
            lpszBuffer = (char*) realloc(lpszBuffer, lpszBuffer_len);
        }
        // search for the required string
        if(nRecv >=  (long) stop_str_len) {
            // found the required string
            if(!strncasecmp(lpszBuffer + nRecv - stop_str_len, stop_str, stop_str_len)) {
                bSuccess = true;
                break;
            }
        }
    };

    // end reading

    if(bSuccess) {
        // delete the searched string
        if(del_stop_str) {
            lpszBuffer[nRecv -  (long) stop_str_len] = '\0';
            if(dwLen)
                *dwLen =(int) nRecv - (int) stop_str_len;
        }
        else {
            lpszBuffer[nRecv] = '\0';
            if(dwLen)
                *dwLen = (int) nRecv;
        }
        char * l_buf_realloc = DAP_REALLOC(lpszBuffer,(size_t) *dwLen + 1);
        if( l_buf_realloc)
            lpszBuffer = l_buf_realloc;
        return lpszBuffer;
    }

    // in case of an error or missing string

    if(dwLen)
        *dwLen = 0;

    free(lpszBuffer);

    return NULL;
}

int json_commands(const char * a_name) {
    const char* long_cmd[] = {
            "tx_history",
            "wallet",
            "mempool",
            "ledger",
            "chain_ca_copy",
            "net"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return 1;
        }
    }
    return 0;
}

/**
 * threading function for processing a request from a client
 */
static bool s_thread_one_client_func(void *arg)
{
SOCKET  newsockfd = (SOCKET) (intptr_t) arg;
int     str_len, timeout = 5000, argc = 0, is_data, data_len = 0;
char    *str_header;

    if(s_debug_cli)
        log_it(L_DEBUG, "new connection sockfd=%"DAP_FORMAT_SOCKET, newsockfd);

    while ( !(0 > (is_data = s_poll(newsockfd, timeout))) )                 // wait data from client
    {
        if ( !(is_data) )                                                   // timeout
            continue;

        if ( !is_valid_socket(newsockfd) )
            break;

        // receiving http header
        if ( !(str_header = s_get_next_str(newsockfd, &str_len, "\r\n\r\n", true, timeout)) )
            break;                                                          // bad format

        // Parsing content length from HTTP header
        const char *l_cont_len_str = "Content-Length: ";
        char *l_str_ptr = strstr(str_header, l_cont_len_str);
        if (l_str_ptr) {
            data_len = atoi(l_str_ptr + strlen(l_cont_len_str));
        } else {
            log_it(L_ERROR, "HTTP request without length");
            break;
        }
        DAP_FREE(str_header);

        // Receiving request data
        char * str_json_command = DAP_NEW_Z_SIZE(char, data_len + 1);
        int recv_res = recv(newsockfd, str_json_command, data_len, 0);
        if (recv_res != data_len) {
            printf("The received data size: %d differs from the readed data size ->%d, errno: %d\n", data_len, recv_res, errno);
            break;
        }
        dap_json_rpc_request_t * request = dap_json_rpc_request_from_json(str_json_command);
        DAP_FREE(str_json_command);

        if(request) {
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
            dap_json_rpc_params_t * params = request->params;
            
            char *str_cmd = dap_json_rpc_params_get(params, 0);
            int res = -1;
            char *str_reply = NULL;
            json_object* json_com_res = json_object_new_array();
            if(l_cmd){
                if(l_cmd->overrides.log_cmd_call)
                    l_cmd->overrides.log_cmd_call(str_cmd);
                else {
                    char *l_str_cmd = dap_strdup(str_cmd);
                    char *l_ptr = strstr(l_str_cmd, "-password");
                    if (l_ptr) {
                        l_ptr += 10;
                        while(l_ptr[0] != '\0' && l_ptr[0] != ';') {
                            *l_ptr = '*';
                            l_ptr +=1;
                        }
                    }
                    log_it(L_DEBUG, "execute command=%s", l_str_cmd);
                    DAP_DELETE(l_str_cmd);
                }

                char ** l_argv = dap_strsplit(str_cmd, ";", -1);
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
                if(l_cmd &&  l_argv && l_cmd->func) {
                    if (json_commands(cmd_name)) {
                        res = l_cmd->func(argc, l_argv, (void *)&json_com_res);
                    } else if (l_cmd->arg_func) {
                        res = l_cmd->func_ex(argc, l_argv, l_cmd->arg_func, (void *)&str_reply);
                    } else {
                        res = l_cmd->func(argc, l_argv, (void *)&str_reply);
                    }
                } else if (l_cmd) {
                    log_it(L_WARNING,"NULL arguments for input for command \"%s\"", str_cmd);
                    dap_json_rpc_error_add(-1, "NULL arguments for input for command \"%s\"", str_cmd);
                }else {
                    log_it(L_WARNING,"No function for command \"%s\" but it registred?!", str_cmd);
                    dap_json_rpc_error_add(-1, "No function for command \"%s\" but it registred?!", str_cmd);
                }
                // find '-verbose' command
                l_verbose = dap_cli_server_cmd_find_option_val(l_argv, 1, argc, "-verbose", NULL);
                dap_strfreev(l_argv);
            } else {
                dap_json_rpc_error_add(-1, "can't recognize command=%s", str_cmd);
                log_it(L_ERROR,"Reply string: \"%s\"", str_reply);
            }
            char *reply_body = NULL;
            // -verbose 
            if(l_verbose) {
                if (str_reply) {
                    reply_body = dap_strdup_printf("%d\r\nret_code: %d\r\n%s\r\n", res, res, (str_reply) ? str_reply : "");
                } else {
                    json_object* json_res = json_object_new_object();
                    json_object_object_add(json_res, "ret_code", json_object_new_int(res));
                    json_object_array_add(json_com_res, json_res);
                }
            } else
                reply_body = dap_strdup(str_reply);
            
            // create response 
            dap_json_rpc_response_t* response = NULL;
            if (reply_body) {
                response = dap_json_rpc_response_create(reply_body, TYPE_RESPONSE_STRING, request->id);
            } else {
                response = dap_json_rpc_response_create(json_object_get(json_com_res), TYPE_RESPONSE_JSON, request->id);
            }
            json_object_put(json_com_res);
            const char* response_string = dap_json_rpc_response_to_string(response);
            // send the result of the command function
            char *reply_str = dap_strdup_printf("HTTP/1.1 200 OK\r\n"
                                                "Content-Length: %zu\r\n\r\n"
                                                "%s", strlen(response_string), response_string);
            size_t l_reply_step = 32768;
            size_t l_reply_len = strlen(reply_str);
            size_t l_reply_rest = l_reply_len;

            while(l_reply_rest) {
                size_t l_send_bytes = dap_min(l_reply_step, l_reply_rest);
                int ret = send(newsockfd, reply_str + l_reply_len - l_reply_rest, l_send_bytes, MSG_NOSIGNAL);
                if(ret<=0)
                    break;
                l_reply_rest-=l_send_bytes;
            };

            // DAP_DELETE(reply_body);
            // DAP_DELETE(str_cmd);
            // str_cmd and reply_body are freed here 
            dap_json_rpc_response_free(response);
            dap_json_rpc_request_free(request);
            DAP_DEL_Z(response_string);
            DAP_DELETE(str_reply);
            DAP_DELETE(reply_str);
        }
        break;
    }

    // close connection
    int cs = closesocket(newsockfd);
    if (s_debug_cli)
        log_it(L_DEBUG, "close connection=%d sockfd=%"DAP_FORMAT_SOCKET, cs, newsockfd);

    return false;
}


/**
 * @brief thread_main_func
 * main threading server function
 * @param args
 * @return void*
 */
static void* s_thread_main_func(void *args)
{
    SOCKET sockfd = (SOCKET) (intptr_t) args;
    SOCKET newsockfd;

    log_it( L_INFO, "Server start socket = %s", dap_config_get_item_str( g_config, "conserver", "listen_unix_socket_path") );
    // wait of clients
    while(1)
    {
        struct sockaddr_in peer;
        socklen_t size = sizeof(peer);
        // received a new connection request
        if((newsockfd = accept(sockfd, (struct sockaddr*) &peer, &size)) == (SOCKET) -1) {
            log_it(L_ERROR, "new connection break newsockfd=%"DAP_FORMAT_SOCKET, newsockfd);
            break;
        }
        // Serve client connection on automatically chosen processing thread
        dap_proc_thread_callback_add(NULL, s_thread_one_client_func, DAP_INT_TO_POINTER(newsockfd));
    };
    // close connection
    int cs = closesocket(sockfd);
    log_it(L_INFO, "Exit server thread=%d socket=%"DAP_FORMAT_SOCKET, cs, sockfd);
    return NULL;
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
    return s_commands;
}

/**
 * @brief dap_cli_server_cmd_find
 * @param a_name
 * @return
 */
dap_cli_cmd_t* dap_cli_server_cmd_find(const char *a_name)
{
    dap_cli_cmd_t *l_cmd_item = NULL;
    HASH_FIND_STR(s_commands,a_name,l_cmd_item);
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
