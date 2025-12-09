/**
 * @file test_dap_client_mocks.c
 * @brief Mock wrapper implementations for dap_client test
 * @details Provides linker wrappers for functions called from static libraries
 */

#include "dap_mock.h"
#include "dap_common.h"
#include "dap_client.h"
#include "dap_client_http.h"  // For dap_client_http_t type
#include "dap_events.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_session.h"  // For dap_stream_session_t type
#include "dap_cert.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_timerfd.h"  // For dap_timerfd_t type
#include "dap_list.h"  // For dap_list_t type
#include "dap_context.h"  // For dap_context_t type
#include "dap_net_trans.h"  // For dap_net_trans_* types
#include <string.h>  // For memcpy
#include <stdio.h>   // For fprintf

#define LOG_TAG "test_dap_client_mocks"

// ============================================================================
// Mock Declarations (must be before wrappers)
// ============================================================================

// Mock dap_events functions
DAP_MOCK_DECLARE(dap_events_init);
DAP_MOCK_DECLARE(dap_events_start);
DAP_MOCK_DECLARE(dap_events_stop_all);
DAP_MOCK_DECLARE(dap_events_deinit);
DAP_MOCK_DECLARE(dap_events_worker_get_auto);

// Mock dap_http_client functions
DAP_MOCK_DECLARE(dap_http_client_init);
DAP_MOCK_DECLARE(dap_http_client_deinit);

// Mock dap_client_http functions
DAP_MOCK_DECLARE(dap_client_http_init);
DAP_MOCK_DECLARE(dap_client_http_deinit);
DAP_MOCK_DECLARE(dap_client_http_request);
DAP_MOCK_DECLARE(dap_client_http_close_unsafe);

// Functions called from dap_client_pvt_* (these are mocked, not dap_client_pvt_* itself)
DAP_MOCK_DECLARE(dap_timerfd_delete_unsafe);
DAP_MOCK_DECLARE(dap_timerfd_start_on_worker);
DAP_MOCK_DECLARE(dap_list_append);
DAP_MOCK_DECLARE(dap_list_free_full);
DAP_MOCK_DECLARE(dap_net_trans_list_all);
DAP_MOCK_DECLARE(dap_net_trans_stage_prepare);
DAP_MOCK_DECLARE(dap_stream_new_es_client);
DAP_MOCK_DECLARE(dap_events_socket_delete_unsafe);
DAP_MOCK_DECLARE(dap_worker_add_events_socket);
DAP_MOCK_DECLARE(dap_context_find);
DAP_MOCK_DECLARE(dap_worker_get_current);
DAP_MOCK_DECLARE(dap_events_socket_remove_and_delete_unsafe);
DAP_MOCK_DECLARE(dap_stream_session_pure_new);
DAP_MOCK_DECLARE(dap_cert_add_sign_to_data);
DAP_MOCK_DECLARE(dap_enc_key_delete);

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);
DAP_MOCK_DECLARE(dap_stream_delete_unsafe);

// Mock dap_stream_trans functions
DAP_MOCK_DECLARE(dap_net_trans_init);
DAP_MOCK_DECLARE(dap_net_trans_deinit);
DAP_MOCK_DECLARE(dap_net_trans_find);
DAP_MOCK_DECLARE(dap_net_trans_register);

// Mock dap_stream_ch functions
DAP_MOCK_DECLARE(dap_stream_ch_by_id_unsafe);
DAP_MOCK_DECLARE(dap_stream_ch_pkt_write_unsafe);

// Mock dap_worker functions
DAP_MOCK_DECLARE(dap_worker_exec_callback_on);

// Mock dap_cert functions
DAP_MOCK_DECLARE(dap_cert_find_by_name);

// Mock dap_enc functions (used by dap_client_request_enc_unsafe)
DAP_MOCK_DECLARE(dap_enc_code_out_size);
DAP_MOCK_DECLARE(dap_enc_code);
DAP_MOCK_DECLARE(dap_enc_key_new_generate);

// ============================================================================
// Mock Wrappers for Functions Called from Static Libraries
// ============================================================================

// Mock dap_events functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_events_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_events_start, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_events_stop_all, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_events_deinit, (), ());

// Custom wrapper for dap_events_worker_get_auto - return mock value if enabled
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_events_worker_get_auto, void)
{
    // When mock is enabled, return configured mock value
    // dap_mock_prepare_call already executed delay and recorded the call
    return (dap_worker_t*)g_mock_dap_events_worker_get_auto->return_value.ptr;
}

// Mock dap_http_client functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_http_client_init, (), ());

// Custom wrapper for dap_http_client_deinit - record calls when enabled
DAP_MOCK_WRAPPER_CUSTOM(void, dap_http_client_deinit, void)
{
    // When mock is enabled, intercept the call and don't call original
    // dap_mock_prepare_call already executed delay and recorded the call
    // Mock intercepted - don't call original (return immediately)
}

// Mock dap_client_http functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_http_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_http_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_client_http_t*, dap_client_http_request, (dap_worker_t *a_worker, const char *a_addr, uint16_t a_port, const char *a_method, const char *a_content_type, const char *a_path, void *a_request, size_t a_request_size, void *a_header, dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error, void *a_obj, void *a_header_add), (a_worker, a_addr, a_port, a_method, a_content_type, a_path, a_request, a_request_size, a_header, a_response_proc, a_response_error, a_obj, a_header_add));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_http_close_unsafe, (dap_client_http_t *a_client_http), (a_client_http));

// Functions called from dap_client_pvt_* (these are mocked, not dap_client_pvt_* itself)
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_timerfd_delete_unsafe, (dap_timerfd_t *a_timerfd), (a_timerfd));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_timerfd_t*, dap_timerfd_start_on_worker, (dap_worker_t *a_worker, unsigned long a_timeout_ms, dap_timerfd_callback_t a_callback, void *a_arg), (a_worker, a_timeout_ms, a_callback, a_arg));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_list_t*, dap_list_append, (dap_list_t *a_list, void *a_data), (a_list, a_data));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_list_free_full, (dap_list_t *a_list, void (*a_free_func)(void*)), (a_list, a_free_func));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_list_t*, dap_net_trans_list_all, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_net_trans_stage_prepare, (dap_net_trans_type_t a_trans_type, const dap_net_stage_prepare_params_t *a_params, dap_net_stage_prepare_result_t *a_result), (a_trans_type, a_params, a_result));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_stream_t*, dap_stream_new_es_client, (dap_events_socket_t *a_es, dap_stream_node_addr_t *a_node_addr, bool a_authorized), (a_es, a_node_addr, a_authorized));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_events_socket_delete_unsafe, (dap_events_socket_t *a_es, bool a_now), (a_es, a_now));
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_worker_add_events_socket, (dap_worker_t *a_worker, dap_events_socket_t *a_es), (a_worker, a_es));
DAP_MOCK_WRAPPER_PASSTHROUGH(void*, dap_context_find, (dap_context_t *a_context, dap_events_socket_uuid_t a_uuid), (a_context, a_uuid));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_worker_t*, dap_worker_get_current, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_events_socket_remove_and_delete_unsafe, (dap_events_socket_t *a_es, bool a_now), (a_es, a_now));
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_stream_session_t*, dap_stream_session_pure_new, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH(size_t, dap_cert_add_sign_to_data, (dap_cert_t *a_cert, uint8_t **a_data, size_t *a_data_size, const void *a_data_to_sign, size_t a_data_to_sign_size), (a_cert, a_data, a_data_size, a_data_to_sign, a_data_to_sign_size));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_enc_key_delete, (dap_enc_key_t *a_key), (a_key));

// Mock dap_stream functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_stream_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_stream_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_stream_delete_unsafe, (dap_stream_t *a_stream), (a_stream));

// Mock dap_net_trans functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_net_trans_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_net_trans_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_net_trans_t*, dap_net_trans_find, (dap_net_trans_type_t a_type), (a_type));
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_net_trans_register, (const char *a_name, dap_net_trans_type_t a_type, const dap_net_trans_ops_t *a_ops, void *a_inheritor), (a_name, a_type, a_ops, a_inheritor));

// Mock dap_stream_ch functions
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_stream_ch_t*, dap_stream_ch_by_id_unsafe, (dap_stream_t *a_stream, uint8_t a_ch_id), (a_stream, a_ch_id));
DAP_MOCK_WRAPPER_PASSTHROUGH(ssize_t, dap_stream_ch_pkt_write_unsafe, (dap_stream_ch_t *a_ch, uint8_t a_type, void *a_data, size_t a_data_size), (a_ch, a_type, a_data, a_data_size));

// Mock dap_worker functions
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_worker_exec_callback_on, (dap_worker_t *a_worker, dap_worker_callback_t a_callback, void *a_arg), (a_worker, a_callback, a_arg));

// Mock dap_cert functions
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_cert_t*, dap_cert_find_by_name, (const char *a_name), (a_name));

// Mock dap_enc functions - custom wrappers that return mock values when enabled
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_enc_code_out_size,
    PARAM(dap_enc_key_t*, a_key),
    PARAM(size_t, a_buf_in_size),
    PARAM(dap_enc_data_type_t, type)
)
{
    // Return mock value: for base64 encoding, add ~33% overhead, for raw return input size
    size_t l_result = (type == DAP_ENC_DATA_TYPE_RAW) ? a_buf_in_size : (a_buf_in_size * 4 / 3 + 100);
    // Use configured return value if set, otherwise use calculated result
    return (size_t)(intptr_t)(g_mock_dap_enc_code_out_size->return_value.ptr ?: (void*)(intptr_t)l_result);
}

DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_enc_code,
    PARAM(dap_enc_key_t*, a_key),
    PARAM(const void*, a_buf_in),
    PARAM(size_t, a_buf_in_size),
    PARAM(void*, a_buf_out),
    PARAM(size_t, a_buf_out_size_max),
    PARAM(dap_enc_data_type_t, a_data_type_out)
)
{
    // Mock: just copy input to output for testing
    if (a_buf_out && a_buf_in && a_buf_out_size_max >= a_buf_in_size) {
        memcpy(a_buf_out, a_buf_in, a_buf_in_size);
        return a_buf_in_size;
    }
    return 0;
}

DAP_MOCK_WRAPPER_CUSTOM(dap_enc_key_t*, dap_enc_key_new_generate,
    PARAM(dap_enc_key_type_t, a_key_type),
    PARAM(const void*, a_kex_buf),
    PARAM(size_t, a_kex_size),
    PARAM(const void*, a_seed),
    PARAM(size_t, a_seed_size),
    PARAM(size_t, a_key_size)
)
{
    // Return configured mock value
    return (dap_enc_key_t*)g_mock_dap_enc_key_new_generate->return_value.ptr;
}

