/**
 * @file test_dap_client_mocks.c
 * @brief Mock wrapper implementations for dap_client test
 * @details Provides linker wrappers for functions called from static libraries
 */

#include "dap_mock.h"
#include "dap_common.h"
#include "dap_client.h"
#include "dap_client_pvt.h"  // For dap_client_pvt_t type
#include "dap_events.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_cert.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
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

// Mock dap_client_pvt functions
DAP_MOCK_DECLARE(dap_client_pvt_init);
DAP_MOCK_DECLARE(dap_client_pvt_deinit);
DAP_MOCK_DECLARE(dap_client_pvt_new);
DAP_MOCK_DECLARE(dap_client_pvt_delete_unsafe);
DAP_MOCK_DECLARE(dap_client_pvt_queue_add);
DAP_MOCK_DECLARE(dap_client_pvt_queue_clear);
DAP_MOCK_DECLARE(dap_client_pvt_stage_transaction_begin);
DAP_MOCK_DECLARE(dap_client_pvt_request);
DAP_MOCK_DECLARE(dap_client_pvt_request_enc);

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);
DAP_MOCK_DECLARE(dap_stream_delete_unsafe);

// Mock dap_stream_transport functions
DAP_MOCK_DECLARE(dap_stream_transport_init);
DAP_MOCK_DECLARE(dap_stream_transport_deinit);
DAP_MOCK_DECLARE(dap_stream_transport_find);
DAP_MOCK_DECLARE(dap_stream_transport_register);

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
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_events_worker_get_auto)
{
    // When mock is enabled, return configured mock value
    // dap_mock_prepare_call already executed delay and recorded the call
    return (dap_worker_t*)g_mock_dap_events_worker_get_auto->return_value.ptr;
}

// Mock dap_http_client functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_http_client_init, (), ());

// Custom wrapper for dap_http_client_deinit - record calls when enabled
DAP_MOCK_WRAPPER_CUSTOM(void, dap_http_client_deinit)
{
    // When mock is enabled, intercept the call and don't call original
    // dap_mock_prepare_call already executed delay and recorded the call
    // Mock intercepted - don't call original (return immediately)
}

// Mock dap_client_http functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_http_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_http_deinit, (), ());

// Mock dap_client_pvt functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_pvt_init, (), ());

// Custom wrapper for dap_client_pvt_deinit - record calls when enabled
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_pvt_deinit)
{
    // When mock is enabled, intercept the call and don't call original
    // dap_mock_prepare_call already executed delay and recorded the call
    // Mock intercepted - don't call original (return immediately)
}
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_pvt_new, (dap_client_pvt_t *a_client_pvt), (a_client_pvt));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_pvt_delete_unsafe, (dap_client_pvt_t *a_client_pvt), (a_client_pvt));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_pvt_queue_add, (dap_client_pvt_t *a_client_pvt, char a_ch_id, uint8_t a_type, void *a_data, size_t a_data_size), (a_client_pvt, a_ch_id, a_type, a_data, a_data_size));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_pvt_queue_clear, (dap_client_pvt_t *a_client_pvt), (a_client_pvt));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_client_pvt_stage_transaction_begin, (dap_client_pvt_t *a_client_pvt, dap_client_stage_t a_stage_next, dap_client_callback_t a_done_callback), (a_client_pvt, a_stage_next, a_done_callback));
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_pvt_request, (dap_client_pvt_t *a_client_pvt, const char *a_path, void *a_request, size_t a_request_size, dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error), (a_client_pvt, a_path, a_request, a_request_size, a_response_proc, a_response_error));
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_client_pvt_request_enc, (dap_client_pvt_t *a_client_pvt, const char *a_path, const char *a_suburl, const char *a_query, void *a_request, size_t a_request_size, dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error), (a_client_pvt, a_path, a_suburl, a_query, a_request, a_request_size, a_response_proc, a_response_error));

// Mock dap_stream functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_stream_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_stream_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_stream_delete_unsafe, (dap_stream_t *a_stream), (a_stream));

// Mock dap_stream_transport functions
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_stream_transport_init, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_stream_transport_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH(dap_stream_transport_t*, dap_stream_transport_find, (dap_stream_transport_type_t a_type), (a_type));
DAP_MOCK_WRAPPER_PASSTHROUGH(int, dap_stream_transport_register, (const char *a_name, dap_stream_transport_type_t a_type, const dap_stream_transport_ops_t *a_ops, void *a_inheritor), (a_name, a_type, a_ops, a_inheritor));

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

