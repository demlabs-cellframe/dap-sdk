#include "dap_json_rpc_response.h"
#include "dap_cli_server.h"
#include "json.h"

#define LOG_TAG "dap_json_rpc_response"
#define INDENTATION_LEVEL "    "

/**
 * @brief Initialize a new JSON-RPC response object
 * @return Pointer to newly allocated dap_json_rpc_response_t structure, or NULL on memory allocation failure
 */
dap_json_rpc_response_t *dap_json_rpc_response_init()
{
    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response)
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    return response;
}

/**
 * @brief Create a JSON-RPC response with specified result data
 * @param result Pointer to result data (type depends on 'type' parameter)
 * @param type Type of the result (string, integer, double, boolean, JSON object, or null)
 * @param id Request identifier to match response with request
 * @param a_version JSON-RPC protocol version
 * @return Pointer to newly created dap_json_rpc_response_t structure, or NULL on error
 */
dap_json_rpc_response_t* dap_json_rpc_response_create(void * result, dap_json_rpc_response_type_result_t type, int64_t id, int a_version) {

    dap_return_val_if_fail(result, NULL);
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

/**
 * @brief Free memory allocated for JSON-RPC response object
 * @param response Pointer to dap_json_rpc_response_t structure to be freed
 * @note Handles freeing of internal data based on response type
 */
void dap_json_rpc_response_free(dap_json_rpc_response_t *response)
{
    if (response) {
        switch(response->type) {
            case TYPE_RESPONSE_STRING:
                DAP_DEL_Z(response->result_string); break;
            case TYPE_RESPONSE_JSON:
                if (response->result_json_object)
                    dap_json_object_free(response->result_json_object);
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

/**
 * @brief Convert JSON-RPC response structure to JSON string representation
 * @param response Pointer to dap_json_rpc_response_t structure to be serialized
 * @return Dynamically allocated string containing JSON representation, or NULL on error
 * @note Caller is responsible for freeing the returned string
 * @note Internal response JSON object dereferenced and highly likely (sic!) to be freed by this call
 */
char *dap_json_rpc_response_to_string(dap_json_rpc_response_t* response) {
    if (!response) {
        return NULL;
    }

    dap_json_t *jobj = dap_json_object_new();
    if (!jobj) {
        return NULL;
    }
    
    // json type
    dap_json_object_add_int64(jobj, "type", response->type);

    // json result
    switch (response->type) {
        case TYPE_RESPONSE_STRING:
            dap_json_object_add_string(jobj, "result", response->result_string);
            break;
        case TYPE_RESPONSE_INTEGER:
            dap_json_object_add_int64(jobj, "result", response->result_int);
            break;
        case TYPE_RESPONSE_DOUBLE:
            dap_json_object_add_double(jobj, "result", response->result_double);
            break;
        case TYPE_RESPONSE_BOOLEAN:
            dap_json_object_add_bool(jobj, "result", response->result_boolean);
            break;
        case TYPE_RESPONSE_JSON:
            if (response->result_json_object) {
                dap_json_object_add_object(jobj, "result", response->result_json_object);
            } else {
                dap_json_object_add_null(jobj, "result");
            }
            break;
        case TYPE_RESPONSE_NULL:
            dap_json_object_add_null(jobj, "result");
            break;
    }

    // json id
    dap_json_object_add_int64(jobj, "id", response->id);
    // json version
    dap_json_object_add_int64(jobj, "version", response->version);

    // convert to string
    char* result_string = dap_json_to_string(jobj);
    dap_json_object_free(jobj);
    response->result_json_object = NULL;
    dap_json_rpc_response_free(response);

    return result_string;
}

/**
 * @brief Parse JSON string and create JSON-RPC response structure 
 * @param json_string JSON formatted string to be parsed
 * @return Pointer to newly created dap_json_rpc_response_t structure, or NULL on parsing error
 * @note Caller is responsible for freeing the returned structure using dap_json_rpc_response_free()
 */
dap_json_rpc_response_t* dap_json_rpc_response_from_string(const char* json_string) {
    dap_json_t *jobj = dap_json_parse_string(json_string);
    if (!jobj) {
        log_it(L_ERROR, "Error parsing JSON string");
        return NULL;
    }

    dap_json_rpc_response_t* response = DAP_NEW_Z(dap_json_rpc_response_t);
    if (!response) {
        dap_json_object_free(jobj);
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    // Parse version (direct int64 read)
    response->version = dap_json_object_get_int64(jobj, "version");
    if (response->version == 0) {
        log_it(L_DEBUG, "Can't find response version, apply version 1");
        response->version = 1;
    }

    // Parse type (direct int64 read)
    response->type = (int)dap_json_object_get_int64(jobj, "type");
    
    // Parse result (this is an object/array, so use get_object)
    dap_json_t *result_obj = dap_json_object_get_object(jobj, "result");
    if (result_obj) {
        switch (response->type) {
            case TYPE_RESPONSE_STRING: {
                const char *str_val = dap_json_get_string(result_obj);
                response->result_string = str_val ? dap_strdup(str_val) : NULL;
                dap_json_object_free(result_obj); // Free borrowed wrapper
                break;
            }
            case TYPE_RESPONSE_INTEGER:
                response->result_int = dap_json_get_int64(result_obj);
                dap_json_object_free(result_obj); // Free borrowed wrapper
                break;
            case TYPE_RESPONSE_DOUBLE:
                response->result_double = dap_json_get_double(result_obj);
                dap_json_object_free(result_obj); // Free borrowed wrapper
                break;
            case TYPE_RESPONSE_BOOLEAN:
                response->result_boolean = dap_json_get_bool(result_obj);
                dap_json_object_free(result_obj); // Free borrowed wrapper
                break;
            case TYPE_RESPONSE_JSON:
                // Link the result JSON object for response 
                response->result_json_object = result_obj;
                dap_json_object_ref(result_obj);
                break;
            case TYPE_RESPONSE_NULL:
                dap_json_object_free(result_obj); // Free borrowed wrapper
                break;
        }
    }
    
    // Parse id (direct int64 read)
    response->id = dap_json_object_get_int64(jobj, "id");

    dap_json_object_free(jobj);
    return response;
}

/**
 * @brief Check if command requires special JSON printing format
 * @param a_name Command name to check
 * @return Index of special command (1-based), or 0 if standard format should be used
 */
int json_print_commands(const char * a_name) {
    const char* long_cmd[] = {
            "file"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[i]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return i+1;
        }
    }
    return 0;
}

/**
 * @brief Print JSON-RPC response with custom format for transaction history command
 * @param response Pointer to dap_json_rpc_response_t structure containing transaction history data
 * @note Provides formatted output showing transaction statistics per network and chain
 */
void json_print_for_tx_history(dap_json_rpc_response_t* response) {
    if (!response || !response->result_json_object) {
        printf("Response is empty\n");
        return;
    }
    
    if (dap_json_is_array(response->result_json_object)) {
        size_t result_count = dap_json_array_length(response->result_json_object);
        if (result_count <= 0) {
            printf("Response array is empty\n");
            return;
        }
        
        for (size_t i = 0; i < result_count; i++) {
            dap_json_t *json_obj_result = dap_json_array_get_idx(response->result_json_object, i);
            if (!json_obj_result) {
                printf("Failed to get array element at index %zu\n", i);
                continue;
            }

            dap_json_t *j_obj_sum = dap_json_object_get_object(json_obj_result, "tx_sum");
            dap_json_t *j_obj_accepted = dap_json_object_get_object(json_obj_result, "accepted_tx");
            dap_json_t *j_obj_rejected = dap_json_object_get_object(json_obj_result, "rejected_tx");
            dap_json_t *j_obj_chain = dap_json_object_get_object(json_obj_result, "chain");
            dap_json_t *j_obj_net_name = dap_json_object_get_object(json_obj_result, "network");
            
            if (j_obj_sum && j_obj_accepted && j_obj_rejected) {
                int64_t sum = dap_json_get_int64(j_obj_sum);
                int64_t accepted = dap_json_get_int64(j_obj_accepted);
                int64_t rejected = dap_json_get_int64(j_obj_rejected);
                const char *net_name = j_obj_net_name ? dap_json_get_string(j_obj_net_name) : "unknown";
                const char *chain_name = j_obj_chain ? dap_json_get_string(j_obj_chain) : "unknown";
                
                printf("Print %ld transactions in network %s chain %s. \n"
                        "Of which %ld were accepted into the ledger and %ld were rejected.\n",
                        sum, net_name ? net_name : "unknown", 
                        chain_name ? chain_name : "unknown", accepted, rejected);
            } else {
                dap_json_print_object(json_obj_result, stdout, 0);
            }
            printf("\n");
            
            // All are borrowed references - no free needed
        }
    } else {
        dap_json_print_object(response->result_json_object, stdout, 0);
    }
}

/**
 * @brief Print JSON-RPC response with custom format for file-related commands
 * @param response Pointer to dap_json_rpc_response_t structure containing file command output
 * @note Handles nested array structures and prints file content appropriately
 */
void json_print_for_file_cmd(dap_json_rpc_response_t* response) {
    if (!response || !response->result_json_object) {
        printf("Response is empty\n");
        return;
    }
    
    if (dap_json_is_array(response->result_json_object)) {
        size_t result_count = dap_json_array_length(response->result_json_object);
        if (result_count <= 0) {
            printf("Response array is empty\n");
            return;
        }
        
        dap_json_t *first_element = dap_json_array_get_idx(response->result_json_object, 0);
        if (first_element && dap_json_is_array(first_element)) {
            for (size_t i = 0; i < result_count; i++) {
                dap_json_t *json_obj_result = dap_json_array_get_idx(response->result_json_object, i);
                if (!json_obj_result) {
                    printf("Failed to get array element at index %zu\n", i);
                    continue;
                }
                
                size_t inner_count = dap_json_array_length(json_obj_result);
                for (size_t j = 0; j < inner_count; j++) {
                    dap_json_t *json_obj = dap_json_array_get_idx(json_obj_result, j);
                    if (json_obj) {
                        const char *str_val = dap_json_get_string(json_obj);
                        if (str_val) {
                            printf("%s", str_val);
                        }
                        dap_json_object_free(json_obj);  // Free wrapper from inner loop
                    }
                }
                dap_json_object_free(json_obj_result);  // Free wrapper from outer loop
            }
            dap_json_object_free(first_element);  // Free first element wrapper
        } else {
            dap_json_object_free(first_element);  // Free even if not array
            dap_json_print_object(response->result_json_object, stdout, -1);
        }
    } else {
        dap_json_print_object(response->result_json_object, stdout, -1);
    }
}

/**
 * @brief Print JSON-RPC response with custom format for mempool list command
 * @param response Pointer to dap_json_rpc_response_t structure containing mempool data
 * @note Displays removed records count and datum information per chain
 */
void  json_print_for_mempool_list(dap_json_rpc_response_t* response){
    dap_json_t *json_obj_response = dap_json_array_get_idx(response->result_json_object, 0);
    if (!json_obj_response) return;
    
    dap_json_t *j_obj_net_name = dap_json_object_get_object(json_obj_response, "net");
    dap_json_t *j_arr_chains = dap_json_object_get_object(json_obj_response, "chains");
    if (!j_arr_chains) {
        // Free allocated wrappers before early return
        dap_json_object_free(j_obj_net_name);
        dap_json_object_free(json_obj_response);
        return;
    }
    
    size_t result_count = dap_json_array_length(j_arr_chains);
    for (size_t i = 0; i < result_count; i++) {
        dap_json_t *json_obj_result = dap_json_array_get_idx(j_arr_chains, i);
        if (!json_obj_result) continue;
        
        dap_json_t *j_obj_chain = dap_json_object_get_object(json_obj_result, "name");
        dap_json_t *j_obj_removed = dap_json_object_get_object(json_obj_result, "removed");
        dap_json_t *j_arr_datums = dap_json_object_get_object(json_obj_result, "datums");
        dap_json_t *j_arr_total = dap_json_object_get_object(json_obj_result, "total");
        
        const char *net_name = j_obj_net_name ? dap_json_get_string(j_obj_net_name) : "unknown";
        const char *chain_name = j_obj_chain ? dap_json_get_string(j_obj_chain) : "unknown";
        int64_t removed_count = j_obj_removed ? dap_json_get_int64(j_obj_removed) : 0;
        
        printf("Removed %ld records from the %s chain mempool in %s network.\n", 
                removed_count, chain_name ? chain_name : "unknown", net_name ? net_name : "unknown");
        printf("Datums:\n");
        if (j_arr_datums)
            dap_json_print_object(j_arr_datums, stdout, 1);
        // TODO total parser
        if (j_arr_total)
            dap_json_print_object(j_arr_total, stdout, 1);
        
        // All are borrowed references - no free needed
    }
    
    // All are borrowed references - no free needed
}

/**
 * @brief Print JSON-RPC response result to stdout based on response type
 * @param response Pointer to dap_json_rpc_response_t structure to print
 * @param cmd_name Command name used to determine special formatting rules
 * @return 0 on success, negative value on error
 * @note Automatically selects appropriate formatting based on response type and command name
 */
int dap_json_rpc_response_printf_result(dap_json_rpc_response_t* response, char * cmd_name, char ** cmd_params, int cmd_cnt) {
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
                    case 1: json_print_for_file_cmd(response); break;
                    default: {
                            dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find(cmd_name);
                            if (!l_cmd || l_cmd->func_rpc(response, cmd_params, cmd_cnt)){
                                dap_json_print_object(response->result_json_object, stdout, 0);
                            }
                        }
                        break;
                }
            } else {
                dap_json_print_object(response->result_json_object, stdout, 0);
            }
            break;
    }
    return 0;
}

/**
 * @brief Free memory allocated for JSON-RPC request JSON structure
 * @param l_request_JSON Pointer to dap_json_rpc_request_JSON_t structure to be freed
 * @note Frees all internal JSON objects and error structures
 */
void dap_json_rpc_request_JSON_free(dap_json_rpc_request_JSON_t *l_request_JSON)
{
    if (l_request_JSON->struct_error)
        dap_json_rpc_error_JSON_free(l_request_JSON->struct_error);
    if (l_request_JSON->obj_result)
        dap_json_object_free(l_request_JSON->obj_result);
    if (l_request_JSON->obj_error)
        dap_json_object_free(l_request_JSON->obj_error);
    if (l_request_JSON->obj_id)
        dap_json_object_free(l_request_JSON->obj_id);
    DAP_FREE(l_request_JSON);
}
