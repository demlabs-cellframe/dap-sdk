#include "dap_json_rpc_request_handler.h"
#include "dap_cli_server.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_json_rpc.h"

#define LOG_TAG "dap_json_rpc_request_handler"

static dap_json_rpc_request_handler_t *s_handler_hash_table = NULL;

int dap_json_rpc_registration_request_handler(const char *a_name, handler_func_t *a_func)
{
    dap_json_rpc_request_handler_t *l_handler = NULL;
    HASH_FIND_STR(s_handler_hash_table, a_name, l_handler);
    if (l_handler == NULL){
        l_handler = DAP_NEW(dap_json_rpc_request_handler_t);
        if (!l_handler) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return -1;
        }
        l_handler->name = dap_strdup(a_name);
        l_handler->func = a_func;
        HASH_ADD_STR(s_handler_hash_table, name, l_handler);
        log_it(L_NOTICE, "Registration handler for request name: %s", a_name);
        return 0;
    }
    return 1;
}
int dap_json_rpc_unregistration_request_handler(const char *a_name)
{
    dap_json_rpc_request_handler_t *l_handler = NULL;
    HASH_FIND_STR(s_handler_hash_table, a_name, l_handler);
    if (l_handler == NULL){
        return 1;
    } else {
    	log_it(L_NOTICE, "Unregistration for handler request name: %s", a_name);
        HASH_DEL(s_handler_hash_table, l_handler);
        DAP_FREE(l_handler->name);
        DAP_FREE(l_handler);
        return 0;
    }
}

int dap_json_rpc_request_handler(const char * a_request,  dap_http_simple_t *a_http_simple)
{
    if (!a_request) {
        log_it(L_ERROR, "Empty request");
        return -1;
    }
    log_it(L_INFO, "Processing exec_cmd request");
    dap_json_rpc_http_request_t* l_http_request = dap_json_rpc_http_request_deserialize(a_request, a_http_simple->request_size);
    if (!l_http_request) {
        log_it(L_ERROR, "Can't read request");
        return -2;
    }
    char * l_data_str = dap_json_rpc_request_to_json_string(l_http_request->request);
    dap_hash_fast_t l_sign_pkey_hash;
    bool l_sign_correct = false;
    dap_sign_t * l_sign = (dap_sign_t*)(l_http_request->tsd_n_signs);
    dap_sign_get_pkey_hash(l_sign, &l_sign_pkey_hash);
    l_sign_correct =  dap_check_node_pkey_in_map(&l_sign_pkey_hash);
    if (l_sign_correct)
        l_sign_correct = !dap_sign_verify_all(l_sign, l_http_request->header.signs_size, l_data_str, sizeof(l_data_str));
    const char* l_response = dap_cli_cmd_exec(l_data_str);
    size_t res = dap_http_simple_reply(a_http_simple, (void*)l_response, strlen(l_response));
    if (!res)
        log_it(L_ERROR, "Error in json-rpc reply");
    log_it(L_INFO, "reply for exec_cmd");
    DAP_DEL_Z(l_response);
    return 0;
}
