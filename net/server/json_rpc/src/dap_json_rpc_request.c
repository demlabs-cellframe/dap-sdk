#include "dap_json_rpc_request.h"

#define LOG_TAG "dap_json_rpc_request"

static char *s_url_service = NULL;

int dap_json_rpc_request_init(const char *a_url_service)
{
    if (s_url_service == NULL){
        s_url_service = dap_strdup(a_url_service);
        return  0;
    }
    return 1;
}

dap_json_rpc_request_t *dap_json_rpc_request_creation(const char *a_method, dap_json_rpc_params_t *a_params, int64_t a_id)
{
    dap_json_rpc_request_t *l_request = DAP_NEW(dap_json_rpc_request_t);
    if (!l_request) {
        log_it(L_CRITICAL, "Memory allocation error");
    }
    l_request->method = dap_strdup(a_method);
    l_request->params = a_params;
    l_request->id = a_id;
    return l_request;
}

void dap_json_rpc_request_free(dap_json_rpc_request_t *request) {
    if (request) {
        DAP_DEL_Z(request->method);
        if (request->params) 
            dap_json_rpc_params_remove_all(request->params);
        DAP_FREE(request);
    }
}


/**
 * Convert a JSON-formatted string to a dap_json_rpc_request_t structure.
 *
 * This function parses a JSON-formatted string representing a JSON-RPC request
 * and populates a dap_json_rpc_request_t structure with the extracted data.
 *
 * @param a_data The JSON-formatted string representing the JSON-RPC request.
 * @return A pointer to a dap_json_rpc_request_t structure on success,
 *         or NULL on failure (memory allocation or parsing error).
 *         Memory is properly cleaned up in case of failure.
 */
dap_json_rpc_request_t *dap_json_rpc_request_from_json(const char *a_data)
{
    log_it(L_DEBUG, "Translation JSON string to struct dap_json_rpc_request");
    enum json_tokener_error l_jterr;
    json_object *l_jobj = json_tokener_parse_verbose(a_data, &l_jterr);
    json_object *l_jobj_id = NULL;
    json_object *l_jobj_method = NULL;
    json_object *l_jobj_params = NULL;
    dap_json_rpc_request_t *l_request = DAP_NEW_Z(dap_json_rpc_request_t);
    if (!l_request) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_request->params = NULL;
    bool l_err_parse_request = false;
    if (l_jterr == json_tokener_success){
        if (json_object_object_get_ex(l_jobj, "id", &l_jobj_id)){
            l_request->id = json_object_get_int64(l_jobj_id);
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching id request");
            l_err_parse_request = true;
        }
        if (json_object_object_get_ex(l_jobj, "method", &l_jobj_method)){
            l_request->method = dap_strdup(json_object_get_string(l_jobj_method));
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching method for request with id: %"DAP_UINT64_FORMAT_U, l_request->id);
            l_err_parse_request = true;
        }
        if (json_object_object_get_ex(l_jobj, "params", &l_jobj_params) && !l_err_parse_request){
            l_request->params = dap_json_rpc_params_create_from_array_list(l_jobj_params);
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching array params for request with id: %"DAP_UINT64_FORMAT_U, l_request->id);
            l_err_parse_request = true;
        }
    } else {
        log_it(L_ERROR, "Error parse json tokener: %s", json_tokener_error_desc(l_jterr));
        l_err_parse_request = true;
    }
    if (l_err_parse_request){
        DAP_FREE(l_request->method);
        DAP_FREE(l_request);
        return NULL;
    }
    return l_request;

}

char *dap_json_rpc_request_to_json(const dap_json_rpc_request_t *a_request)
{
    log_it(L_DEBUG, "Translation struct dap_json_rpc_request to JSON string");
    char *l_str = dap_strdup_printf("{\"method\":\"%s\", \"params\":%s, \"id\":\"%llu\" }",
                                    a_request->method, dap_json_rpc_params_get_string_json(a_request->params), a_request->id);
    // char *res_str = dap_strdup_printf("{\"size:\":%d, %s", strlen(l_str), l_str);
    // char *l_str = dap_strjoin(NULL, "{\"method\":", "\"", a_request->method, "\"", ", \"params\":",
    //                           dap_json_rpc_params_get_string_json(a_request->params), ", \"id\": ", a_request->id, "}", NULL);
    return l_str;
}

void dap_json_rpc_request_send(dap_json_rpc_request_t *a_request, dap_json_rpc_response_handler_func_t *response_handler,
                               const char *a_uplink_addr, const uint16_t a_uplink_port,
                               dap_client_http_callback_error_t func_error)
{
    uint64_t l_id_response = dap_json_rpc_response_registration(response_handler);
    a_request->id = l_id_response;
    char *l_str = dap_json_rpc_request_to_json(a_request);
    log_it(L_NOTICE, "Sending request in address: %s", a_uplink_addr);
    dap_client_http_request(NULL,a_uplink_addr, a_uplink_port, "POST", "application/json", s_url_service, l_str, strlen(l_str),
                            NULL, dap_json_rpc_response_accepted, func_error, NULL, NULL);
}

// char *dap_json_rpc_request_to_send_str(const dap_json_rpc_request_t *a_request)
// {
//     log_it(L_DEBUG, "Translation struct dap_json_rpc_request to JSON string");
//     char *l_str = dap_strdup_printf("\"method\":%s, \"params\":%s, \"id\":%llu }",
//                                     a_request->method, dap_json_rpc_params_get_string_json(a_request->params), a_request->id);
//     char *res_str = dap_strdup_printf("{\"size:\":%d, %s", strlen(l_str), l_str);
//     return res_str;
// }

// int dap_json_rpc_request_send_to_server( char * cmd_name, char ** cmd_param, SOCKET *a_socket) {
//     dap_json_rpc_params_t * params = dap_json_rpc_params_create();
//     dap_json_rpc_params_add_data(params, cmd_param, TYPE_PARAM_STRING);
//     uint64_t l_id_response = dap_json_rpc_response_registration(response_handler);
//     dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(cmd_name, params, l_id_response);
//     char * send_str = dap_json_rpc_request_to_send_str(a_request);
//     ssize_t ret = send(*a_socket, send_str, strlen(send_str), 0);
//     if (ret != (ssize_t)strlen(send_str)) {
//         log_it(L_ERROR, "Error sending to server");
//         return -1;
//     }
//     return 0;
// }
