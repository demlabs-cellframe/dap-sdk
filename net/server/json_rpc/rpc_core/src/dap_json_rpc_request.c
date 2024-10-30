#include "dap_json_rpc_request.h"
#include "dap_cert.h"
#include "dap_enc.h"

#define LOG_TAG "dap_json_rpc_request"

static char *s_url_service = NULL;

struct exec_cmd_request {
    dap_client_pvt_t * client_pvt;
#ifdef DAP_OS_WINDOWS
    CONDITION_VARIABLE wait_cond;
    CRITICAL_SECTION wait_crit_sec;
#else
    pthread_cond_t wait_cond;
    pthread_mutex_t wait_mutex;
#endif
    char* response;
    size_t response_size;
    int error_code;
};

enum ExecCmdRetCode {
    EXEC_CMD_OK = 0,
    EXEC_CMD_ERR_WAIT_TIMEOUT,
    EXEC_CMD_ERR_UNKNOWN
};

int dap_json_rpc_request_init(const char *a_url_service)
{
    if (s_url_service == NULL)
    {
        s_url_service = dap_strdup(a_url_service);
        return 0;
    }
    return 1;
}

static struct exec_cmd_request* s_exec_cmd_request_init(dap_client_pvt_t * a_client_pvt)
{
    struct exec_cmd_request *l_exec_cmd_request = DAP_NEW_Z(struct exec_cmd_request);
    if (!l_exec_cmd_request)
        return NULL;
    l_exec_cmd_request->client_pvt = a_client_pvt;
#ifdef DAP_OS_WINDOWS
    InitializeCriticalSection(&l_exec_cmd_request->wait_crit_sec);
    InitializeConditionVariable(&l_exec_cmd_request->wait_cond);
#else
    pthread_mutex_init(&l_exec_cmd_request->wait_mutex, NULL);
#ifdef DAP_OS_DARWIN
    pthread_cond_init(&l_exec_cmd_request->wait_cond, NULL);
#else
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&l_exec_cmd_request->wait_cond, &attr);    
#endif
#endif
    return l_exec_cmd_request;
}

void s_exec_cmd_request_free(struct exec_cmd_request *a_exec_cmd_request)
{
    if (!a_exec_cmd_request)
        return;

#ifdef DAP_OS_WINDOWS
    DeleteCriticalSection(&a_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_destroy(&a_exec_cmd_request->wait_mutex);
    pthread_cond_destroy(&a_exec_cmd_request->wait_cond);
#endif
    DAP_DEL_Z(a_exec_cmd_request);
}

static void s_exec_cmd_response_handler(void *a_response, size_t a_response_size, void *a_arg,
                                            http_status_code_t http_status_code) {
    (void)http_status_code;
    struct exec_cmd_request *l_exec_cmd_request = (struct exec_cmd_request *)a_arg;
#ifdef DAP_OS_WINDOWS
    EnterCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_lock(&l_exec_cmd_request->wait_mutex);
#endif
    l_exec_cmd_request->response = a_response;
    l_exec_cmd_request->response_size = a_response_size;
#ifdef DAP_OS_WINDOWS
    WakeConditionVariable(&l_exec_cmd_request->wait_cond);
    LeaveCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_cond_signal(&l_exec_cmd_request->wait_cond);
    pthread_mutex_unlock(&l_exec_cmd_request->wait_mutex);
#endif
}

static void s_exec_cmd_error_handler(int a_error_code, void *a_arg){
    struct exec_cmd_request * l_exec_cmd_request = (struct exec_cmd_request *)a_arg;
#ifdef DAP_OS_WINDOWS
    EnterCriticalSection(&l_exec_cmd_request->wait_crit_sec);
    l_exec_cmd_request->error_code = a_error_code;
    WakeConditionVariable(&l_exec_cmd_request->wait_cond);
    LeaveCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_lock(&l_exec_cmd_request->wait_mutex);
    l_exec_cmd_request->error_code = a_error_code;
    pthread_cond_signal(&l_exec_cmd_request->wait_cond);
    pthread_mutex_unlock(&l_exec_cmd_request->wait_mutex);
#endif
}

static int s_exec_cmd_request_get_response(struct exec_cmd_request *a_exec_cmd_request, json_object **a_response_out, size_t *a_response_out_size)
{
    int ret = 0;
    char *l_response = NULL;
    size_t l_response_size = 0;

#ifdef DAP_OS_WINDOWS
    EnterCriticalSection(&a_exec_cmd_request->wait_crit_sec);
    if (a_exec_cmd_request->error_code != 0) {
        ret = a_exec_cmd_request->error_code;
    } else {
        l_response = a_exec_cmd_request->response;
        l_response_size = a_exec_cmd_request->response_size;
        ret = 0;
    }
    LeaveCriticalSection(&a_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_lock(&a_exec_cmd_request->wait_mutex);
    if (a_exec_cmd_request->error_code != 0) {
        ret = a_exec_cmd_request->error_code;
    } else {
        l_response = a_exec_cmd_request->response;
        l_response_size = a_exec_cmd_request->response_size;
        ret = 0; 
    }
    pthread_mutex_unlock(&a_exec_cmd_request->wait_mutex);
#endif
    if (a_exec_cmd_request->error_code) {
        log_it(L_ERROR, "Response error code: %d", ret);
        ret = - 1;
    } else if (l_response) {
            dap_client_pvt_t * l_client_pvt = a_exec_cmd_request->client_pvt;
            l_client_pvt->http_client = NULL;
            size_t l_response_dec_size_max = l_response_size ? l_response_size * 2 + 16 : 0;
            char * l_response_dec = l_response_size ? DAP_NEW_Z_SIZE(char, l_response_dec_size_max) : NULL;
            size_t l_response_dec_size = 0;
            if(l_response_size)
                l_response_dec_size = dap_enc_decode(l_client_pvt->session_key,
                        l_response, l_response_size,
                        l_response_dec, l_response_dec_size_max,
                        DAP_ENC_DATA_TYPE_RAW);
            *a_response_out = json_tokener_parse(l_response_dec);
            if (!*a_response_out && l_response_dec) {
                *a_response_out = json_object_new_string("Can't decode the response, check the access rights on the remote node");
                log_it(L_DEBUG, "Wrong response %s", json_object_new_string(l_response_dec));
                DAP_DEL_Z(l_response_dec);
            }
            *a_response_out_size = l_response_dec_size;
    } else {
        log_it(L_ERROR, "Empty response in json-rpc");
        ret = -2;
    }

    return ret;
}


static int dap_chain_exec_cmd_list_wait(struct exec_cmd_request *a_exec_cmd_request, int a_timeout_ms) {
#ifdef DAP_OS_WINDOWS
    EnterCriticalSection(&a_exec_cmd_request->wait_crit_sec);
    if (a_exec_cmd_request->response)
        return LeaveCriticalSection(&a_exec_cmd_request->wait_crit_sec), a_exec_cmd_request->response;
    while (!a_exec_cmd_request->response) {
        if ( !SleepConditionVariableCS(&a_exec_cmd_request->wait_cond, &a_exec_cmd_request->wait_crit_sec, a_timeout_ms) )
            a_exec_cmd_request->response = GetLastError() == ERROR_TIMEOUT ? EXEC_CMD_ERR_WAIT_TIMEOUT : EXEC_CMD_ERR_UNKNOWN;
    }
    return LeaveCriticalSection(&a_exec_cmd_request->wait_crit_sec), a_exec_cmd_request->response;     
#else
    pthread_mutex_lock(&a_exec_cmd_request->wait_mutex);
    if(a_exec_cmd_request->response)
        return pthread_mutex_unlock(&a_exec_cmd_request->wait_mutex), EXEC_CMD_OK;
    struct timespec l_cond_timeout;
#ifdef DAP_OS_DARWIN
    l_cond_timeout = (struct timespec){ .tv_sec = a_timeout_ms / 1000 };
#else
    clock_gettime(CLOCK_MONOTONIC, &l_cond_timeout);
    l_cond_timeout.tv_sec += a_timeout_ms / 1000;
#endif
    while (!a_exec_cmd_request->response) {
        switch (
#ifdef DAP_OS_DARWIN
            pthread_cond_timedwait_relative_np(&a_exec_cmd_request->wait_cond, &a_exec_cmd_request->wait_mutex, &l_cond_timeout)
#else
            pthread_cond_timedwait(&a_exec_cmd_request->wait_cond, &a_exec_cmd_request->wait_mutex, &l_cond_timeout)
#endif
        ) {
        case ETIMEDOUT:
            a_exec_cmd_request->response = "ERR_WAIT_TIMEOUT";
            return EXEC_CMD_ERR_WAIT_TIMEOUT;
        default:
            break;
        }
    }
    return pthread_mutex_unlock(&a_exec_cmd_request->wait_mutex), EXEC_CMD_OK;
#endif
}

char * dap_json_rpc_enc_request(dap_client_pvt_t* a_client_internal, char * a_request_data_str, size_t a_request_data_size,
                                char ** a_path, size_t * a_enc_request_size, char ** a_custom_header) {

    const char * l_sub_url = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                                    a_client_internal->client->active_channels,
                                                    a_client_internal->session_key_type,
                                                    a_client_internal->session_key_block_size, 0);

    bool is_query_enc = true;
    const char * a_query = "type=tcp,maxconn=4";
    size_t l_sub_url_size = l_sub_url ? strlen(l_sub_url) : 0;
    size_t l_query_size = a_query ? strlen(a_query) : 0;

    size_t l_sub_url_enc_size_max = l_sub_url_size ? (5 * l_sub_url_size + 16) : 0;
    char *l_sub_url_enc = l_sub_url_size ? DAP_NEW_Z_SIZE(char, l_sub_url_enc_size_max + 1) : NULL;

    size_t l_query_enc_size_max = (is_query_enc) ? (l_query_size * 5 + 16) : l_query_size;
    char *l_query_enc =
            (is_query_enc) ? (l_query_size ? DAP_NEW_Z_SIZE(char, l_query_enc_size_max + 1) : NULL) : (char*) a_query;

    size_t l_request_enc_size_max = a_request_data_size ? a_request_data_size * 2 + 16 : 0;
    char * l_request_enc = a_request_data_size ? DAP_NEW_Z_SIZE(char, l_request_enc_size_max + 1) : NULL;

    a_client_internal->is_encrypted = true;
    dap_enc_data_type_t l_enc_type;

    if(a_client_internal->uplink_protocol_version >= 21)
        l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
    else
        l_enc_type = DAP_ENC_DATA_TYPE_B64;

    if(l_sub_url_size)
        dap_enc_code(a_client_internal->session_key,
                l_sub_url, l_sub_url_size,
                l_sub_url_enc, l_sub_url_enc_size_max,
                l_enc_type);

    if(is_query_enc && l_query_size)
        dap_enc_code(a_client_internal->session_key,
                a_query, l_query_size,
                l_query_enc, l_query_enc_size_max,
                l_enc_type);

    if(a_request_data_size)
        *a_enc_request_size = dap_enc_code(a_client_internal->session_key,
                a_request_data_str, a_request_data_size,
                l_request_enc, l_request_enc_size_max,
                DAP_ENC_DATA_TYPE_RAW);

    size_t l_path_size= l_query_enc_size_max + l_sub_url_enc_size_max + 1;
    const char * path = "exec_cmd";
    *a_path = DAP_NEW_Z_SIZE(char, l_path_size);
    (*a_path)[0] = '\0';
    if(path) {
        if(l_sub_url_size){
            if(l_query_size){
                snprintf(*a_path, l_path_size, "%s/%s?%s", path?path:"",
                             l_sub_url_enc?l_sub_url_enc:"",
                                   l_query_enc?l_query_enc:"");
            }else{
                snprintf(*a_path, l_path_size, "%s/%s", path, l_sub_url_enc);
            }
        } else {
            dap_stpcpy(*a_path, path);
        }
    }

    size_t l_size_required = a_client_internal->session_key_id ? strlen(a_client_internal->session_key_id) + 40 : 40;
    *a_custom_header = DAP_NEW_Z_SIZE(char, l_size_required);
    size_t l_off = snprintf(*a_custom_header, l_size_required, "KeyID: %s\r\n", 
                            a_client_internal->session_key_id ? a_client_internal->session_key_id : "NULL");
    if (a_client_internal->is_close_session)
        snprintf(*a_custom_header + l_off, l_size_required - l_off, "%s\r\n", "SessionCloseAfterRequest: true");
    
    DAP_DEL_Z(l_sub_url_enc);
    DAP_DEL_Z(l_query_enc);

    return l_request_enc;
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

char* dap_json_rpc_request_to_http_str(dap_json_rpc_request_t *a_request, size_t*output_data_size){
    uint64_t l_id_response = dap_json_rpc_response_registration(a_request);
    a_request->id = 0;
    dap_cert_t *l_cert = dap_cert_find_by_name("node-addr");
    if (!l_cert) {
        log_it(L_ERROR, "Can't load cert");
        return NULL;
    }
    dap_json_rpc_http_request_t *l_http_request = dap_json_rpc_request_sign_by_cert(a_request, l_cert);
    size_t l_http_length = 0;
    char *l_http_str = dap_json_rpc_http_request_serialize(l_http_request, output_data_size);
    return l_http_str;
}

int dap_json_rpc_request_send(dap_client_pvt_t*  a_client_internal, dap_json_rpc_request_t *a_request, json_object** a_response) {
    size_t l_request_data_size = 0, l_enc_request_size, l_response_size;
    char* l_custom_header = NULL, *l_path = NULL;

    char* l_request_data_str =  dap_json_rpc_request_to_http_str(a_request, &l_request_data_size);
    if (!l_request_data_str) {
        return -1;
    }
    char * l_enc_request = dap_json_rpc_enc_request(a_client_internal, l_request_data_str, l_request_data_size, &l_path, &l_enc_request_size, &l_custom_header);
    DAP_DEL_Z(l_request_data_str);
    if (!l_enc_request || !l_path) {
        DAP_DEL_Z(l_request_data_str);
        DAP_DEL_Z(l_custom_header);
        return -2;
    }
    struct exec_cmd_request* l_exec_cmd_request = s_exec_cmd_request_init(a_client_internal);
    if (!l_exec_cmd_request) {
        DAP_DEL_Z(l_custom_header);
        DAP_DEL_Z(l_path);
        DAP_DEL_Z(l_enc_request);
        return -3;
    }

    log_it(L_DEBUG, "Send enc json-rpc request to %s:%d, path = %s, request size = %lu",
                     a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port, l_path, l_enc_request_size);

    a_client_internal->http_client = dap_client_http_request(
                                    a_client_internal->worker, 
                                    a_client_internal->client->link_info.uplink_addr,
                                    a_client_internal->client->link_info.uplink_port,
                                    "POST", "application/json",
                                    l_path, l_enc_request, l_enc_request_size, NULL,
                                    s_exec_cmd_response_handler, s_exec_cmd_error_handler, 
                                    l_exec_cmd_request, l_custom_header);

    int l_ret = dap_chain_exec_cmd_list_wait(l_exec_cmd_request, 100000);
    switch (l_ret) {
        case EXEC_CMD_OK :{
            if (s_exec_cmd_request_get_response(l_exec_cmd_request, a_response, &l_response_size)) {
                char * l_err = dap_strdup_printf("Response error code: %d", l_exec_cmd_request->error_code);
                *a_response = json_object_new_string(l_err);
                DAP_DEL_Z(l_err);
                break;
            }
            log_it(L_DEBUG, "Get response from %s:%d, response size = %lu",
                            a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port, l_response_size);
            break;
        }
        case EXEC_CMD_ERR_WAIT_TIMEOUT: {
            *a_response = json_object_new_string("Response time run out ");
            log_it(L_ERROR, "Response time from %s:%d  run out",
                            a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port);
            break;
        }
        case EXEC_CMD_ERR_UNKNOWN : {
            *a_response = json_object_new_string("Unknown error in json-rpc");
            log_it(L_ERROR, "Response from %s:%d has unknown error",
                            a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port);
            break;
        }
    }

    s_exec_cmd_request_free(l_exec_cmd_request);

    DAP_DEL_Z(l_custom_header);
    DAP_DEL_Z(l_path);
    DAP_DEL_Z(l_enc_request);
    return l_ret;
}
