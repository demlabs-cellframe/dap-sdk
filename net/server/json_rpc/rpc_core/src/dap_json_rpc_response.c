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

    if (!result) {
        log_it(L_CRITICAL, "Invalid arguments");
        return NULL;
    }

    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    
    response->id = id;
    json_object* errors = dap_json_rpc_error_get();
    if (!errors) {
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
                response->result_json_object = json_object_get(result); break;
            case TYPE_RESPONSE_NULL:
                break;
            default:
                log_it(L_ERROR, "Wrong response type");
                DAP_FREE(response);
                return NULL;
        }
    } else {
        switch(response->type) {
            case TYPE_RESPONSE_STRING:
                DAP_DEL_Z(result);
                break;
            case TYPE_RESPONSE_JSON:
                if (result)
                    json_object_put(result);
                break;
            case TYPE_RESPONSE_BOOLEAN:
            case TYPE_RESPONSE_DOUBLE:
            case TYPE_RESPONSE_INTEGER:
            case TYPE_RESPONSE_NULL:
            case TYPE_RESPONSE_ERROR:
                break;
        }
        response->type = TYPE_RESPONSE_ERROR;
        response->json_arr_errors = errors;
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
                if (response->result_json_object)
                    json_object_put(response->result_json_object);
                break;
            case TYPE_RESPONSE_INTEGER:
            case TYPE_RESPONSE_DOUBLE:
            case TYPE_RESPONSE_BOOLEAN:
            case TYPE_RESPONSE_NULL:
            case TYPE_RESPONSE_ERROR:
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
    // json type
    json_object_object_add(jobj, "type", json_object_new_int(response->type));

    // json result
    switch (response->type) {
        case TYPE_RESPONSE_STRING:
            json_object_object_add(jobj, "result", json_object_new_string(response->result_string));
            break;
        case TYPE_RESPONSE_ERROR:
            json_object_object_add(jobj, "result", json_object_new_null());
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
            json_object_object_add(jobj, "result", json_object_get(response->result_json_object));
            break;
        case TYPE_RESPONSE_NULL:
            json_object_object_add(jobj, "result", NULL);
            break;
    }

    // json errors
    if (response->type == TYPE_RESPONSE_ERROR) {
        json_object_object_add(jobj, "errors", response->json_arr_errors);
    } else {
        json_object_object_add(jobj, "errors", json_object_new_null());
    }

    // json id
    json_object_object_add(jobj, "id", json_object_new_int64(response->id));

    // convert to string
    const char* json_string = json_object_to_json_string(jobj);
    char* result_string = strdup(json_string);
    json_object_put(jobj);

    return result_string;
}

dap_json_rpc_response_t* dap_json_rpc_response_from_string(const char* json_string) {
    json_object* jobj = json_tokener_parse(json_string);
    if (!jobj) {
        // log_it(L_ERROR, "Error parsing JSON string");
        printf("Error parsing JSON string");
        return NULL;
    }

    dap_json_rpc_response_t* response = malloc(sizeof(dap_json_rpc_response_t));
    if (!response) {
        json_object_put(jobj);
        // log_it(L_CRITICAL, "Memmory allocation error");
        printf( "Memmory allocation error");
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
                case TYPE_RESPONSE_ERROR:
                    json_object_object_get_ex(jobj, "errors", &result_obj);
                    response->json_arr_errors = json_object_get(result_obj);
                case TYPE_RESPONSE_NULL:
                    break;
            }
        }
    }
    json_object* result_id = NULL;
    json_object_object_get_ex(jobj, "id", &result_id);
    response->id = json_object_get_int64(result_id);

    // json_object_put(jobj);
    return response;
}

int json_print_commands(const char * a_name) {
    const char* long_cmd[] = {
            "tx_history",
            "mempool_list",
            "net"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return i+1;
        }
    }
    return 0;
}

void json_print_object(json_object *obj, int indent_level) {
    enum json_type type = json_object_get_type(obj);

    switch (type) {
        case json_type_object: {
            json_object_object_foreach(obj, key, val) {
                for (int i = 0; i <= indent_level; i++) {
                    printf("    "); // indentation level
                }
                printf("%s: ", key);
                json_print_value(val, key, indent_level + 1);
                printf("\n");
            }
            break;
        }
        case json_type_array: {
            int length = json_object_array_length(obj);
            for (int i = 0; i < length; i++) {
                json_object *item = json_object_array_get_idx(obj, i);
                json_print_value(item, NULL, indent_level + 1);
            }
            break;
        }
        default:
            break;
    }
}

void json_print_value(json_object *obj, const char *key, int indent_level) {
    enum json_type type = json_object_get_type(obj);

    switch (type) {
        case json_type_string:
            printf("%s", json_object_get_string(obj));
            break;
        case json_type_int:
            printf("%d", json_object_get_int(obj));
            break;
        case json_type_double:
            printf("%lf", json_object_get_double(obj));
            break;
        case json_type_boolean:
            printf("%s", json_object_get_boolean(obj) ? "true" : "false");
            break;
        case json_type_object:
        case json_type_array:
            printf("\n");
            json_print_object(obj, indent_level);
            break;
        default:
            break;
    }
}

void json_print_for_tx_history(dap_json_rpc_response_t* response) {
    int result_count = json_object_array_length(response->result_json_object);
    json_object * json_obj_items = json_object_array_get_idx(response->result_json_object, 0);
    json_print_object(json_obj_items, 0);
    if (result_count == 2) {
        json_object * json_obj_response = json_object_array_get_idx(response->result_json_object, 1);
        json_object * j_obj_net_name, * j_obj_chain, *j_obj_sum, *j_obj_accepted, *j_obj_rejected;
        json_object_object_get_ex(json_obj_response, "network", &j_obj_net_name);
        json_object_object_get_ex(json_obj_response, "chain", &j_obj_chain);
        json_object_object_get_ex(json_obj_response, "tx_sum", &j_obj_sum);
        json_object_object_get_ex(json_obj_response, "accepted_tx", &j_obj_accepted);
        json_object_object_get_ex(json_obj_response, "rejected_tx", &j_obj_rejected);
        printf("Chain %s in network %s contains %d transactions. \n"
                "Of which %d were accepted into the ledger and %d were rejected.\n", 
                json_object_get_string(j_obj_chain), json_object_get_string(j_obj_net_name), 
                json_object_get_int(j_obj_sum), json_object_get_int(j_obj_accepted), json_object_get_int(j_obj_rejected));
    }
}

void  json_print_for_mempool_list(dap_json_rpc_response_t* response){
    json_object * json_obj_response = json_object_array_get_idx(response->result_json_object, 0);
    json_object * j_obj_net_name, * j_arr_chains, * j_obj_chain, *j_obj_removed, *j_arr_datums, *j_arr_total;
    json_object_object_get_ex(json_obj_response, "net", &j_obj_net_name);
    json_object_object_get_ex(json_obj_response, "chains", &j_arr_chains);
    int result_count = json_object_array_length(j_arr_chains);
    for (int i = 0; i < result_count; i++) {
        json_object * json_obj_result = json_object_array_get_idx(j_arr_chains, i);
        json_object_object_get_ex(json_obj_result, "name", &j_obj_chain);
        json_object_object_get_ex(json_obj_result, "removed", &j_obj_removed);
        json_object_object_get_ex(json_obj_result, "datums", &j_arr_datums);
        json_object_object_get_ex(json_obj_result, "total", &j_arr_total);
        printf("Removed %d records from the %s chain mempool in %s network.\n", 
                json_object_get_int(j_obj_removed), json_object_get_string(j_obj_chain), json_object_get_string(j_obj_net_name));
        printf("Datums:\n");
        json_print_object(j_arr_datums, 1);
        // TODO total parser
        json_print_object(j_arr_total, 1);
    }
}

int dap_json_rpc_response_printf_result(dap_json_rpc_response_t* response, char * cmd_name) {
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
            printf("response type is NULL\n");
            break;
        case TYPE_RESPONSE_JSON:
            if (!response->result_json_object) {
                printf("json object is NULL\n");
                return -2;
            }
            switch(json_print_commands(cmd_name)) {
                case 1: json_print_for_tx_history(response); break; return 0;
                // case 2: json_print_for_mempool_list(response); break; return 0;
                default: {
                        int json_type = 0;
                        json_object_is_type(response->result_json_object, json_type);
                        if (json_type == json_type_array) {                                     /* print json array */ 
                            int result_count = json_object_array_length(response->result_json_object);
                            if (result_count <= 0) {
                                printf("response json array length is 0\n");
                                return -3;
                            }
                            for (int i = 0; i < result_count; i++) {
                                struct json_object * json_obj_result = json_object_array_get_idx(response->result_json_object, i);
                                json_print_object(json_obj_result, 0);
                                printf("\n");
                                json_object_put(json_obj_result);
                            }
                        } else {                                                                /* print json */ 
                            json_print_object(response->result_json_object, 0);
                        }
                    }
                    break;
            }
            break;
        case TYPE_RESPONSE_ERROR:
            if (!response->json_arr_errors) {
                printf("json errors is NULL");
                return -4;
            }
            int errors_count = json_object_array_length(response->json_arr_errors);
            for (int i = 0; i < errors_count; i++) {
                    struct json_object *json_obj = json_object_array_get_idx(response->json_arr_errors, i);
                    struct json_object *error_obj;
                    if (json_object_object_get_ex(json_obj, "error", &error_obj)) {
                        struct json_object *code_obj, *message_obj;
                        if (json_object_object_get_ex(error_obj, "code", &code_obj) &&
                            json_object_object_get_ex(error_obj, "message", &message_obj)) {
                            int code = json_object_get_int(code_obj);
                            const char *message = json_object_get_string(message_obj);
                            printf("Error %d: %s\n", code, message);
                        }
                    }
            }
            break;
    }
    return 0;
}

void dap_json_rpc_request_JSON_free(dap_json_rpc_request_JSON_t *l_request_JSON)
{
    if (l_request_JSON->struct_error)
        dap_json_rpc_error_JSON_free(l_request_JSON->struct_error);
    json_object_put(l_request_JSON->obj_result);
    json_object_put(l_request_JSON->obj_error);
    json_object_put(l_request_JSON->obj_id);
    DAP_FREE(l_request_JSON);
}

// doesn't work
void dap_json_rpc_response_send(dap_json_rpc_response_t *a_response, dap_http_simple_t *a_client)
{
    json_object *l_jobj = json_object_new_object();
    json_object *l_obj_id = json_object_new_int64(a_response->id);
    json_object *l_obj_result = NULL;
    json_object *l_obj_error = NULL;
    const char *str_response = NULL;
    if (a_response->json_arr_errors == NULL){
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
        // l_obj_error = json_object_new_object();
        // json_object *l_obj_err_code = json_object_new_int(a_response->error->code_error);
        // json_object *l_obj_err_msg = json_object_new_string(a_response->error->msg);
        // json_object_object_add(l_obj_error, "code", l_obj_err_code);
        // json_object_object_add(l_obj_error, "message", l_obj_err_msg);
    }
    json_object_object_add(l_jobj, "result", l_obj_result);
    json_object_object_add(l_jobj, "id", l_obj_id);
    json_object_object_add(l_jobj, "error", l_obj_error);
    str_response = json_object_to_json_string(l_jobj);
    dap_http_simple_reply(a_client, (void *)str_response, strlen(str_response));
    json_object_put(l_jobj);
}
