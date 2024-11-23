#include "dap_json_rpc_params.h"

#define LOG_TAG "dap_json_rpc_params"

dap_json_rpc_param_t* dap_json_rpc_create_param(void * data, dap_json_rpc_type_param_t type)
{
    dap_json_rpc_param_t *param = DAP_NEW(dap_json_rpc_param_t);
    if (!param) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    param->value_param = data;
    param->type = type;

    return param;
}

dap_json_rpc_params_t* dap_json_rpc_params_create(void)
{
    dap_json_rpc_params_t *params = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_json_rpc_params_t, NULL);
    return params;
}

void dap_json_rpc_params_add_data(dap_json_rpc_params_t *a_params, const void *a_value,
                                  dap_json_rpc_type_param_t a_type)
{
    dap_json_rpc_param_t *new_param = DAP_NEW_Z_RET_IF_FAIL(dap_json_rpc_param_t);
    new_param->type = a_type;
    size_t value_size;

    switch (a_type) {
        case TYPE_PARAM_STRING:
            new_param->value_param = dap_strdup((char*)a_value);
            break;
        case TYPE_PARAM_BOOLEAN:
            new_param->value_param = DAP_DUP((bool*)a_value);
            break;
        case TYPE_PARAM_INTEGER:
            new_param->value_param = DAP_DUP((int64_t*)a_value);
            break;
        case TYPE_PARAM_DOUBLE:
            new_param->value_param = DAP_DUP((double*)a_value);
            break;
        default:
            new_param->value_param = NULL;
            break;
    }
    dap_json_rpc_params_add_param(a_params, new_param);
}

void dap_json_rpc_params_add_param(dap_json_rpc_params_t *a_params, dap_json_rpc_param_t *a_param)
{
    dap_json_rpc_param_t **new_params = DAP_REALLOC_COUNT_RET_IF_FAIL(a_params->params, a_params->length + 1);
    new_params[a_params->length] = a_param;
    ++a_params->length;
}

void dap_json_rpc_param_remove(dap_json_rpc_param_t *param)
{
    DAP_DEL_MULTY(param->value_param, param);
}

void dap_json_rpc_params_remove_all(dap_json_rpc_params_t *a_params)
{
    for (uint32_t i=0x0 ; i < dap_json_rpc_params_length(a_params); i++){
        dap_json_rpc_param_remove(a_params->params[i]);
    }
    DAP_DEL_MULTY(a_params->params, a_params);
}

uint32_t dap_json_rpc_params_length(dap_json_rpc_params_t *a_params)
{
    return a_params->length;
}

void *dap_json_rpc_params_get(dap_json_rpc_params_t *a_params, uint32_t index)
{
    return a_params->length > index ? a_params->params[index]->value_param : NULL;
}

dap_json_rpc_type_param_t dap_json_rpc_params_get_type_param(dap_json_rpc_params_t *a_params, uint32_t index)
{
    return a_params->length > index ? a_params->params[index]->type : TYPE_PARAM_NULL;
}

dap_json_rpc_params_t * dap_json_rpc_params_create_from_array_list(json_object *a_array_list)
{
    if (a_array_list == NULL)
        return NULL;
    dap_json_rpc_params_t *params = dap_json_rpc_params_create();
    int length = json_object_array_length(a_array_list);

    for (int i = 0; i < length; i++){
        json_object *jobj = json_object_array_get_idx(a_array_list, i);
        json_type jobj_type = json_object_get_type(jobj);

        switch (jobj_type) {
            case json_type_string: {
                char * l_str_tmp = dap_strdup(json_object_get_string(jobj));
                dap_json_rpc_params_add_data(params, l_str_tmp, TYPE_PARAM_STRING);
                DAP_FREE(l_str_tmp);
                break;
            }
            case json_type_boolean: {
                bool l_bool_tmp = json_object_get_boolean(jobj);
                dap_json_rpc_params_add_data(params, &l_bool_tmp, TYPE_PARAM_BOOLEAN);
                break;
            }
            case json_type_int: {
                int64_t l_int_tmp = json_object_get_int64(jobj);
                dap_json_rpc_params_add_data(params, &l_int_tmp, TYPE_PARAM_INTEGER);
                break;
            }
            case json_type_double: {
                double l_double_tmp = json_object_get_double(jobj);
                dap_json_rpc_params_add_data(params, &l_double_tmp, TYPE_PARAM_DOUBLE);
                break;
            }
            default:
                dap_json_rpc_params_add_data(params, NULL, TYPE_PARAM_NULL);
        }
    }
    return params;
}

char *dap_json_rpc_params_get_string_json(dap_json_rpc_params_t * a_params)
{
    dap_return_val_if_fail(a_params, NULL);

    json_object *jobj_array = json_object_new_array();
    if (!jobj_array)
        return log_it(L_CRITICAL, "Failed to create JSON array"), NULL;

    for (uint32_t i = 0; i < a_params->length; i++){
        json_object *jobj_tmp = NULL;

        switch (a_params->params[i]->type) {
            case TYPE_PARAM_NULL:
                jobj_tmp = json_object_new_object();
                break;
            case TYPE_PARAM_STRING:
                jobj_tmp = json_object_new_string((char*)a_params->params[i]->value_param);
                break;
            case TYPE_PARAM_INTEGER:
                jobj_tmp = json_object_new_int64(*((int64_t*)a_params->params[i]->value_param));
                break;
            case TYPE_PARAM_DOUBLE:
                jobj_tmp = json_object_new_double(*((double*)a_params->params[i]->value_param));
                break;
            case TYPE_PARAM_BOOLEAN:
                jobj_tmp = json_object_new_boolean(*((bool*)a_params->params[i]->value_param));
                break;
            default:
                log_it(L_CRITICAL, "Invalid parameter type");
                json_object_put(jobj_array);
                return NULL;
        }
        json_object_array_add(jobj_array, jobj_tmp);
    };
    char *l_str = dap_strdup( json_object_to_json_string(jobj_array) );
    json_object_put(jobj_array);
    return l_str;
}

