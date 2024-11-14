/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_cli_server.h"
#include "dap_app_cli.h"
#include "dap_app_cli_net.h"
#include "dap_app_cli_shell.h"
#include "dap_json_rpc_params.h"
#include "dap_json_rpc_request.h"

#ifdef DAP_OS_ANDROID
#include <android/log.h>
#include <jni.h>
static dap_config_t *cli_config;
#endif
#define LOG_TAG "node-cli"

/**
 * split string to argc and argv
 */
static char** split_word(char *line, int *argc)
{
    if(!line)
    {
        if(argc)
            *argc = 0;
        return NULL ;
    }
    char **argv = calloc(sizeof(char*), strlen(line));
    if (!argv) {
        return NULL;
    }
    int n = 0;
    char *s, *start = line;
    size_t len = strlen(line);
    for(s = line; s <= line + len; s++) {
        if(whitespace(*s)) {
            *s = '\0';
            argv[n] = start;
            s++;
            // miss spaces
            for(; whitespace(*s); s++)
                ;
            start = s;
            n++;
        }
    }
    // last param
    if(len) {
        argv[n] = start;
        n++;
    }
    if(argc)
        *argc = n;
    return argv;
}

/**
 *  Read and execute commands until EOF is reached.  This assumes that
 *  the input source has already been initialized.
 */
int execute_line(dap_app_cli_connect_param_t cparam, char *line)
{
    register int i;
    char *word;

    /* Isolate the command word. */
    i = 0;
    while(line[i] && whitespace(line[i]))
        i++;
    word = line + i;

    int argc = 0;
    char **argv = split_word(word, &argc);

    // Call the function
    if(argc > 0) {
        dap_app_cli_cmd_state_t cmd = {
            .cmd_name           = (char*)argv[0],
            .cmd_param_count    = argc - 1,
            .cmd_param          = argc - 1 > 0 ? (char**)(argv + 1) : NULL
        };
        // Send command
        int res = dap_app_cli_post_command(cparam, &cmd);
        DAP_DELETE(argv);
        return res;
    }
    fprintf(stderr, "No command\n");
    DAP_DELETE(argv);
    return -1;
}

/**
 *  Read and execute commands until EOF is reached.  This assumes that
 *  the input source has already been initialized.
 */
static int shell_reader_loop()
{
    char *line, *s;

    rl_initialize(); /* Bind our completer. */
    int done = 0;
    // Loop reading and executing lines until the user quits.
    while (!done) {
        // Read a line of input
        if ( !(line = rl_readline("> ")) ) {
            printf("\r\n");
            break;
        }

        /* Remove leading and trailing whitespace from the line.
         Then, if there is anything left, add it to the history list
         and execute it. */
        if (*(s = dap_strstrip(line)) )
        {
            dap_app_cli_connect_param_t cparam = dap_app_cli_connect();
            if ( (dap_app_cli_connect_param_t)~0 == cparam )
                return DAP_DELETE(line), printf("Can't connect to CLI server\r\n"), -3;
            add_history(s);
            execute_line(cparam, s);
            dap_app_cli_disconnect(cparam);
        }
        DAP_DELETE(line);
    }
    return 0;
}


char *dap_cli_exec(int argc, char **argv) {

    dap_app_cli_cmd_state_t cmd = {
            .cmd_name           = (char*)argv[0],
            .cmd_param_count    = argc - 2,
            .cmd_param          = argc - 2 > 0 ? (char**)(argv + 1) : NULL
    };

    char *l_cmd_str = dap_app_cli_form_command(&cmd);
    dap_json_rpc_params_t *params = dap_json_rpc_params_create();
    dap_json_rpc_params_add_data(params, l_cmd_str, TYPE_PARAM_STRING);
    DAP_DELETE(l_cmd_str);
    dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(cmd.cmd_name, params, 0);
    char    *req_str = dap_json_rpc_request_to_json_string(a_request),
            *res = dap_cli_cmd_exec(req_str);
    dap_json_rpc_request_free(a_request);
    return res;


}

#ifdef DAP_OS_ANDROID
JNIEXPORT jstring JNICALL Java_com_CellframeWallet_Node_cellframeNodeCliMain(JNIEnv *javaEnv, jobject __unused jobj, jobjectArray argvStr)
{
    //g_sys_dir_path = dap_strdup_printf("/storage/emulated/0/Android/data/com.CellframeWallet/files/node");
    dap_cli_cmd_t *l1 = NULL, *l2 = NULL;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Config %p", g_config);
    HASH_ITER(hh, dap_cli_server_cmd_get_first(), l1, l2) {
         __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Command %s", l1->name);
    }
    jsize argc = (*javaEnv)->GetArrayLength(javaEnv, argvStr);
    char **argv = malloc(sizeof(char*) * argc);
    for (jsize i = 1; i < argc; ++i) {
        jstring string = (jstring)((*javaEnv)->GetObjectArrayElement(javaEnv, argvStr, i));
        const char *cstr = (*javaEnv)->GetStringUTFChars(javaEnv, string, 0);
        argv[i - 1] = strdup(cstr);
         __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Param %d: %s", i, argv[i - 1]);
        (*javaEnv)->ReleaseStringUTFChars(javaEnv, string, cstr );
        (*javaEnv)->DeleteLocalRef(javaEnv, string );
    }
    if ( argc > 1 ) {
        dap_app_cli_cmd_state_t cmd = {
            .cmd_name           = (char*)argv[0],
            .cmd_param_count    = argc - 2,
            .cmd_param          = argc - 2 > 0 ? (char**)(argv + 1) : NULL
        };
        char *l_cmd_str = dap_app_cli_form_command(&cmd);
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Full request %s", l_cmd_str);
        dap_json_rpc_params_t *params = dap_json_rpc_params_create();
        dap_json_rpc_params_add_data(params, l_cmd_str, TYPE_PARAM_STRING);
        DAP_DELETE(l_cmd_str);
        dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(cmd.cmd_name, params, 0);
        char    *req_str = dap_json_rpc_request_to_json_string(a_request),
                *res = dap_cli_cmd_exec(req_str);
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Full command %s", req_str);
        dap_json_rpc_request_free(a_request);
        for(jsize i = 0; i < argc - 1; ++i)
            free(argv[i]);
        free(argv);
        jstring jres = (*javaEnv)->NewStringUTF(javaEnv, res);
        DAP_DELETE(res);
        DAP_DELETE(req_str);
        return jres;
    } else {
        return (*javaEnv)->NewStringUTF(javaEnv, "Empty command");
    }
}
#endif
int dap_app_cli_main(const char *a_app_name, int a_argc, const char **a_argv)
{
    {
        char l_config_dir[MAX_PATH + 1];
        snprintf(l_config_dir, MAX_PATH, "%s/etc", g_sys_dir_path);
        if ( dap_config_init(l_config_dir) || !(g_config = dap_config_open(a_app_name)) )
            return printf("Can't init general config \"%s/%s.cfg\"\n", l_config_dir, a_app_name), -3;
    }
    int l_res = -1;
    if (a_argc > 1) {
        // Call the function
        dap_app_cli_cmd_state_t cmd = {
            .cmd_name           = (char *)a_argv[1],
            .cmd_param_count    = a_argc - 2,
            .cmd_param          = a_argc - 2 > 0 ? (char**)(a_argv + 2) : NULL
        };
        // Send command
        dap_app_cli_connect_param_t cparam = dap_app_cli_connect();
        if ( (dap_app_cli_connect_param_t)~0 == cparam )
            return printf("Can't connect to CLI server\r\n"), -3;
        l_res = dap_app_cli_post_command(cparam, &cmd);
        dap_app_cli_disconnect(cparam);
    } else {
        // no command passed, start interactive shell
        l_res = shell_reader_loop();
    }
    dap_config_close(g_config);
    return l_res;
}

