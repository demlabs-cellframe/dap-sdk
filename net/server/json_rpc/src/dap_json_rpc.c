#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_json_rpc_response_handler.h"
#include "dap_http_server.h"
#include "dap_pkey.h"
#include "dap_config.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "/exec_cmd"

static bool exec_cmd_module = false;
typedef struct dap_exec_cmd_pkey {
    dap_hash_fast_t pkey;
    UT_hash_handle hh;
} dap_exec_cmd_pkey_t;
static dap_exec_cmd_pkey_t *s_exec_cmd_map;
static pthread_rwlock_t s_exec_cmd_rwlock;

static int dap_json_rpc_map_init(dap_config_t *a_config) {
    s_exec_cmd_map = NULL;
    uint16_t  l_array_length = 0;
    const char ** l_pkeys = dap_config_get_array_str(a_config, "server", "exec_cmd", &l_array_length);
    for (size_t i = 0; i < l_array_length; i++) {
        dap_hash_fast_t l_pkey = {0};
        dap_chain_hash_fast_from_str(l_pkeys[i], &l_pkey);
        dap_exec_cmd_pkey_t* l_exec_cmd_pkey = DAP_NEW_Z(dap_exec_cmd_pkey_t);
        l_exec_cmd_pkey->pkey = l_pkey;
        HASH_ADD(hh, s_exec_cmd_map, pkey, sizeof(dap_exec_cmd_pkey_t), l_exec_cmd_pkey);
    }
    return 0;
}

static int dap_json_rpc_map_deinit() {
    dap_exec_cmd_pkey_t* l_pkey = NULL, *tmp = NULL;
    HASH_ITER(hh, s_exec_cmd_map, l_pkey, tmp) {
        HASH_DEL(s_exec_cmd_map, l_pkey);
        DAP_DEL_Z(l_pkey);
    }
    return 0;
}

bool dap_check_node_pkey_in_map(dap_hash_fast_t *a_pkey){
    dap_exec_cmd_pkey_t* l_exec_cmd_pkey = NULL, *tmp = NULL;
    HASH_ITER(hh, s_exec_cmd_map, l_exec_cmd_pkey, tmp) {
        if (dap_hash_fast_compare(&l_exec_cmd_pkey->pkey, a_pkey))
            return true;
    }
    return false;
}

dap_client_http_callback_error_t * dap_json_rpc_error_callback() {

}

int dap_json_rpc_init(dap_server_t* a_http_server, dap_config_t *a_config)
{
    exec_cmd_module = true;
    if (!a_http_server) {
        log_it(L_ERROR, "Can't find server for %s", DAP_EXEC_CMD_URL);
        return -1;
    }

    dap_http_server_t * l_http = DAP_HTTP_SERVER(a_http_server);
    if(!l_http){
        log_it(L_ERROR, "Can't find http server for %s", DAP_EXEC_CMD_URL);
        return -2;
    }

    dap_json_rpc_map_init(a_config);
    dap_json_rpc_request_init("/exec_cmd");
    dap_http_simple_proc_add(l_http, "/exec_cmd", 24000, dap_json_rpc_http_proc);
    return 0;
}

bool dap_json_rpc_exec_cmd_inited(){
    return exec_cmd_module;
}

void dap_json_rpc_deinit()
{
    dap_json_rpc_map_deinit();
}

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    log_it(L_INFO, "Proc exec_cmd request");
    http_status_code_t *l_return_code = (http_status_code_t*)a_arg;
    *l_return_code = Http_Status_OK;
    strcpy(a_http_simple->reply_mime, "application/json");

    // const char* l_query = a_http_simple->http_client->in_query_string;
    // uint32_t l_query_length = a_http_simple->http_client->in_query_string_len;
    *l_return_code = Http_Status_OK;
    if (!a_http_simple->request){
        *l_return_code = Http_Status_NoContent;
        dap_http_simple_reply_f(a_http_simple, "JSON-RPC request was not formed. "
                                          "JSON-RPC must represent a method containing objects from an object, "
                                          "this is a string the name of the method, id is a number and params "
                                          "is an array that can contain strings, numbers and boolean values.");
    }
    dap_json_rpc_request_handler(a_http_simple->http_client->in_query_string, a_http_simple);
}
