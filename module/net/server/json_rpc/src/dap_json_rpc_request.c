#include "dap_json_rpc_request.h"
#include "dap_json_rpc_params.h"
#include "dap_cert.h"
#include "dap_enc.h"
#include "dap_enc_ks.h"
#include "dap_client_http.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_json_rpc_request"

// Simple structure for tracking HTTP request/response - no dap_client_pvt dependency
struct exec_cmd_request {
    dap_client_http_t *http_client;
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

static struct exec_cmd_request* s_exec_cmd_request_init(void)
{
    struct exec_cmd_request *l_exec_cmd_request = DAP_NEW_Z(struct exec_cmd_request);
    if (!l_exec_cmd_request)
        return NULL;
    l_exec_cmd_request->http_client = NULL;
#ifdef DAP_OS_WINDOWS
    InitializeCriticalSection(&l_exec_cmd_request->wait_crit_sec);
    InitializeConditionVariable(&l_exec_cmd_request->wait_cond);
#else
    pthread_mutex_init(&l_exec_cmd_request->wait_mutex, NULL);
#if defined(DAP_OS_DARWIN) || defined(DAP_OS_ANDROID)
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

static void s_exec_cmd_request_free(struct exec_cmd_request* a_exec_cmd_request) {
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

static void s_exec_cmd_response_handler(void *a_response, size_t a_response_size, void *a_arg, dap_http_status_code_t a_http_status) {
    (void)a_http_status;
    struct exec_cmd_request *l_exec_cmd_request = (struct exec_cmd_request *)a_arg;
#ifdef DAP_OS_WINDOWS
    EnterCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_mutex_lock(&l_exec_cmd_request->wait_mutex);
#endif
    l_exec_cmd_request->error_code = 0;
    if (a_response && a_response_size) {
        l_exec_cmd_request->response = DAP_DUP_SIZE(a_response, a_response_size);
        l_exec_cmd_request->response_size = a_response_size;
    }
#ifdef DAP_OS_WINDOWS
    WakeConditionVariable(&l_exec_cmd_request->wait_cond);
    LeaveCriticalSection(&l_exec_cmd_request->wait_crit_sec);
#else
    pthread_cond_signal(&l_exec_cmd_request->wait_cond);
    pthread_mutex_unlock(&l_exec_cmd_request->wait_mutex);
#endif
}

static void s_exec_cmd_error_handler(int a_error_code, void *a_arg) {
    struct exec_cmd_request *l_exec_cmd_request = (struct exec_cmd_request *)a_arg;
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
    log_it(L_ERROR, "JSON-RPC request error: %d", a_error_code);
}

static int s_exec_cmd_request_get_response(struct exec_cmd_request *a_exec_cmd_request, dap_json_t **a_response_out, size_t *a_response_out_size)
{
    int ret = 0;

    if (a_exec_cmd_request->error_code) {
        log_it(L_ERROR, "Response error code: %d", a_exec_cmd_request->error_code);
        ret = -1;
    } else if (a_exec_cmd_request->response && a_exec_cmd_request->response_size) {
        // Parse response directly - no encryption in basic DAP SDK
        *a_response_out = dap_json_parse_string((const char *)a_exec_cmd_request->response);
        if (!*a_response_out) {
            *a_response_out = dap_json_object_new_string("Failed to parse JSON response");
            log_it(L_ERROR, "Failed to parse JSON response");
            ret = -1;
        }
        *a_response_out_size = a_exec_cmd_request->response_size;
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

/**
 * @brief Encrypt JSON-RPC request using enc_server key
 * @param a_key_id Encryption key ID from enc_server
 * @param a_request_data_str Request data to encrypt
 * @param a_request_data_size Request data size
 * @param a_channels Channels string (e.g., "A,B,C")
 * @param a_path Output: path for HTTP request
 * @param a_enc_request_size Output: encrypted request size
 * @param a_custom_header Output: custom headers
 * @return Encrypted request data (caller must free)
 */
char * dap_json_rpc_enc_request(const char *a_key_id, char *a_request_data_str, size_t a_request_data_size,
                                const char *a_channels, char **a_path, size_t *a_enc_request_size, 
                                char **a_custom_header) {
    if (!a_key_id || !a_request_data_str || !a_path || !a_enc_request_size || !a_custom_header) {
        log_it(L_ERROR, "Invalid arguments for JSON-RPC encryption");
        return NULL;
    }

    // Get encryption key from enc_server key storage
    dap_enc_ks_key_t *l_ks_key = dap_enc_ks_find(a_key_id);
    if (!l_ks_key || !l_ks_key->key) {
        log_it(L_ERROR, "Failed to get encryption key by ID: %s", a_key_id);
        return NULL;
    }
    dap_enc_key_t *l_enc_key = l_ks_key->key;

    dap_enc_key_type_t l_key_type = l_enc_key->type;
    size_t l_key_size = l_enc_key->priv_key_data_size;

    // Prepare suburl and query
    char s_query[] = "type=tcp,maxconn=4", s_suburl[128];
    int l_query_len = sizeof(s_query) - 1;
    int l_suburl_len = snprintf(s_suburl, sizeof(s_suburl), "channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                a_channels ? a_channels : "A", l_key_type, l_key_size, 0);
    if (l_suburl_len >= (int)sizeof(s_suburl)) {
        log_it(L_ERROR, "Suburl buffer overflow");
        return NULL;
    }

    // Use URL-safe base64 encoding
    dap_enc_data_type_t l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
    
    // Calculate sizes for encrypted data
    int l_suburl_enc_len = dap_enc_code_out_size(l_enc_key, l_suburl_len, l_enc_type);
    int l_query_enc_len = dap_enc_code_out_size(l_enc_key, l_query_len, l_enc_type);
    int l_req_enc_len = dap_enc_code_out_size(l_enc_key, a_request_data_size, DAP_ENC_DATA_TYPE_RAW);

    // Allocate buffers
    char *l_suburl_enc = DAP_NEW_Z_SIZE(char, l_suburl_enc_len + 1);
    char *l_query_enc = DAP_NEW_Z_SIZE(char, l_query_enc_len + 1);
    char *l_req_enc = DAP_NEW_Z_SIZE(char, l_req_enc_len + 1);
    
    if (!l_suburl_enc || !l_query_enc || !l_req_enc) {
        log_it(L_ERROR, "Memory allocation failed for encryption buffers");
        DAP_DEL_MULTY(l_suburl_enc, l_query_enc, l_req_enc);
        return NULL;
    }

    // Encode data
    l_suburl_enc_len = dap_enc_code(l_enc_key, s_suburl, l_suburl_len, l_suburl_enc, l_suburl_enc_len + 1, l_enc_type);
    l_query_enc_len = dap_enc_code(l_enc_key, s_query, l_query_len, l_query_enc, l_query_enc_len + 1, l_enc_type);
    *a_enc_request_size = dap_enc_code(l_enc_key, a_request_data_str, a_request_data_size, l_req_enc, l_req_enc_len + 1, DAP_ENC_DATA_TYPE_RAW);

    l_suburl_enc[l_suburl_enc_len] = l_query_enc[l_query_enc_len] = '\0';

    // Build path and headers
    *a_path = dap_strdup_printf("exec_cmd/%s?%s", l_suburl_enc, l_query_enc);
    *a_custom_header = dap_strdup_printf("KeyID: %s\r\n", a_key_id);

    DAP_DEL_MULTY(l_suburl_enc, l_query_enc);
    
    log_it(L_DEBUG, "Encrypted JSON-RPC request with key ID: %s", a_key_id);
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
    if (jterr == DAP_JSON_TOKENER_SUCCESS && jobj)
        do {
            // Parse id field
            request->id = dap_json_object_get_int64(jobj, "id");
            if (request->id == 0) {
                log_it(L_ERROR, "Error parse JSON string, can't find request id");
                break;
            }
            
            // Parse version field
            request->version = dap_json_object_get_int64(jobj, "version");
            if (request->version == 0) {
                log_it(L_DEBUG, "Can't find request version, apply version %d", a_version_default);
                request->version = a_version_default;
            }

            // Parse method field
            const char *l_method = dap_json_object_get_string(jobj, "method");
            if (l_method) {
                request->method = dap_strdup(l_method);
            } else {
                log_it(L_ERROR, "Error parse JSON string, can't find method for request with id: %" DAP_UINT64_FORMAT_U, request->id);
                break;
            }

            // Parse params/subcommand/arguments
            dap_json_t *jobj_params = dap_json_object_get_object(jobj, "params");
            if (!jobj_params) {
                dap_json_t *jobj_subcmd = dap_json_object_get_object(jobj, "subcommand");
                dap_json_t *l_arguments_obj = dap_json_object_get_object(jobj, "arguments");
                if (jobj_subcmd && l_arguments_obj) {
                    request->params = dap_json_rpc_params_create_from_subcmd_and_args(jobj_subcmd, l_arguments_obj, request->method);
                }
            } else {
                request->params = dap_json_rpc_params_create_from_array_list(jobj_params);
            }

            dap_json_object_free(jobj);
            if (!request->params){
                DAP_DEL_MULTY(request->method, request);
                return NULL;
            }
            return request;
        } while (0);
    else
        log_it(L_ERROR, "Error parse json tokener: %s", dap_json_tokener_error_desc(jterr));
    if (jobj)
        dap_json_object_free(jobj);
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

/**
 * @brief Send JSON-RPC request with encryption
 * @param a_uplink_addr Server address
 * @param a_uplink_port Server port
 * @param a_key_id Encryption key ID (NULL for no encryption)
 * @param a_channels Channels string
 * @param a_request JSON-RPC request
 * @param a_response Output: JSON response
 * @param a_cert_path Certificate path for signing
 * @return 0 on success, negative on error
 */
int dap_json_rpc_request_send(const char *a_uplink_addr, uint16_t a_uplink_port,
                              const char *a_key_id, const char *a_channels,
                              dap_json_rpc_request_t *a_request, dap_json_t **a_response, 
                              const char *a_cert_path) {
    if (!a_uplink_addr || !a_request || !a_response) {
        log_it(L_ERROR, "Invalid arguments for JSON-RPC request send");
        return -EINVAL;
    }

    size_t l_request_data_size, l_enc_request_size, l_response_size;
    char *l_custom_header = NULL, *l_path = NULL;

    // Convert request to HTTP string with signature
    char *l_request_data_str = dap_json_rpc_request_to_http_str(a_request, &l_request_data_size, a_cert_path);
    if (!l_request_data_str) {
        log_it(L_ERROR, "Failed to convert JSON-RPC request to HTTP string");
        return -1;
    }

    // Encrypt request if key ID provided
    char *l_enc_request = NULL;
    if (a_key_id) {
        l_enc_request = dap_json_rpc_enc_request(a_key_id, l_request_data_str, l_request_data_size, 
                                                 a_channels, &l_path, &l_enc_request_size, &l_custom_header);
        DAP_DELETE(l_request_data_str);
        if (!l_enc_request || !l_path) {
            log_it(L_ERROR, "Failed to encrypt JSON-RPC request");
            return DAP_DEL_MULTY(l_custom_header, l_enc_request, l_path), -1;
        }
    } else {
        // No encryption - use plain request
        l_enc_request = l_request_data_str;
        l_enc_request_size = l_request_data_size;
        l_path = dap_strdup("exec_cmd");
        l_custom_header = dap_strdup("");
    }

    // Initialize request tracking structure
    struct exec_cmd_request *l_exec_cmd_request = s_exec_cmd_request_init();
    if (!l_exec_cmd_request) {
        log_it(L_ERROR, "Failed to initialize exec_cmd_request");
        return DAP_DEL_MULTY(l_custom_header, l_enc_request, l_path), -ENOMEM;
    }

    log_it(L_DEBUG, "Send JSON-RPC request to %s:%d, path = %s, request size = %zu",
           a_uplink_addr, a_uplink_port, l_path, l_enc_request_size);

    // Send HTTP request (worker selected automatically via NULL)
    l_exec_cmd_request->http_client = dap_client_http_request(NULL, a_uplink_addr, a_uplink_port,
                                                             "POST", "application/json",
                                                             l_path, l_enc_request, l_enc_request_size, NULL,
                                                             s_exec_cmd_response_handler, s_exec_cmd_error_handler,
                                                             l_exec_cmd_request, l_custom_header);

    // Wait for response (15 second timeout)
    int l_ret = dap_chain_exec_cmd_list_wait(l_exec_cmd_request, 15000);
    
    // Process result
    switch (l_ret) {
        case EXEC_CMD_OK: {
            if (s_exec_cmd_request_get_response(l_exec_cmd_request, a_response, &l_response_size)) {
                char l_err[64] = "";
                snprintf(l_err, sizeof(l_err), "Response error code: %d", l_exec_cmd_request->error_code);
                *a_response = dap_json_object_new_string(l_err);
                l_ret = -1;
            } else {
                log_it(L_DEBUG, "Got response from %s:%d, response size = %zu",
                      a_uplink_addr, a_uplink_port, l_response_size);
            }
            break;
        }
        case EXEC_CMD_ERR_WAIT_TIMEOUT: {
            *a_response = dap_json_object_new_string("Response timeout");
            log_it(L_ERROR, "Response timeout from %s:%d", a_uplink_addr, a_uplink_port);
            l_ret = -ETIMEDOUT;
            break;
        }
        case EXEC_CMD_ERR_UNKNOWN:
        default: {
            *a_response = dap_json_object_new_string("Unknown error in JSON-RPC");
            log_it(L_ERROR, "Unknown error from %s:%d", a_uplink_addr, a_uplink_port);
            l_ret = -1;
            break;
        }
    }

    // Cleanup
    s_exec_cmd_request_free(l_exec_cmd_request);
    DAP_DEL_MULTY(l_custom_header, l_path, l_enc_request);
    
    return l_ret;
}
