#include <string.h>
#include <ctype.h>
#include "dap_json_rpc_response.h"
#include "dap_cli_server.h"
#include "dap_json.h"
#include "internal/dap_json_internal.h"

#define LOG_TAG "dap_json_rpc_response"

static bool s_has_json_flag(char **a_params, int a_cnt)
{
    for (int i = 0; i < a_cnt; i++) {
        if (a_params[i] && !strcmp(a_params[i], "-json"))
            return true;
    }
    return false;
}

/*
 * Lightweight JSON string → human-readable text converter.
 * Works directly on the serialized JSON string (no dap_json_t traversal needed,
 * bypassing IMMUTABLE mode limitations).
 */

static const char *s_skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }

static const char *s_skip_string(const char *p, char *a_buf, size_t a_buf_sz)
{
    if (*p != '"') return p;
    p++;
    size_t pos = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            p++;
            char c = *p;
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': case '\\': case '/': break;
                default: break;
            }
            if (a_buf && pos + 1 < a_buf_sz) a_buf[pos++] = c;
        } else {
            if (a_buf && pos + 1 < a_buf_sz) a_buf[pos++] = *p;
        }
        p++;
    }
    if (a_buf && pos < a_buf_sz) a_buf[pos] = '\0';
    if (*p == '"') p++;
    return p;
}

static const char *s_skip_value(const char *p)
{
    p = s_skip_ws(p);
    if (*p == '"') return s_skip_string(p, NULL, 0);
    if (*p == '{') {
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"') p = s_skip_string(p, NULL, 0) - 1;
            p++;
        }
        return p;
    }
    if (*p == '[') {
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == '"') p = s_skip_string(p, NULL, 0) - 1;
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;
    return p;
}

static size_t s_read_scalar(const char *p, char *a_buf, size_t a_buf_sz)
{
    p = s_skip_ws(p);
    if (*p == '"') {
        s_skip_string(p, a_buf, a_buf_sz);
        return strlen(a_buf);
    }
    size_t pos = 0;
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) {
        if (pos + 1 < a_buf_sz) a_buf[pos++] = *p;
        p++;
    }
    if (pos < a_buf_sz) a_buf[pos] = '\0';
    return pos;
}

static void s_print_indent(FILE *f, int level) { for (int i = 0; i < level; i++) fputs("  ", f); }

static void s_json_str_to_text(const char *p, FILE *f, int indent);

static void s_print_object(const char *p, FILE *f, int indent)
{
    p = s_skip_ws(p);
    if (*p != '{') return;
    p++; // skip '{'
    char key[256];
    while (*p) {
        p = s_skip_ws(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') break;
        p = s_skip_string(p, key, sizeof(key));
        p = s_skip_ws(p);
        if (*p == ':') p++;
        p = s_skip_ws(p);

        if (*p == '{') {
            s_print_indent(f, indent);
            fprintf(f, "%s:\n", key);
            const char *start = p;
            s_print_object(p, f, indent + 1);
            p = s_skip_value(start);
        } else if (*p == '[') {
            s_print_indent(f, indent);
            fprintf(f, "%s:\n", key);
            const char *start = p;
            s_json_str_to_text(p, f, indent + 1);
            p = s_skip_value(start);
        } else {
            char val[4096];
            s_read_scalar(p, val, sizeof(val));
            s_print_indent(f, indent);
            fprintf(f, "%s: %s\n", key, val);
            p = s_skip_value(p);
        }
    }
}

static void s_json_str_to_text(const char *p, FILE *f, int indent)
{
    p = s_skip_ws(p);
    if (*p == '[') {
        p++; // skip '['
        bool first = true;
        while (*p) {
            p = s_skip_ws(p);
            if (*p == ']') break;
            if (*p == ',') { p++; continue; }
            if (*p == '{') {
                if (!first) fprintf(f, "\n");
                first = false;
                const char *start = p;
                s_print_object(p, f, indent);
                p = s_skip_value(start);
            } else if (*p == '[') {
                const char *start = p;
                s_json_str_to_text(p, f, indent);
                p = s_skip_value(start);
            } else {
                char val[4096];
                s_read_scalar(p, val, sizeof(val));
                s_print_indent(f, indent);
                fprintf(f, "%s\n", val);
                p = s_skip_value(p);
            }
        }
    } else if (*p == '{') {
        s_print_object(p, f, indent);
    } else {
        char val[4096];
        s_read_scalar(p, val, sizeof(val));
        if (val[0]) fprintf(f, "%s\n", val);
    }
}

static bool s_json_str_has_errors(const char *p)
{
    return strstr(p, "\"errors\"") != NULL;
}

static void s_print_error_messages(const char *p, FILE *f)
{
    /* Find all "message":"..." and print them line by line */
    const char *needle = "\"message\"";
    while ((p = strstr(p, needle)) != NULL) {
        p += strlen(needle);
        p = s_skip_ws(p);
        if (*p == ':') p++;
        p = s_skip_ws(p);
        if (*p == '"') {
            char msg[4096];
            s_skip_string(p, msg, sizeof(msg));
            fprintf(f, "%s\n", msg);
            p = s_skip_value(p);
        }
    }
}

dap_json_rpc_response_t *dap_json_rpc_response_init()
{
    dap_json_rpc_response_t *response = DAP_NEW(dap_json_rpc_response_t);
    if (!response)
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    return response;
}

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

char *dap_json_rpc_response_to_string(dap_json_rpc_response_t* response) {
    if (!response) {
        return NULL;
    }

    dap_json_t *jobj = dap_json_object_new();
    if (!jobj) {
        return NULL;
    }
    
    dap_json_object_add_int64(jobj, "type", response->type);

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

    dap_json_object_add_int64(jobj, "id", response->id);
    dap_json_object_add_int64(jobj, "version", response->version);

    char* result_string = dap_json_to_string(jobj);
    dap_json_object_free(jobj);
    response->result_json_object = NULL;
    dap_json_rpc_response_free(response);

    return result_string;
}

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

    response->version = dap_json_object_get_int64(jobj, "version");
    if (response->version == 0) {
        log_it(L_DEBUG, "Can't find response version, apply version 1");
        response->version = 1;
    }

    response->type = (int)dap_json_object_get_int64(jobj, "type");
    
    // Parse result - may be an object, array, string, or primitive depending on type
    dap_json_t *result_obj = NULL;
    dap_json_object_get_ex(jobj, "result", &result_obj);
    if (result_obj) {
        switch (response->type) {
            case TYPE_RESPONSE_STRING: {
                const char *str_val = dap_json_get_string(result_obj);
                response->result_string = str_val ? dap_strdup(str_val) : NULL;
                dap_json_object_free(result_obj);
                break;
            }
            case TYPE_RESPONSE_INTEGER:
                response->result_int = dap_json_get_int64(result_obj);
                dap_json_object_free(result_obj);
                break;
            case TYPE_RESPONSE_DOUBLE:
                response->result_double = dap_json_get_double(result_obj);
                dap_json_object_free(result_obj);
                break;
            case TYPE_RESPONSE_BOOLEAN:
                response->result_boolean = dap_json_get_bool(result_obj);
                dap_json_object_free(result_obj);
                break;
            case TYPE_RESPONSE_JSON:
                // Transfer input buffer ownership from parent to sub-wrapper so
                // freeing jobj won't destroy the shared input buffer.
                // Tape is arena-allocated and doesn't need ownership transfer.
                if (result_obj->mode == DAP_JSON_MODE_IMMUTABLE &&
                    jobj->mode == DAP_JSON_MODE_IMMUTABLE) {
                    result_obj->mode_data.immutable.owned_input_copy =
                        jobj->mode_data.immutable.owned_input_copy;
                    jobj->mode_data.immutable.owned_input_copy = NULL;
                }
                response->result_json_object = result_obj;
                break;
            case TYPE_RESPONSE_NULL:
                dap_json_object_free(result_obj);
                break;
        }
    }
    
    response->id = dap_json_object_get_int64(jobj, "id");

    dap_json_object_free(jobj);
    return response;
}

/**
 * @brief Check if command requires special JSON printing format
 * @param a_name Command name to check
 * @return Index of special command (1-based), or 0 if standard format should be used
 */
static int json_print_commands(const char * a_name) {
    const char* long_cmd[] = {
            "file"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return i+1;
        }
    }
    return 0;
}

/**
 * @brief Print JSON-RPC response with custom format for file-related commands
 * @param response Pointer to dap_json_rpc_response_t structure containing file command output
 * @note Handles nested array structures and prints file content appropriately
 */
static void json_print_for_file_cmd(dap_json_rpc_response_t* response) {
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
                        dap_json_object_free(json_obj);
                    }
                }
                dap_json_object_free(json_obj_result);
            }
            dap_json_object_free(first_element);
        } else {
            dap_json_object_free(first_element);
            dap_json_print_object(response->result_json_object, stdout, -1);
        }
    } else {
        dap_json_print_object(response->result_json_object, stdout, -1);
    }
}

int dap_json_rpc_response_printf_result(dap_json_rpc_response_t* response, char * cmd_name, char ** cmd_params, int cmd_cnt) {
    if (!response) {
        printf("Empty response");
        return -1;
    }

    bool l_json_mode = s_has_json_flag(cmd_params, cmd_cnt);

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
                    default: 
                        dap_json_print_object(response->result_json_object, stdout, 0);
                        break;
                }
            } else {
                if (l_json_mode) {
                    dap_json_print_object(response->result_json_object, stdout, 0);
                } else {
                    char *l_str = dap_json_to_string(response->result_json_object);
                    if (l_str) {
                        if (s_json_str_has_errors(l_str))
                            s_print_error_messages(l_str, stdout);
                        else
                            s_json_str_to_text(l_str, stdout, 0);
                        DAP_DELETE(l_str);
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
    if (l_request_JSON->obj_result)
        dap_json_object_free(l_request_JSON->obj_result);
    if (l_request_JSON->obj_error)
        dap_json_object_free(l_request_JSON->obj_error);
    if (l_request_JSON->obj_id)
        dap_json_object_free(l_request_JSON->obj_id);
    DAP_FREE(l_request_JSON);
}
