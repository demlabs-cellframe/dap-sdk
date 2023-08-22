#include "dap_json_rpc_response.h"

#define LOG_TAG "dap_json_rpc_response"

                                                   
dap_json_rpc_response_t *dap_json_rpc_response_init()
{
    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response)
        log_it(L_CRITICAL, "Memory allocation error");
    dap_json_rpc_error_init();
    return response;
}

dap_json_rpc_response_t* dap_json_rpc_response_create(void * result, dap_json_rpc_response_type_result_t type, int64_t id) {

    dap_json_rpc_error_init();

    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    response->id = id;
    response->type = type;

    switch(response->type){
        case TYPE_RESPONSE_STRING:
            response->result_string = (char*)result; break;
        case TYPE_RESPONSE_INTEGER:
            response->result_int = *((int64_t*)result); break;
        case TYPE_RESPONSE_DOUBLE:
            response->result_double = *((double*)result); break;
        case TYPE_RESPONSE_BOOLEAN:
            response->result_boolean = *((bool*)result); break;
        case TYPE_RESPONSE_JSON:
            response->result_json_object = json_object_get((json_object*)result); break;
        case TYPE_RESPONSE_NULL:
            break;
        default:
            log_it(L_ERROR, "Wrong resonse type");
            DAP_FREE(response);
            return NULL;
    }
    return response;
}

void dap_json_rpc_response_free(dap_json_rpc_response_t *response)
{
    if (response) {
        switch(response->type) {
            case TYPE_RESPONSE_STRING:
                DAP_DEL_Z(response->result_string); break;
            case TYPE_RESPONSE_JSON:
                json_object_put(response->result_json_object); break;
            case TYPE_RESPONSE_INTEGER:
            case TYPE_RESPONSE_DOUBLE:
            case TYPE_RESPONSE_BOOLEAN:
            case TYPE_RESPONSE_NULL:
                // No specific cleanup needed for these response types
                break;
            default:
                log_it(L_ERROR, "Unsupported response type");
                break;
        }
        dap_json_rpc_error_deinit();
        DAP_FREE(response);
    }
}

char* dap_json_rpc_response_to_string(const dap_json_rpc_response_t* response) {
    if (!response) {
        return NULL;
    }

    json_object* jobj = json_object_new_object();

    json_object_object_add(jobj, "type", json_object_new_int(response->type));
    switch (response->type) {
        case TYPE_RESPONSE_STRING:
            json_object_object_add(jobj, "result", json_object_new_string(response->result_string));
            break;
        case TYPE_RESPONSE_INTEGER:
            json_object_object_add(jobj, "result", json_object_new_int64(response->result_int));
            break;
        case TYPE_RESPONSE_DOUBLE:
            json_object_object_add(jobj, "result", json_object_new_double(response->result_double));
            break;
        case TYPE_RESPONSE_BOOLEAN:
            json_object_object_add(jobj, "result", json_object_new_boolean(response->result_boolean));
            break;
        case TYPE_RESPONSE_JSON:
            json_object_object_add(jobj, "result", response->result_json_object);
            break;
        case TYPE_RESPONSE_NULL:
            json_object_object_add(jobj, "result", NULL);
            break;
    }

    json_object_object_add(jobj, "id", json_object_new_int64(response->id));

    const char* json_string = json_object_to_json_string(jobj);
    char* result_string = strdup(json_string);
    json_object_put(jobj);

    return result_string;
}

dap_json_rpc_response_t* dap_json_rpc_response_from_string(const char* json_string) {
    json_object* jobj = json_tokener_parse(json_string);
    if (!jobj) {
        log_it(L_ERROR, "Error parsing JSON string");
        return NULL;
    }

    dap_json_rpc_response_t* response = malloc(sizeof(dap_json_rpc_response_t));
    if (!response) {
        json_object_put(jobj);
        log_it(L_CRITICAL, "Memmory allocation error");
        return NULL;
    }

    json_object* type_obj = NULL;
    if (json_object_object_get_ex(jobj, "type", &type_obj)) {
        response->type = json_object_get_int(type_obj);

        json_object* result_obj = NULL;
        if (json_object_object_get_ex(jobj, "result", &result_obj)) {
            switch (response->type) {
                case TYPE_RESPONSE_STRING:
                    response->result_string = strdup(json_object_get_string(result_obj));
                    break;
                case TYPE_RESPONSE_INTEGER:
                    response->result_int = json_object_get_int64(result_obj);
                    break;
                case TYPE_RESPONSE_DOUBLE:
                    response->result_double = json_object_get_double(result_obj);
                    break;
                case TYPE_RESPONSE_BOOLEAN:
                    response->result_boolean = json_object_get_boolean(result_obj);
                    break;
                case TYPE_RESPONSE_JSON:
                    response->result_json_object = json_object_get(result_obj);
                    break;
                case TYPE_RESPONSE_NULL:
                    break;
            }
        }
    }
    json_object* result_id = NULL;
    json_object_object_get_ex(jobj, "id", &result_id);
    response->id = json_object_get_int64(result_id);

    json_object_put(jobj);
    return response;
}


/**
 * Print the result of a JSON-RPC response to the console.
 *
 * @param response A pointer to the dap_json_rpc_response_t instance.
 * @return 0 on success, 
 *         -1 if the response is empty, 
 *         -2 if the JSON object is NULL,
 *         and -3 if the JSON object length is 0.
 */
int dap_json_rpc_response_printf_result(dap_json_rpc_response_t* response) {
    if (!response) {
        printf("Empty response");
        return -1;
    }

    switch (response->type) {
        case TYPE_RESPONSE_STRING:
            printf("%s\n", response->result_string);
            break;
        case TYPE_RESPONSE_INTEGER:
            printf("%lld\n", (long long int)response->result_int);
            break;
        case TYPE_RESPONSE_DOUBLE:
            printf("%lf\n", response->result_double);
            break;
        case TYPE_RESPONSE_BOOLEAN:
            printf("%s\n", response->result_boolean ? "true" : "false");
            break;
        case TYPE_RESPONSE_NULL:
            printf("Response type is NULL\n");
            break;
        case TYPE_RESPONSE_JSON:
            if (response->result_json_object) {
                printf("Json Object is NULL\n");
                return -2;
            }

            if (json_object_object_length(response->result_json_object) <= 0) {
                printf("Json Object length is 0\n");
                return -3;
            }

            json_object_object_foreach(response->result_json_object, key, val) {
                printf("%s:", key);

                if (json_object_is_type(val, json_type_string)) {
                    printf("%s,\n", json_object_get_string(val));
                } else if (json_object_is_type(val, json_type_int)) {
                    printf("%lld,\n", (long long int)json_object_get_int64(val));
                } else if (json_object_is_type(val, json_type_double)) {
                    printf("%lf,\n", json_object_get_double(val));
                } else if (json_object_is_type(val, json_type_boolean)) {
                    printf("%s,\n", json_object_get_boolean(val) ? "true" : "false");
                }
            }
            break;
    }
    return 0;
}

void dap_json_rpc_response_JSON_free(dap_json_rpc_request_JSON_t *l_request_JSON)
{
    if (l_request_JSON->struct_error)
        dap_json_rpc_error_JSON_free(l_request_JSON->struct_error);
    json_object_put(l_request_JSON->obj_result);
    json_object_put(l_request_JSON->obj_error);
    json_object_put(l_request_JSON->obj_id);
    DAP_FREE(l_request_JSON);
}

// void dap_json_rpc_response_free(dap_json_rpc_response_t *a_response)
// {
//     DAP_FREE(a_response->error);
//     if (a_response->type == TYPE_RESPONSE_STRING){
//         DAP_FREE(a_response->result);
//     }
//     DAP_FREE(a_response);
// }

void dap_json_rpc_response_send(dap_json_rpc_response_t *a_response, dap_http_simple_t *a_client)
{
    json_object *l_jobj = json_object_new_object();
    json_object *l_obj_id = json_object_new_int64(a_response->id);
    json_object *l_obj_result = NULL;
    json_object *l_obj_error = NULL;
    const char *str_response = NULL;
    if (a_response->error == NULL){
        l_obj_error = json_object_new_null();
        switch (a_response->type) {
            case TYPE_RESPONSE_STRING:
                l_obj_result = json_object_new_string(a_response->result_string);
                break;
            case TYPE_RESPONSE_DOUBLE:
                l_obj_result = json_object_new_double(a_response->result_double);
                break;
            case TYPE_RESPONSE_BOOLEAN:
                l_obj_result = json_object_new_boolean(a_response->result_boolean);
                break;
            case TYPE_RESPONSE_INTEGER:
                l_obj_result = json_object_new_int64(a_response->result_int);
                break;
            case TYPE_RESPONSE_JSON:
                l_obj_result = json_object_get(a_response->result_json_object);
                json_object_put(a_response->result_json_object);
                break;
            default:{}
        }
    }else{
        l_obj_error = json_object_new_object();
        json_object *l_obj_err_code = json_object_new_int(a_response->error->code_error);
        json_object *l_obj_err_msg = json_object_new_string(a_response->error->msg);
        json_object_object_add(l_obj_error, "code", l_obj_err_code);
        json_object_object_add(l_obj_error, "message", l_obj_err_msg);
    }
    json_object_object_add(l_jobj, "result", l_obj_result);
    json_object_object_add(l_jobj, "id", l_obj_id);
    json_object_object_add(l_jobj, "error", l_obj_error);
    str_response = json_object_to_json_string(l_jobj);
    dap_http_simple_reply(a_client, (void *)str_response, strlen(str_response));
    json_object_put(l_jobj);
}


dap_json_rpc_response_t *dap_json_rpc_response_from_json(char *a_data_json)
{
    json_object *l_jobj = json_tokener_parse(a_data_json);
    json_object *l_jobj_result = json_object_object_get(l_jobj, "result");
    json_object *l_jobj_error = json_object_object_get(l_jobj, "error");
    json_object *l_jobj_id = json_object_object_get(l_jobj, "id");
    dap_json_rpc_response_t *l_response = DAP_NEW(dap_json_rpc_response_t);
    if (!l_response) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_response->id = json_object_get_int64(l_jobj_id);
    if (json_object_is_type(l_jobj_error, json_type_null)){
        l_response->error = NULL;
        switch(json_object_get_type(l_jobj_result)){
        case json_type_int:
            l_response->type = TYPE_RESPONSE_INTEGER;
            l_response->result_int = json_object_get_int64(l_jobj_result);
            break;
        case json_type_double:
            l_response->type = TYPE_RESPONSE_DOUBLE;
            l_response->result_double = json_object_get_double(l_jobj_result);
            break;
        case json_type_boolean:
            l_response->type = TYPE_RESPONSE_BOOLEAN;
            l_response->result_boolean = json_object_get_boolean(l_jobj_result);
            break;
        case json_type_string:
            l_response->type = TYPE_RESPONSE_STRING;
            l_response->result_string = dap_strdup(json_object_get_string(l_jobj_result));
            break;
        default:
            l_response->type = TYPE_RESPONSE_NULL;
            break;
        }
    } else {
        l_response->error = dap_json_rpc_create_from_json_object(l_jobj_error);
        l_response->type = TYPE_RESPONSE_NULL;
    }
//    json_object_put(l_jobj_id);
//    json_object_put(l_jobj_error);
//    json_object_put(l_jobj_result);
    json_object_put(l_jobj);
    return l_response;
}



