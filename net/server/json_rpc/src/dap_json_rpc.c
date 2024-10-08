#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_http_server.h"
#include "dap_pkey.h"
#include "dap_config.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "/exec_cmd"

static bool init_module = false;
typedef struct dap_exec_cmd_pkey {
    dap_pkey_t *pkey;
    UT_hash_handle hh;
} dap_exec_cmd_pkey_t;
static dap_exec_cmd_pkey_t *s_exec_cmd_map;
static pthread_rwlock_t s_exec_cmd_rwlock;

static int dap_json_rpc_map_init(dap_config_t *a_config) {
    s_exec_cmd_map = NULL;
    uint16_t  l_array_length = 0;
    const char ** l_pkeys = dap_config_get_array_str(a_config, "server", "exec_cmd", &l_array_length);
    for (size_t i = 0; i < l_array_length; i++) {
        dap_pkey_t *l_ret = DAP_NEW_SIZE(dap_pkey_t, sizeof(dap_pkey_t) + sizeof(l_pkeys[i]));
        dap_pkey_type_t l_ret_type = {0};
        l_ret_type.type = DAP_PKEY_TYPE_SIGN_DILITHIUM;
        l_ret->header.type = l_ret_type;
        l_ret->header.size = (uint32_t)sizeof(l_pkeys[i]);
        memcpy(&l_ret->pkey, l_pkeys[i], sizeof(l_pkeys[i]));

        dap_exec_cmd_pkey_t* l_pkey = DAP_NEW_Z(dap_exec_cmd_pkey_t);
        l_pkey->pkey = l_ret;
        HASH_ADD_PTR(s_exec_cmd_map, pkey, l_pkey);
    }
    return 0;
}

static int dap_json_rpc_map_deinit() {
    dap_exec_cmd_pkey_t* l_pkey = NULL, *tmp = NULL;
    HASH_ITER(hh, s_exec_cmd_map, l_pkey, tmp) {
        DAP_DEL_Z(l_pkey->pkey);
        HASH_DEL(s_exec_cmd_map, l_pkey);
        DAP_DEL_Z(l_pkey);
    }
    return 0;
}

int dap_json_rpc_init(dap_server_t* a_http_server, dap_config_t *a_config)
{
    init_module = true;
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
    dap_json_rpc_request_init(DAP_EXEC_CMD_URL);
    dap_http_simple_proc_add(l_http, DAP_EXEC_CMD_URL, 24000, dap_json_rpc_http_proc);
    return 0;
}

void dap_json_rpc_deinit()
{
    dap_json_rpc_map_deinit();
}

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    log_it(L_DEBUG, "Proc exec_cmd request");
    http_status_code_t *l_return_code = (http_status_code_t*)a_arg;
    *l_return_code = Http_Status_OK;
    strcpy(a_http_simple->reply_mime, "application/json");

    const char* l_query = a_http_simple->http_client->in_query_string;
    uint32_t l_query_length = a_http_simple->http_client->in_query_string_len;
    *l_return_code = Http_Status_OK;
    if (!a_http_simple->request){
        *l_return_code = Http_Status_NoContent;
        dap_http_simple_reply_f(a_http_simple, "JSON-RPC request was not formed. "
                                          "JSON-RPC must represent a method containing objects from an object, "
                                          "this is a string the name of the method, id is a number and params "
                                          "is an array that can contain strings, numbers and boolean values.");
    }
    dap_json_rpc_request_handler(a_http_simple->request, a_http_simple);
}
