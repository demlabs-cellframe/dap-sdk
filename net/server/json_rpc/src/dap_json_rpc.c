#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_http_server.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "exec_cmd"

static bool init_module = false;

int dap_json_rpc_init(dap_server_t* a_http_server)
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

    dap_json_rpc_request_init(DAP_EXEC_CMD_URL);
    dap_http_simple_proc_add(l_http, DAP_EXEC_CMD_URL, 24000, dap_json_rpc_http_proc);
    return 0;
}

void dap_json_rpc_deinit()
{
    //
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
