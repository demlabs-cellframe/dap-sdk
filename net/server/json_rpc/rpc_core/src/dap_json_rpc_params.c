#include "dap_json_rpc_params.h"
#include "dap_string.h"

#define LOG_TAG "dap_json_rpc_params"

dap_json_rpc_param_t* dap_json_rpc_create_param(void * data, dap_json_rpc_type_param_t type)
{
    dap_json_rpc_param_t param = { .type = type, .value_param = data };
    return DAP_DUP(&param);
}

dap_json_rpc_params_t* dap_json_rpc_params_create(void)
{
    return DAP_NEW_Z(dap_json_rpc_params_t);
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
        case TYPE_PARAM_JSON: {
            json_object *l_jobj_value = (json_object*)a_value;
            json_object *l_obj_dist = NULL;
            json_object_deep_copy(l_jobj_value, &l_obj_dist, NULL);
            new_param->value_param = l_obj_dist;
        } break;
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
    a_params->params = new_params;
    ++a_params->length;
}

void dap_json_rpc_param_remove(dap_json_rpc_param_t *param)
{
    dap_return_if_fail(param);
    if (param->type == TYPE_PARAM_JSON) {
        json_object *l_obj = (json_object*)param->value_param;
        json_object_put(l_obj);
    } else
        DAP_DELETE(param->value_param);
    DAP_DELETE(param);
}

void dap_json_rpc_params_remove_all(dap_json_rpc_params_t *a_params)
{
    dap_return_if_fail(a_params);
    for (uint32_t i=0x0 ; i < dap_json_rpc_params_length(a_params); i++){
        dap_json_rpc_param_remove(a_params->params[i]);
    }
    DAP_DEL_MULTY(a_params->params, a_params);
}

uint32_t dap_json_rpc_params_length(dap_json_rpc_params_t *a_params)
{
    return a_params ? a_params->length : 0;
}

void *dap_json_rpc_params_get(dap_json_rpc_params_t *a_params, uint32_t index)
{
    return a_params && a_params->length > index ? a_params->params[index]->value_param : NULL;
}

dap_json_rpc_type_param_t dap_json_rpc_params_get_type_param(dap_json_rpc_params_t *a_params, uint32_t index)
{
    return a_params && a_params->length > index ? a_params->params[index]->type : TYPE_PARAM_NULL;
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
            case json_type_object: {
                dap_json_rpc_params_add_data(params, jobj, TYPE_PARAM_JSON);
                break;
            }
            default:
                dap_json_rpc_params_add_data(params, NULL, TYPE_PARAM_NULL);
        }
    }
    return params;
}

dap_json_rpc_params_t * dap_json_rpc_params_create_from_subcmd_and_args(json_object *a_subcmd, json_object *a_args, const char* a_method)
{
    if (a_method == NULL )
        return NULL;
    dap_json_rpc_params_t *params = dap_json_rpc_params_create();

    dap_string_t * l_str_tmp = dap_string_new("");
    // add subcmd to params
    dap_string_append_printf(l_str_tmp, "%s;", a_method);
    if(a_subcmd){
        enum json_type l_subcmd_type = json_object_get_type(a_subcmd);
        if(l_subcmd_type == json_type_array){
            int length = json_object_array_length(a_subcmd);
            
            for (int i = 0; i < length; i++){
                json_object *jobj = json_object_array_get_idx(a_subcmd, i);
                json_type jobj_type = json_object_get_type(jobj);

                if (jobj_type != json_type_string){
                    log_it(L_ERROR, "Bad subcommand type");
                    dap_string_free(l_str_tmp, true);
                    return NULL;
                }

                dap_string_append_printf(l_str_tmp, "%s;", json_object_get_string(jobj));
            }
        } else if (l_subcmd_type == json_type_string) { 
            dap_string_append_printf(l_str_tmp, "%s;", json_object_get_string( a_subcmd));
        } else {
            return log_it(L_CRITICAL, "Subcomand must be array or string type."), dap_string_free(l_str_tmp, true),  NULL;
        }
    }

    if (a_args){        
        json_object_object_foreach(a_args, key, val){
            const char *l_key_str = NULL;
            const char *l_val_str = NULL;
            enum json_type l_arg_type = json_object_get_type(val);
            if(l_arg_type == json_type_string || 
                l_arg_type == json_type_null || l_arg_type == json_type_object) {
                l_key_str = key;
                l_val_str = json_object_get_string(val);
            } else if(l_arg_type == json_type_array){
                int length = json_object_array_length(val);
                dap_string_append_printf(l_str_tmp, "-%s;", key);

                for (int i = 0; i < length; i++){
                    json_object *jobj = json_object_array_get_idx(val, i);
                    json_type jobj_type = json_object_get_type(jobj);

                    if (jobj_type != json_type_string){
                        log_it(L_ERROR, "Bad subcommand type");
                        dap_string_free(l_str_tmp, true);
                        return NULL;
                    }

                    dap_string_append_printf(l_str_tmp, "%s%s", json_object_get_string(jobj), i == length - 1 ? ";" : ",");
                }
                continue;
            }

            if(l_key_str){
                dap_string_append_printf(l_str_tmp, "-%s;%s;", l_key_str, l_val_str ? l_val_str : "");
            } else {
                return log_it(L_CRITICAL, "Bad argument!"), dap_string_free(l_str_tmp, true),  NULL;
            }
        }
    }

    l_str_tmp->str[strlen(l_str_tmp->str) - 1] = '\0';

    dap_json_rpc_params_add_data(params, l_str_tmp->str, TYPE_PARAM_STRING);
    dap_string_free(l_str_tmp, true);

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

