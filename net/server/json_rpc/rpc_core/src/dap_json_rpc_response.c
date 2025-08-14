#include "dap_json_rpc_response.h"

#define LOG_TAG "dap_json_rpc_response"
#define INDENTATION_LEVEL "    "

dap_json_rpc_response_t *dap_json_rpc_response_init()
{
    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response)
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    return response;
}

dap_json_rpc_response_t* dap_json_rpc_response_create(void * result, dap_json_rpc_response_type_result_t type, int64_t id, int a_version) {

    if (!result) {
        log_it(L_CRITICAL, "Invalid arguments");
        return NULL;
    }

    dap_json_rpc_response_t *response = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_json_rpc_response_t, NULL);
    
    response->id = id;
    response->type = type;
    response->version = a_version;

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
            response->result_json_object = result; break;
        case TYPE_RESPONSE_NULL:
            break;
        default:
            log_it(L_ERROR, "Wrong response type");
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
                if (response->result_json_object)
                    json_object_put(response->result_json_object);
                break;
            case TYPE_RESPONSE_INTEGER:
            case TYPE_RESPONSE_DOUBLE:
            case TYPE_RESPONSE_BOOLEAN:
            case TYPE_RESPONSE_NULL:
            break;
            default:
                log_it(L_ERROR, "Unsupported response type");
                break;
        }
        DAP_FREE(response);
    }
}

char* dap_json_rpc_response_to_string(const dap_json_rpc_response_t* response) {
    if (!response) {
        return NULL;
    }

    json_object* jobj = json_object_new_object();
    if (!jobj) {
        log_it(L_ERROR, "Can't create json object");
        return NULL;
    }
    // json type
    json_object_object_add(jobj, "type", json_object_new_int(response->type));

    // json result
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
            json_object_object_add(jobj, "result", json_object_get(response->result_json_object));
            break;
        case TYPE_RESPONSE_NULL:
            json_object_object_add(jobj, "result", NULL);
            break;
    }

    // json id
    json_object_object_add(jobj, "id", json_object_new_int64(response->id));
    // json version
    json_object_object_add(jobj, "version", json_object_new_int64(response->version));

    // convert to string
    const char* json_string = json_object_to_json_string(jobj);
    if (!json_string) {
        log_it(L_ERROR, "Can't convert json object to string");
        json_object_put(jobj);
        return NULL;
    }
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

    json_object* version_obj = NULL;
    if (json_object_object_get_ex(jobj, "version", &version_obj))
        response->version = json_object_get_int64(version_obj);
    else {
        log_it(L_DEBUG, "Can't find response version, apply version 1");
        response->version = 1;
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

int json_print_commands(const char * a_name) {
    const char* long_cmd[] = {
            "tx_history"
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
                    printf(INDENTATION_LEVEL); // indentation level
                }
                printf("%s: ", key);
                json_print_value(val, key, indent_level + 1, false);
                printf("\n");
            }
            break;
        }
        case json_type_array: {
            int length = json_object_array_length(obj);
            for (int i = 0; i < length; i++) {
                for (int j = 0; j <= indent_level; j++) {
                    printf(INDENTATION_LEVEL); // indentation level
                }
                json_object *item = json_object_array_get_idx(obj, i);
                json_print_value(item, NULL, indent_level + 1, length - 1 - i);
                printf("\n");
            }
            break;
        }
        default:
            break;
    }
}

void json_print_value(json_object *obj, const char *key, int indent_level, bool print_separator) {
    enum json_type type = json_object_get_type(obj);

    switch (type) {
        case json_type_string:
            printf(print_separator ? "%s, " : "%s", json_object_get_string(obj));
            break;
        case json_type_int:
            printf("%"DAP_INT64_FORMAT, json_object_get_int64(obj));
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
    if (!response || !response->result_json_object) {
        printf("Response is empty\n");
        return;
    }
    if (json_object_get_type(response->result_json_object) == json_type_array) {
        int result_count = json_object_array_length(response->result_json_object);
        if (result_count <= 0) {
            printf("Response array is empty\n");
            return;
        }
        for (int i = 0; i < result_count; i++) {
            struct json_object *json_obj_result = json_object_array_get_idx(response->result_json_object, i);
            if (!json_obj_result) {
                printf("Failed to get array element at index %d\n", i);
                continue;
            }

            json_object *j_obj_sum, *j_obj_accepted, *j_obj_rejected, *j_obj_chain, *j_obj_net_name;
            if (json_object_object_get_ex(json_obj_result, "tx_sum", &j_obj_sum) &&
                json_object_object_get_ex(json_obj_result, "accepted_tx", &j_obj_accepted) &&
                json_object_object_get_ex(json_obj_result, "rejected_tx", &j_obj_rejected)) {
                json_object_object_get_ex(json_obj_result, "chain", &j_obj_chain);
                json_object_object_get_ex(json_obj_result, "network", &j_obj_net_name);

                if (j_obj_sum && j_obj_accepted && j_obj_rejected && j_obj_chain && j_obj_net_name) {
                    printf("Print %d transactions in network %s chain %s. \n"
                            "Of which %d were accepted into the ledger and %d were rejected.\n",
                            json_object_get_int(j_obj_sum), json_object_get_string(j_obj_net_name),
                            json_object_get_string(j_obj_chain), json_object_get_int(j_obj_accepted), json_object_get_int(j_obj_rejected));
                } else {
                    printf("Missing required fields in array element at index %d\n", i);
                }
            } else {
                json_print_object(json_obj_result, 0);
            }
            printf("\n");
        }
    } else {
        json_print_object(response->result_json_object, 0);
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
            if (response->version == 1) {
                switch(json_print_commands(cmd_name)) {
                    case 1: json_print_for_tx_history(response); break; return 0;
                    // case 2: json_print_for_mempool_list(response); break; return 0;
                    default: {
                            json_print_object(response->result_json_object, 0);
                        }
                        break;
                }
            } else {
                json_print_object(response->result_json_object, 0);
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

// void dap_json_rpc_response_send(dap_json_rpc_response_t *a_response, dap_http_simple_t *a_client)
// {
//     json_object *l_jobj = json_object_new_object();
//     json_object *l_obj_id = json_object_new_int64(a_response->id);
//     json_object *l_obj_result = NULL;
//     const char *str_response = NULL;
//     switch (a_response->type) {
//         case TYPE_RESPONSE_STRING:
//             l_obj_result = json_object_new_string(a_response->result_string);
//             break;
//         case TYPE_RESPONSE_DOUBLE:
//             l_obj_result = json_object_new_double(a_response->result_double);
//             break;
//         case TYPE_RESPONSE_BOOLEAN:
//             l_obj_result = json_object_new_boolean(a_response->result_boolean);
//             break;
//         case TYPE_RESPONSE_INTEGER:
//             l_obj_result = json_object_new_int64(a_response->result_int);
//             break;
//         case TYPE_RESPONSE_JSON:
//             l_obj_result = json_object_get(a_response->result_json_object);
//             json_object_put(a_response->result_json_object);
//             break;
//         default:{}
//     }
//     json_object_object_add(l_jobj, "result", l_obj_result);
//     json_object_object_add(l_jobj, "id", l_obj_id);
//     str_response = json_object_to_json_string(l_jobj);
//     dap_http_simple_reply(a_client, (void *)str_response, strlen(str_response));
//     json_object_put(l_jobj);
// }
