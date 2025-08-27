#include "dap_json_rpc_request.h"
#include "dap_cert.h"
#include "dap_enc.h"

#define LOG_TAG "dap_json_rpc_request"

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
    DAP_DEL_MULTY(a_exec_cmd_request->response, a_exec_cmd_request);
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
    l_exec_cmd_request->response = DAP_DUP_SIZE(a_response, a_response_size);
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
    l_exec_cmd_request->response = NULL;
    l_exec_cmd_request->error_code = a_error_code;
    WakeConditionVariable(&l_exec_cmd_request->wait_cond);
    LeaveCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_lock(&l_exec_cmd_request->wait_mutex);
    l_exec_cmd_request->response = NULL;
    l_exec_cmd_request->error_code = a_error_code;
    pthread_cond_signal(&l_exec_cmd_request->wait_cond);
    pthread_mutex_unlock(&l_exec_cmd_request->wait_mutex);
#endif
}

static int s_exec_cmd_request_get_response(struct exec_cmd_request *a_exec_cmd_request, dap_json_t **a_response_out, size_t *a_response_out_size)
{
    int ret = 0;

    if (a_exec_cmd_request->error_code) {
        log_it(L_ERROR, "Response error code: %d", ret);
        ret = - 1;
    } else if (a_exec_cmd_request->response) {
            dap_client_pvt_t * l_client_pvt = a_exec_cmd_request->client_pvt;
            l_client_pvt->http_client = NULL;
            size_t l_response_dec_size_max = a_exec_cmd_request->response_size ? a_exec_cmd_request->response_size * 2 + 16 : 0;
            char * l_response_dec = a_exec_cmd_request->response_size ? DAP_NEW_Z_SIZE(char, l_response_dec_size_max) : NULL;
            size_t l_response_dec_size = 0;
            if(a_exec_cmd_request->response_size)
                l_response_dec_size = dap_enc_decode(l_client_pvt->session_key,
                        a_exec_cmd_request->response, a_exec_cmd_request->response_size,
                        l_response_dec, l_response_dec_size_max,
                        DAP_ENC_DATA_TYPE_RAW);
            *a_response_out = dap_json_parse_string(l_response_dec);
            if (!*a_response_out && l_response_dec) {
                *a_response_out = dap_json_object_new_string("Can't decode the response, check the access rights on the remote node");
                log_it(L_DEBUG, "Wrong response %s", l_response_dec);
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
        return LeaveCriticalSection(&a_exec_cmd_request->wait_crit_sec), EXEC_CMD_OK;
    while (!a_exec_cmd_request->response) {
        if ( !SleepConditionVariableCS(&a_exec_cmd_request->wait_cond, &a_exec_cmd_request->wait_crit_sec, a_timeout_ms) )
            a_exec_cmd_request->error_code = GetLastError() == ERROR_TIMEOUT ? EXEC_CMD_ERR_WAIT_TIMEOUT : EXEC_CMD_ERR_UNKNOWN;
    }
    return LeaveCriticalSection(&a_exec_cmd_request->wait_crit_sec), a_exec_cmd_request->error_code;     
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
        case 0:
            break;
        case ETIMEDOUT:
            a_exec_cmd_request->error_code = EXEC_CMD_ERR_WAIT_TIMEOUT;
            break;
        default:
            a_exec_cmd_request->error_code = EXEC_CMD_ERR_UNKNOWN;
            break;
        }
    }
    return pthread_mutex_unlock(&a_exec_cmd_request->wait_mutex), a_exec_cmd_request->error_code;
#endif
}

char * dap_json_rpc_enc_request(dap_client_pvt_t* a_client_internal, char * a_request_data_str, size_t a_request_data_size,
                                char ** a_path, size_t * a_enc_request_size, char ** a_custom_header) {

    char s_query[] = "type=tcp,maxconn=4", s_suburl[128];
    int l_query_len = sizeof(s_query) - 1,
        l_suburl_len = snprintf(s_suburl, sizeof(s_suburl), "channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                                            a_client_internal->client->active_channels, a_client_internal->session_key_type,
                                                            a_client_internal->session_key_block_size, 0);
    if (l_suburl_len >= (int)sizeof(s_suburl))
        return NULL;
    dap_enc_data_type_t l_enc_type = a_client_internal->uplink_protocol_version >= 21
        ? DAP_ENC_DATA_TYPE_B64_URLSAFE : DAP_ENC_DATA_TYPE_B64;
    int l_suburl_enc_len = dap_enc_code_out_size(a_client_internal->session_key, l_suburl_len, l_enc_type),
        l_query_enc_len = dap_enc_code_out_size(a_client_internal->session_key, l_query_len, l_enc_type),
        l_req_enc_len = dap_enc_code_out_size(a_client_internal->session_key, a_request_data_size, DAP_ENC_DATA_TYPE_RAW);

    char l_suburl_enc[ l_suburl_enc_len + 1 ], l_query_enc[ l_query_enc_len + 1 ], *l_req_enc = DAP_NEW_Z_SIZE(char, l_req_enc_len + 1);

    a_client_internal->is_encrypted = true;
    l_suburl_enc_len = dap_enc_code(a_client_internal->session_key, s_suburl,             l_suburl_len,       l_suburl_enc, l_suburl_enc_len + 1, l_enc_type);
    l_query_enc_len  = dap_enc_code(a_client_internal->session_key, s_query,              l_query_len,        l_query_enc,  l_query_enc_len + 1,  l_enc_type);
    *a_enc_request_size = dap_enc_code(a_client_internal->session_key, a_request_data_str,a_request_data_size,l_req_enc,    l_req_enc_len + 1,    DAP_ENC_DATA_TYPE_RAW);

    l_suburl_enc[l_suburl_enc_len] = l_query_enc[l_query_enc_len] = '\0';

    *a_path = dap_strdup_printf("exec_cmd/%s?%s", l_suburl_enc, l_query_enc);
    *a_custom_header = dap_strdup_printf("KeyID: %s\r\n%s",
                                         a_client_internal->session_key_id ? a_client_internal->session_key_id : "NULL",
                                         a_client_internal->is_close_session ? "SessionCloseAfterRequest: true\r\n" : "");
    return l_req_enc;
}


dap_json_rpc_request_t *dap_json_rpc_request_creation(const char *a_method, dap_json_rpc_params_t *a_params, int64_t a_id, int a_version)
{
    dap_json_rpc_request_t *request = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_json_rpc_request_t, NULL);
    request->method = dap_strdup(a_method);
    request->params = a_params;
    request->id = a_id;
    request->version = a_version;
    return request;
}

void dap_json_rpc_request_free(dap_json_rpc_request_t *request)
{
    if (!request)
        return;
    DAP_DELETE(request->method);
    if (request->params)
        dap_json_rpc_params_remove_all(request->params);
    DAP_DELETE(request);
}

dap_json_rpc_request_t *dap_json_rpc_request_from_json(const char *a_data, int a_version_default)
{
    if (!a_data)
        return NULL;
    dap_json_rpc_request_t *request = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_json_rpc_request_t, NULL);
    dap_json_tokener_error_t jterr;
    dap_json_t *jobj = dap_json_tokener_parse_verbose(a_data, &jterr);
    if (jterr == DAP_JSON_TOKENER_ERROR_SUCCESS && jobj)
        do {
            request->id = dap_json_object_get_int64(jobj, "id");
            else {
                log_it(L_ERROR, "Error parse JSON string, can't find request id");
                break;
            }
            if (json_object_object_get_ex(jobj, "version", &jobj_version))
                request->version = json_object_get_int64(jobj_version);
            else {
                log_it(L_DEBUG, "Can't find request version, apply version %d", a_version_default);
                request->version = a_version_default;
            }

            if (json_object_object_get_ex(jobj, "method", &jobj_method))
                request->method = dap_strdup(json_object_get_string(jobj_method));
            else {
                log_it(L_ERROR, "Error parse JSON string, can't find method for request with id: %" DAP_UINT64_FORMAT_U, request->id);
                break;
            }

            if(!json_object_object_get_ex(jobj, "params", &jobj_params)){
                json_object_object_get_ex(jobj, "subcommand", &jobj_subcmd);
                json_object_object_get_ex(jobj, "arguments", &l_arguments_obj);
            }

            if (jobj_params)
                request->params = dap_json_rpc_params_create_from_array_list(jobj_params);
            else 
                request->params = dap_json_rpc_params_create_from_subcmd_and_args(jobj_subcmd, l_arguments_obj, request->method);

            json_object_put(jobj);
            if (!request->params){
                dap_json_rpc_params_remove_all(request->params);
                DAP_DEL_MULTY(request->method, request);
                return NULL;
            }
            return request;
        } while (0);
    else
        log_it(L_ERROR, "Error parse json tokener: %s", json_tokener_error_desc(jterr));
    json_object_put(jobj);
    dap_json_rpc_params_remove_all(request->params);
    DAP_DEL_MULTY(request->method, request);
    return NULL;
}

char *dap_json_rpc_request_to_json_string(const dap_json_rpc_request_t *a_request)
{
    char *params_json = dap_json_rpc_params_get_string_json(a_request->params);
    if (!params_json)
        return log_it(L_ERROR, "Failed to generate JSON for params"), NULL;

    char *l_str = dap_strdup_printf(
        "{\"method\":\"%s\", \"params\":%s, \"id\":\"%" DAP_UINT64_FORMAT_U "\", \"version\":\"%d\" }",
        a_request->method, params_json, a_request->id, a_request->version);
    DAP_DELETE(params_json);
    return l_str;
}

char *dap_json_rpc_http_request_serialize(dap_json_rpc_http_request_t *a_request, size_t *a_total_size)
{
    *a_total_size = a_request->header.data_size + a_request->header.signs_size + sizeof(a_request->header);
    return (char*)DAP_DUP_SIZE(a_request, *a_total_size);
}

dap_json_rpc_http_request_t *dap_json_rpc_http_request_deserialize(const void *data, size_t data_size)
{
    __typeof__( (dap_json_rpc_http_request_t){0}.header ) l_hdr;
    if (data_size < sizeof(l_hdr))
        return log_it(L_ERROR, "Data size is less than minimum: %zu < %zu",
                               data_size, sizeof(dap_json_rpc_http_request_t)),
               NULL;
    memcpy(&l_hdr, data, sizeof(l_hdr));
    if ( data_size < sizeof(l_hdr) + l_hdr.data_size + l_hdr.signs_size )
        return log_it(L_ERROR, "Data size is less than needed: %zu < %zu",
                               data_size, sizeof(dap_json_rpc_http_request_t) + l_hdr.data_size + l_hdr.signs_size),
               NULL;

    dap_json_rpc_http_request_t *l_ret = (dap_json_rpc_http_request_t*)DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, data_size + 1, NULL);
    dap_mempcpy(l_ret, data, data_size);
    return l_ret;
}

void dap_json_rpc_http_request_free(dap_json_rpc_http_request_t *a_http_request)
{
    DAP_DELETE(a_http_request);
}

dap_json_rpc_http_request_t *dap_json_rpc_request_sign_by_cert(dap_json_rpc_request_t *a_request, dap_cert_t *a_cert)
{
    char *l_str = dap_json_rpc_request_to_json_string(a_request);
    if (!l_str)
        return log_it(L_ERROR, "Can't convert JSON-request to string!"), NULL;
    int l_len = strlen(l_str);
    dap_sign_t *l_sign = dap_cert_sign(a_cert, l_str, l_len);
    if (!l_sign)
        return DAP_DELETE(l_str), log_it(L_ERROR, "JSON request signing failed"), NULL;
    size_t l_sign_size = dap_sign_get_size(l_sign);

    dap_json_rpc_http_request_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_json_rpc_http_request_t,
                                                    sizeof(dap_json_rpc_http_request_t) + l_len + 1 + l_sign_size, NULL, l_sign, l_str);
    *l_ret = (dap_json_rpc_http_request_t) {
        .header.data_size = l_len + 1,
        .header.signs_size = l_sign_size,
    };
    byte_t* l_cur = (byte_t*)dap_strncpy((char*)l_ret->request_n_signs, l_str, l_len);
    memcpy(l_cur + 1, l_sign, l_sign_size);
    return DAP_DEL_MULTY(l_sign, l_str), l_ret;
}

char* dap_json_rpc_request_to_http_str(dap_json_rpc_request_t *a_request, size_t*output_data_size, const char *a_cert_path){
    a_request->id = 0;
    dap_cert_t *l_cert = NULL;
    if (!a_cert_path) {
        l_cert = dap_cert_find_by_name("node-addr");
    } else {
        l_cert = dap_cert_find_by_name(a_cert_path);
    }
    if (!l_cert)
        return log_it(L_ERROR, "Can't load cert"), NULL;

    dap_json_rpc_http_request_t *l_http_request = dap_json_rpc_request_sign_by_cert(a_request, l_cert);
    size_t l_http_length = 0;
    char *l_http_str = dap_json_rpc_http_request_serialize(l_http_request, output_data_size);
    return DAP_DELETE(l_http_request), l_http_str;
}

int dap_json_rpc_request_send(dap_client_pvt_t*  a_client_internal, dap_json_rpc_request_t *a_request, json_object** a_response, const char *a_cert_path) {
    size_t l_request_data_size, l_enc_request_size, l_response_size;
    char* l_custom_header = NULL, *l_path = NULL;

    char* l_request_data_str = dap_json_rpc_request_to_http_str(a_request, &l_request_data_size, a_cert_path);
    if (!l_request_data_str)
        return -1;

    char * l_enc_request = dap_json_rpc_enc_request(a_client_internal, l_request_data_str, l_request_data_size, &l_path, &l_enc_request_size, &l_custom_header);
    DAP_DELETE(l_request_data_str);
    if (!l_enc_request || !l_path)
        return DAP_DEL_MULTY(l_custom_header, l_enc_request, l_path), -1;

    struct exec_cmd_request* l_exec_cmd_request = s_exec_cmd_request_init(a_client_internal);
    if (!l_exec_cmd_request)
        return DAP_DEL_MULTY(l_custom_header, l_enc_request, l_path), -1;

    log_it(L_DEBUG, "Send enc json-rpc request to %s:%d, path = %s, request size = %lu",
                     a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port, l_path, l_enc_request_size);

    a_client_internal->http_client = dap_client_http_request(a_client_internal->worker,
                                                             a_client_internal->client->link_info.uplink_addr,
                                                             a_client_internal->client->link_info.uplink_port,
                                                             "POST", "application/json",
                                                             l_path, l_enc_request, l_enc_request_size, NULL,
                                                             s_exec_cmd_response_handler, s_exec_cmd_error_handler,
                                                             l_exec_cmd_request, l_custom_header);

    int l_ret = dap_chain_exec_cmd_list_wait(l_exec_cmd_request, 15000);
    switch (l_ret) {
        case EXEC_CMD_OK :{
            if (s_exec_cmd_request_get_response(l_exec_cmd_request, a_response, &l_response_size)) {
                char l_err[40] = "";
                snprintf(l_err, sizeof(l_err), "Response error code: %d", l_exec_cmd_request->error_code);
                *a_response = json_object_new_string(l_err);
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
    DAP_DEL_MULTY(l_custom_header, l_path, l_enc_request);
    return l_ret;
}
