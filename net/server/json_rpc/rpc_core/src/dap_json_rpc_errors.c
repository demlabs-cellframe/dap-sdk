#include "dap_json_rpc_errors.h"

#define LOG_TAG "dap_json_rpc_errors"

static dap_json_rpc_error_t *s_errors;
int _dap_json_rpc_error_cmp_by_code(dap_json_rpc_error_t *a_error, int a_code_error);

int dap_json_rpc_error_init(void)
{
    s_errors = NULL;
    return  0;
}

void dap_json_rpc_error_deinit(void)
{
    dap_json_rpc_error_t *err, *tmp;
    if (s_errors != NULL){
        LL_FOREACH_SAFE(s_errors, err, tmp){
            LL_DELETE(s_errors, err);
            DAP_FREE(err->msg);
            DAP_FREE(err);
        }
    }
}

dap_json_rpc_error_JSON_t * dap_json_rpc_error_JSON_create()
{
    dap_json_rpc_error_JSON_t *l_json = DAP_NEW(dap_json_rpc_error_JSON_t);
    if (!l_json) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_json->obj_msg = NULL;
    l_json->obj_code = NULL;
    return l_json;
}
void dap_json_rpc_error_JSON_free(dap_json_rpc_error_JSON_t *a_error_json)
{
//    json_object_put(a_error_json->obj_code);
//    json_object_put(a_error_json->obj_msg);
    DAP_FREE(a_error_json);
}
dap_json_rpc_error_JSON_t * dap_json_rpc_error_JSON_add_data(int code, const char *msg)
{
    dap_json_rpc_error_JSON_t *l_json_err = dap_json_rpc_error_JSON_create();
    if (!l_json_err) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_json_err->obj_code = json_object_new_int(code);
    l_json_err->obj_msg = json_object_new_string(msg);
    return l_json_err;
}

int dap_json_rpc_error_add(int a_code_error, const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    const char *a_msg = dap_strdup_vprintf(msg, args); //*str_reply = dap_strdup(str);
    va_end(args);

    dap_json_rpc_error_t *l_el_search =dap_json_rpc_error_search_by_code(a_code_error);
    if (l_el_search != NULL)
        return 1;
    dap_json_rpc_error_t *l_error = DAP_NEW(dap_json_rpc_error_t);
    if(!l_error) {
        log_it(L_CRITICAL, "Memory allocation error");
        return 2;
    }
    l_error->code_error = a_code_error;
    l_error->msg = dap_strdup(a_msg);
    LL_APPEND(s_errors, l_error);
    log_it(L_ERROR, "Registration type error. Code error: %d message: %s", a_code_error, a_msg);
    return 0;
}

json_object * dap_json_rpc_error_get(){
    json_object* json_arr_errors = json_object_new_array();
    dap_json_rpc_error_t * error = NULL;
    LL_FOREACH(s_errors, error) {
        json_object_array_add(json_arr_errors, dap_json_rpc_error_get_json(error));
    }
    if (json_object_array_length(json_arr_errors) > 0) {
        return json_arr_errors;
    } else {
        json_object_put(json_arr_errors);
        return NULL;
    }
}

int _dap_json_rpc_error_cmp_by_code(dap_json_rpc_error_t *a_error, int a_code_error)
{
    if (a_error->code_error == a_code_error)
        return 0;
    else if (a_error->code_error < a_code_error)
        return -1;
    else
        return 1;
}


dap_json_rpc_error_t *dap_json_rpc_error_search_by_code(int a_code_error)
{
    dap_json_rpc_error_t *l_element = NULL;
    LL_SEARCH(s_errors, l_element, a_code_error, _dap_json_rpc_error_cmp_by_code);
    return l_element;
}

json_object *dap_json_rpc_error_get_json(dap_json_rpc_error_t *a_error)
{
    json_object *l_jobj_code = json_object_new_int64(a_error->code_error);
    json_object *l_jobj_msg = json_object_new_string(a_error->msg);
    json_object *l_jobj = json_object_new_object();
    json_object_object_add(l_jobj, "code", l_jobj_code);
    json_object_object_add(l_jobj, "message", l_jobj_msg);
    json_object *l_jobj_err = json_object_new_object();
    json_object_object_add(l_jobj_err, "error", l_jobj);
    return l_jobj_err;
}

char *dap_json_rpc_error_get_json_str(dap_json_rpc_error_t *a_error)
{
    log_it(L_NOTICE, "Translation JSON string to struct dap_json_rpc_error");
    json_object *l_jobj_code = json_object_new_int64(a_error->code_error);
    json_object *l_jobj_msg = json_object_new_string(a_error->msg);
    json_object *l_jobj = json_object_new_object();
    json_object_object_add(l_jobj, "code", l_jobj_code);
    json_object_object_add(l_jobj, "message", l_jobj_msg);
    json_object *l_jobj_err = json_object_new_object();
    json_object_object_add(l_jobj_err, "error", l_jobj);
    char *l_json_str = dap_strdup(json_object_to_json_string(l_jobj_err));
    json_object_put(l_jobj);
    return l_json_str;
}

dap_json_rpc_error_t *dap_json_rpc_create_from_json(const char *a_json)
{
    log_it(L_NOTICE, "Translation struct dap_json_rpc_error to JSON string");
    json_object *l_jobj = json_tokener_parse(a_json);
    dap_json_rpc_error_t *l_error = dap_json_rpc_create_from_json_object(l_jobj);
    json_object_put(l_jobj);
    return l_error;
}

void dap_json_rpc_add_standart_erros(void)
{
    log_it(L_DEBUG, "Registration standart type erros");
    dap_json_rpc_error_add(0, "Unknown error");
    dap_json_rpc_error_add(1, "Not found handler for this request");
}

dap_json_rpc_error_t *dap_json_rpc_create_from_json_object(json_object *a_jobj)
{
    dap_json_rpc_error_t *l_error = DAP_NEW(dap_json_rpc_error_t);
    if (!l_error) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    json_object *l_jobj_code_eror = json_object_object_get(a_jobj, "code");
    json_object *l_jobj_msg = json_object_object_get(a_jobj, "message");
    l_error->code_error = json_object_get_int64(l_jobj_code_eror);
    l_error->msg = dap_strdup(json_object_get_string(l_jobj_msg));
    json_object_put(l_jobj_code_eror);
    json_object_put(l_jobj_msg);
    return l_error;
}
