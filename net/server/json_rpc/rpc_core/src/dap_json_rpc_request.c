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
    dap_json_rpc_request_t *request = DAP_NEW(dap_json_rpc_request_t);
    if (!request) {
        log_it(L_CRITICAL, "Memory allocation error");
    }
    request->method = dap_strdup(a_method);
    request->params = a_params;
    request->id = a_id;
    return request;
}

void dap_json_rpc_request_free(dap_json_rpc_request_t *request) {
    if (request) {
        DAP_DEL_Z(request->method);
        if (request->params) 
            dap_json_rpc_params_remove_all(request->params);
        DAP_FREE(request);
    }
}

dap_json_rpc_request_t *dap_json_rpc_request_from_json(const char *a_data)
{
    enum json_tokener_error jterr;
    bool err_parse_request = false;
    json_object *jobj = json_tokener_parse_verbose(a_data, &jterr);
    json_object *jobj_id = NULL;
    json_object *jobj_method = NULL;
    json_object *jobj_params = NULL;

    dap_json_rpc_request_t *request = DAP_NEW_Z(dap_json_rpc_request_t);
    if (!request) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    if (jterr == json_tokener_success){
        if (json_object_object_get_ex(jobj, "id", &jobj_id)){
            request->id = json_object_get_int64(jobj_id);
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching id request");
            err_parse_request = true;
        }

        if (json_object_object_get_ex(jobj, "method", &jobj_method)){
            request->method = dap_strdup(json_object_get_string(jobj_method));
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching method for request with id: %"DAP_UINT64_FORMAT_U, request->id);
            err_parse_request = true;
        }

        if (json_object_object_get_ex(jobj, "params", &jobj_params) && !err_parse_request){
            request->params = dap_json_rpc_params_create_from_array_list(jobj_params);
        }else{
            log_it(L_ERROR, "Error parse JSON string, Can't searching array params for request with id: %"DAP_UINT64_FORMAT_U, request->id);
            err_parse_request = true;
        }
    } else {
        log_it(L_ERROR, "Error parse json tokener: %s", json_tokener_error_desc(jterr));
        err_parse_request = true;
    }
    if (err_parse_request){
        DAP_FREE(request->method);
        DAP_FREE(request);
        return NULL;
    }
    return request;
}

char *dap_json_rpc_request_to_json_string(const dap_json_rpc_request_t *a_request)
{
    char *params_json = dap_json_rpc_params_get_string_json(a_request->params);
    if (!params_json) {
        log_it(L_CRITICAL, "Failed to generate JSON for params");
        return NULL;
    }

    char *l_str = dap_strdup_printf(
        "{\"method\":\"%s\", \"params\":%s, \"id\":\"%llu\" }",
        a_request->method, params_json, a_request->id);

    DAP_FREE(params_json); 
    return l_str;
}

void dap_json_rpc_request_send(dap_json_rpc_request_t *a_request, dap_json_rpc_response_handler_func_t *response_handler,
                               const char *a_uplink_addr, const uint16_t a_uplink_port,
                               dap_client_http_callback_error_t func_error)
{
    uint64_t l_id_response = dap_json_rpc_response_registration(response_handler);
    a_request->id = l_id_response;
    char *l_str = dap_json_rpc_request_to_json_string(a_request);
    log_it(L_NOTICE, "Sending request in address: %s", a_uplink_addr);
    dap_client_http_request(NULL,a_uplink_addr, a_uplink_port, "POST", "application/json", s_url_service, l_str, strlen(l_str),
                            NULL, dap_json_rpc_response_accepted, func_error, NULL, NULL);
}
