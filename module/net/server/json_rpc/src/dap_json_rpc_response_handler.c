#include "dap_json_rpc_response_handler.h"

#define LOG_TAG "dap_json_rpc_response_handler"

static dap_json_rpc_response_handler_t *s_response_handlers = NULL;
static _Atomic(uint64_t) s_delta = 0;

int dap_json_rpc_response_registration_with_id(uint64_t a_id, dap_json_rpc_response_handler_func_t *func)
{
    dap_json_rpc_response_handler_t *l_handler = NULL;
    HASH_FIND_INT(s_response_handlers, &a_id, l_handler);
    if (l_handler)
        return 1;
    l_handler = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_json_rpc_response_handler_t, -1);
    l_handler->id = a_id;
    l_handler->func = func;
    HASH_ADD_INT(s_response_handlers, id, l_handler);
    log_it(L_NOTICE, "Registration handler response with id: %"DAP_UINT64_FORMAT_U, a_id);
    return 0;
}
uint64_t dap_json_rpc_response_registration(dap_json_rpc_response_handler_func_t *func)
{
    return dap_json_rpc_response_registration_with_id(dap_json_rpc_response_get_new_id(), func);
}
void dap_json_rpc_response_unregistration(uint64_t a_id)
{
    dap_json_rpc_response_handler_t *l_handler = NULL;
    HASH_FIND_INT(s_response_handlers, &a_id, l_handler);
    if (l_handler) {
        HASH_DEL(s_response_handlers, l_handler);
        DAP_FREE(l_handler);
        log_it(L_NOTICE, "Unregistration handler response with id: %"DAP_UINT64_FORMAT_U, a_id);
    }
}

void  dap_json_rpc_response_handler(dap_json_rpc_response_t *a_response)
{
    log_it(L_MSG, "Get response");
    switch(a_response->type) {
        case TYPE_RESPONSE_STRING:
            log_it(L_MSG, "response: %s", a_response->result_string);
            break;
        case TYPE_RESPONSE_INTEGER:
            break;
        case TYPE_RESPONSE_DOUBLE:
            break;
        case TYPE_RESPONSE_BOOLEAN:
            break;
        case TYPE_RESPONSE_NULL:
            printf("response type is NULL\n");
            break;
        case TYPE_RESPONSE_JSON:
            log_it(L_MSG, "response: %s", dap_json_to_string(a_response->result_json_object));
            break;
    }
    // dap_json_rpc_response_handler_t *l_handler = NULL;
    // HASH_FIND_INT(s_response_handlers, (void*)a_response->id, l_handler);
    // if (l_handler != NULL){
    //     log_it(L_NOTICE, "Calling handler response id: %"DAP_UINT64_FORMAT_U, a_response->id);
    //     l_handler->func(a_response);
    //     dap_json_rpc_response_unregistration(a_response->id);
    // } else {
    //     log_it(L_NOTICE, "Can't calling handler response id: %"DAP_UINT64_FORMAT_U". This handler not found", a_response->id);
    // }
}

uint64_t dap_json_rpc_response_get_new_id(void)
{
    return ++s_delta;
}

void dap_json_rpc_response_accepted(void *a_data, size_t a_size_data, UNUSED_ARG void *a_obj, http_status_code_t http_status)
{
    if (http_status != Http_Status_OK)
        return log_it(L_ERROR, "Reponse error %d", (int)http_status);
    log_it(L_NOTICE, "Pre handling response");
    dap_json_rpc_response_t *l_response;
    if ( *((char*)a_data + a_size_data) ) {
        char *l_dup = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, a_size_data + 1);
        memcpy(l_dup, a_data, a_size_data);
        l_response = dap_json_rpc_response_from_string(l_dup);
        DAP_DELETE(l_dup);
    } else
        l_response = dap_json_rpc_response_from_string((char*)a_data);
    dap_json_rpc_response_handler(l_response);
    dap_json_rpc_response_free(l_response);
}
