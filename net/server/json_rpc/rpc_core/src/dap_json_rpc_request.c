#include "dap_json_rpc_request.h"
#include "dap_cert.h"

#define LOG_TAG "dap_json_rpc_request"

static char *s_url_service = NULL;

int dap_json_rpc_request_init(const char *a_url_service)
{
    if (s_url_service == NULL)
    {
        s_url_service = dap_strdup(a_url_service);
        return 0;
    }
    return 1;
}

dap_json_rpc_request_t *dap_json_rpc_request_creation(const char *a_method, dap_json_rpc_params_t *a_params, int64_t a_id)
{
    dap_json_rpc_request_t *request = DAP_NEW(dap_json_rpc_request_t);
    if (!request)
    {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    }
    request->method = dap_strdup(a_method);
    request->params = a_params;
    request->id = a_id;
    return request;
}

void dap_json_rpc_request_free(dap_json_rpc_request_t *request)
{
    if (request)
    {
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
    if (!request)
    {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    if (jterr == json_tokener_success)
    {
        if (json_object_object_get_ex(jobj, "id", &jobj_id))
        {
            request->id = json_object_get_int64(jobj_id);
        }
        else
        {
            log_it(L_ERROR, "Error parse JSON string, Can't searching id request");
            err_parse_request = true;
        }

        if (json_object_object_get_ex(jobj, "method", &jobj_method))
        {
            request->method = dap_strdup(json_object_get_string(jobj_method));
        }
        else
        {
            log_it(L_ERROR, "Error parse JSON string, Can't searching method for request with id: %" DAP_UINT64_FORMAT_U, request->id);
            err_parse_request = true;
        }

        if (json_object_object_get_ex(jobj, "params", &jobj_params) && !err_parse_request)
        {
            request->params = dap_json_rpc_params_create_from_array_list(jobj_params);
        }
        else
        {
            log_it(L_ERROR, "Error parse JSON string, Can't searching array params for request with id: %" DAP_UINT64_FORMAT_U, request->id);
            err_parse_request = true;
        }
    }
    else
    {
        log_it(L_ERROR, "Error parse json tokener: %s", json_tokener_error_desc(jterr));
        err_parse_request = true;
    }
    if (err_parse_request)
    {
        DAP_FREE(request->method);
        DAP_FREE(request);
        json_object_put(jobj);
        return NULL;
    }
    json_object_put(jobj);
    return request;
}

char *dap_json_rpc_request_to_json_string(const dap_json_rpc_request_t *a_request)
{
    char *params_json = dap_json_rpc_params_get_string_json(a_request->params);
    if (!params_json)
    {
        log_it(L_CRITICAL, "Failed to generate JSON for params");
        return NULL;
    }

    char *l_str = dap_strdup_printf(
        "{\"method\":\"%s\", \"params\":%s, \"id\":\"%" DAP_UINT64_FORMAT_U "\" }",
        a_request->method, params_json, a_request->id);

    DAP_FREE(params_json);
    return l_str;
}

char *dap_json_rpc_http_request_serialize(dap_json_rpc_http_request_t *a_request, size_t *a_total_size)
{
    *a_total_size = a_request->header.data_size + a_request->header.signs_size + sizeof(a_request->header);

    char *a_output = DAP_NEW_SIZE(char, *a_total_size);
    if (!a_output) {
        return NULL;
    }

    char *ptr = a_output;

    memcpy(ptr, &a_request->header, sizeof(a_request->header));
    ptr += sizeof(a_request->header);

    char *l_str = dap_json_rpc_request_to_json_string(a_request->request);
    if (!l_str) {
        DAP_DEL_Z(a_output);
        return NULL;
    }

    memcpy(ptr, l_str, a_request->header.data_size);
    ptr += a_request->header.data_size;
    DAP_DEL_Z(l_str);

    memcpy(ptr, a_request->tsd_n_signs, a_request->header.signs_size);

    return a_output;
}

dap_json_rpc_http_request_t *dap_json_rpc_http_request_deserialize(const void *data, size_t data_size)
{
    char *ptr = (char *)data;
    dap_json_rpc_http_request_t *l_http_request = DAP_NEW_Z(dap_json_rpc_http_request_t);
    if (!l_http_request)
        return NULL;

    memcpy(&l_http_request->header, ptr, sizeof(l_http_request->header));
    ptr += sizeof(l_http_request->header);

    if (data_size < (sizeof(l_http_request->header) + l_http_request->header.data_size + l_http_request->header.signs_size)) {
        log_it(L_ERROR, "Wrong size of request");
        DAP_DEL_Z(l_http_request);
        return NULL;
    }

    char *l_request_str = DAP_NEW_Z_SIZE(char, l_http_request->header.data_size);
    if (!l_request_str) {
        DAP_DEL_Z(l_http_request);
        return NULL;
    }

    memcpy(l_request_str, ptr, l_http_request->header.data_size);
    l_http_request->request = dap_json_rpc_request_from_json(l_request_str);
    DAP_DEL_Z(l_request_str);

    if (!l_http_request->request) {
        log_it(L_ERROR, "Can't parse request from string");
        DAP_DEL_Z(l_http_request);
        return NULL;
    }
    ptr += l_http_request->header.data_size;

    if (l_http_request->header.signs_size > 0) {
        l_http_request = DAP_REALLOC(l_http_request, sizeof(dap_json_rpc_http_request_t) + l_http_request->header.signs_size);
        if (!l_http_request) {
            return NULL;
        }
        memcpy(l_http_request->tsd_n_signs, ptr, l_http_request->header.signs_size);
    }

    return l_http_request;
}

void dap_json_rpc_http_request_free(dap_json_rpc_http_request_t *a_http_request)
{
    if (!a_http_request)
        return;

    if (a_http_request->request)
        dap_json_rpc_request_free(a_http_request->request);

    DAP_DEL_Z(a_http_request);
}

dap_json_rpc_http_request_t *dap_json_rpc_request_sign_by_cert(dap_json_rpc_request_t *a_request, dap_cert_t *a_cert)
{
    char *l_str = dap_json_rpc_request_to_json_string(a_request);
    dap_sign_t *l_sign = dap_cert_sign(a_cert, l_str, sizeof(l_str), 0);
    if (!l_sign)
    {
        log_it(L_ERROR, "Decree signing failed");
        DAP_DELETE(l_str);
        return NULL;
    }
    dap_json_rpc_http_request_t *ret = DAP_NEW_Z(dap_json_rpc_http_request_t);
    size_t l_sign_size = dap_sign_get_size(l_sign);
    ret->request = a_request;
    ret->header.data_size = strlen(l_str);
    ret->header.signs_size = l_sign_size;
    if (ret->header.signs_size >0) {
        ret = DAP_REALLOC(ret, sizeof(dap_json_rpc_http_request_t) + ret->header.signs_size);
        memcpy(ret->tsd_n_signs, l_sign, l_sign_size);
    }
    DAP_DELETE(l_str);
    DAP_DELETE(l_sign);
    return ret;
}

int dap_json_rpc_request_send(dap_json_rpc_request_t *a_request, void* response_handler,
                               const char *a_uplink_addr, const uint16_t a_uplink_port,
                               dap_client_http_callback_error_t * func_error)
{
    uint64_t l_id_response = dap_json_rpc_response_registration(response_handler);
    a_request->id = 0;
    dap_cert_t *l_cert = dap_cert_find_by_name("node-addr");
    if (!l_cert) {
        log_it(L_ERROR, "Can't load cert");
        return -1;
    }
    dap_json_rpc_http_request_t *l_http_request = dap_json_rpc_request_sign_by_cert(a_request, l_cert);
    size_t l_http_length = 0;
    char *l_http_str = dap_json_rpc_http_request_serialize(l_http_request, &l_http_length);

    log_it(L_NOTICE, "Sending request in address: %s", a_uplink_addr);
    dap_client_http_request(dap_worker_get_auto(), a_uplink_addr, a_uplink_port, "POST", "application/json", "exec_cmd", l_http_str, l_http_length,
                            NULL, response_handler, func_error, NULL, NULL);
    DAP_DEL_Z(l_http_request);
    DAP_DEL_Z(l_http_str);
    return 0;
}


char* dap_json_rpc_request_to_http_str(dap_json_rpc_request_t *a_request) {
    uint64_t l_id_response = dap_json_rpc_response_registration(a_request);
    a_request->id = 0;
    dap_cert_t *l_cert = dap_cert_find_by_name("node-addr");
    if (!l_cert) {
        log_it(L_ERROR, "Can't load cert");
        return NULL;
    }
    dap_json_rpc_http_request_t *l_http_request = dap_json_rpc_request_sign_by_cert(a_request, l_cert);
    size_t l_http_length = 0;
    char *l_http_str = dap_json_rpc_http_request_serialize(l_http_request, &l_http_length);
    return l_http_str;
}
