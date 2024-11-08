#include "dap_json_rpc_request_handler.h"
#include "dap_cli_server.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_json_rpc.h"

#define LOG_TAG "dap_json_rpc_request_handler"

static dap_json_rpc_request_handler_t *s_handler_hash_table = NULL;

int dap_json_rpc_registration_request_handler(const char *a_name, handler_func_t *a_func)
{

    if (!a_request) {
        log_it(L_ERROR, "Empty request");
        return NULL;
    }
    log_it(L_INFO, "Processing exec_cmd request");
    dap_json_rpc_http_request_t* l_http_request = dap_json_rpc_http_request_deserialize(a_request, a_request_size);
    if (!l_http_request) {
        log_it(L_ERROR, "Can't read request");
        return NULL;
    }
    char * l_data_str = dap_json_rpc_request_to_json_string(l_http_request->request);
    dap_hash_fast_t l_sign_pkey_hash;
    bool l_sign_correct = false;
    dap_sign_t * l_sign = (dap_sign_t*)(l_http_request->tsd_n_signs);
    dap_sign_get_pkey_hash(l_sign, &l_sign_pkey_hash);
    l_sign_correct =  dap_check_node_pkey_in_map(&l_sign_pkey_hash);
    if (l_sign_correct)
        l_sign_correct = !dap_sign_verify_all(l_sign, l_http_request->header.signs_size, l_data_str, sizeof(l_data_str));
    if (!l_sign_correct) {
        dap_json_rpc_response_t* l_no_rights_res = dap_json_rpc_response_create("You have no rights", TYPE_RESPONSE_STRING, l_http_request->request->id);
        char * l_no_rights_res_str = dap_json_rpc_response_to_string(l_no_rights_res);
        dap_json_rpc_http_request_free(l_http_request);
        return l_no_rights_res_str;
    }
    char* l_response = dap_cli_cmd_exec(l_data_str);
    dap_json_rpc_http_request_free(l_http_request);
    DAP_DEL_MULTY(l_data_str);
    return l_response;
}
