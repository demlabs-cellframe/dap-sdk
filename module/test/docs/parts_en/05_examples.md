## 4. Complete Examples

### 4.1 State Machine Test (Real Project Example)

Example from `cellframe-srv-vpn-client/tests/unit/test_vpn_state_handlers.c`:

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "vpn_state_machine.h"
#include "vpn_state_handlers_internal.h"

#define LOG_TAG "test_vpn_state_handlers"

// Declare mocks with simple configuration (RECOMMENDED)
DAP_MOCK(dap_net_tun_deinit);
DAP_MOCK(dap_chain_node_client_close_mt);
DAP_MOCK(vpn_wallet_close);

// Mock with return value configuration
DAP_MOCK_DECLARE(dap_chain_node_client_connect_mt, {
    .return_value.l = 0xDEADBEEF
});
// Note: For simple return values, you can also use:
// DAP_MOCK(dap_chain_node_client_connect_mt, 0xDEADBEEF);

static vpn_sm_t *s_test_sm = NULL;

static void setup_test(void) {
    // Note: dap_mock_init() auto-called, not needed here
    s_test_sm = vpn_sm_init();
    assert(s_test_sm != NULL);
}

static void teardown_test(void) {
    if (s_test_sm) {
        vpn_sm_deinit(s_test_sm);
        s_test_sm = NULL;
    }
    // Optional: dap_mock_deinit() to reset mocks between tests
}

void test_state_disconnected_cleanup(void) {
    log_it(L_INFO, "TEST: state_disconnected_entry() cleanup");
    
    setup_test();
    
    // Setup state with resources
    s_test_sm->tun_handle = (void*)0x12345678;
    s_test_sm->wallet = (void*)0xABCDEF00;
    s_test_sm->node_client = (void*)0x22222222;
    
    // Enable mocks
    DAP_MOCK_ENABLE(dap_net_tun_deinit);
    DAP_MOCK_ENABLE(vpn_wallet_close);
    DAP_MOCK_ENABLE(dap_chain_node_client_close_mt);
    
    // Call state handler
    state_disconnected_entry(s_test_sm);
    
    // Verify cleanup was performed
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_deinit) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(vpn_wallet_close) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_chain_node_client_close_mt) == 1);
    
    teardown_test();
    log_it(L_INFO, "[+] PASS");
}

int main() {
    dap_common_init("test_vpn_state_handlers", NULL);
    
    test_state_disconnected_cleanup();
    
    log_it(L_INFO, "All tests PASSED [OK]");
    dap_common_deinit();
    return 0;
}
```

### 4.2 Mock with Callback

```c
#include "dap_mock.h"

DAP_MOCK_DECLARE(dap_hash_fast, {.return_value.i = 0}, {
    if (a_arg_count >= 2) {
        uint8_t *data = (uint8_t*)a_args[0];
        size_t size = (size_t)a_args[1];
        uint32_t hash = 0;
        for (size_t i = 0; i < size; i++) {
            hash += data[i];
        }
        return (void*)(intptr_t)hash;
    }
    return (void*)0;
});

void test_hash() {
    uint8_t data[] = {1, 2, 3};
    uint32_t hash = dap_hash_fast(data, 3);
    assert(hash == 6);  // Callback sums bytes
}
```

### 4.3 Mock with Execution Delays

Example from `dap-sdk/net/client/test/test_http_client_mocks.h`:

```c
#include "dap_mock.h"

// Mock with variance delay: simulates realistic network jitter
// 100ms ± 50ms = range of 50-150ms
#define HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY ((dap_mock_config_t){ \
    .enabled = true, \
    .delay = { \
        .type = DAP_MOCK_DELAY_VARIANCE, \
        .variance = { \
            .center_us = 100000,   /* 100ms center */ \
            .variance_us = 50000   /* ±50ms variance */ \
        } \
    } \
})

// Mock with simulated network latency (using DAP_MOCK_CUSTOM)
// DAP_MOCK_CUSTOM combines declaration and wrapper implementation - no need to write DAP_MOCK_DECLARE_CUSTOM separately!
DAP_MOCK_CUSTOM(dap_client_http_t*, dap_client_http_request_full,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers),
    PARAM(bool, a_follow_redirects)
) {
    // Mock logic - delay is automatically executed by framework
    // Configure delay via control macros if needed:
    // DAP_MOCK_SET_DELAY_VARIANCE_MS(dap_client_http_request_full, 100, 50);  // 100ms ± 50ms
    // G_MOCK is automatically available inside wrapper and points to g_mock_dap_client_http_request_full
    if (g_mock_http_response.should_fail && a_error_callback) {
        a_error_callback(g_mock_http_response.error_code, a_callbacks_arg);
    } else if (a_response_callback) {
        a_response_callback(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            g_mock_http_response.headers,
            a_callbacks_arg,
            g_mock_http_response.status_code
        );
    }
    return (dap_client_http_t*)G_MOCK->return_value.ptr;
}

// Mock without delay for cleanup operations (instant execution)
DAP_MOCK_CUSTOM(void, dap_client_http_close_unsafe,
    PARAM(dap_client_http_t*, a_client_http)
) {
    // Mock close - just free the fake client object
    if (a_client_http) {
        free(a_client_http);
    }
}
```

### 4.4 Custom Mock with Full Control (Advanced)

Example from `test_http_client_mocks.c` using `DAP_MOCK_CUSTOM`:

```c
#include "dap_mock.h"
#include "dap_client_http.h"

// DAP_MOCK_CUSTOM combines declaration and wrapper implementation
// No need to write DAP_MOCK_DECLARE_CUSTOM separately!
DAP_MOCK_CUSTOM(void, dap_client_http_request_async,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(dap_client_http_callback_started_t, a_started_callback),
    PARAM(dap_client_http_callback_progress_t, a_progress_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers),
    PARAM(bool, a_follow_redirects)
) {
    // Custom mock logic - simulate async HTTP behavior
    // DAP_MOCK_CUSTOM automatically:
    // - Registers mock with framework
    // - Generates __wrap_dap_client_http_request_async function signature
    // - Executes configured delay (HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)
    // - Records call
    
    // Call started callback immediately
    if (a_started_callback) {
        a_started_callback(a_callbacks_arg);
    }
    
    // Simulate async callback in separate thread
    typedef struct {
        dap_client_http_callback_full_t response_cb;
        dap_client_http_callback_error_t error_cb;
        void *cb_arg;
    } mock_async_context_t;
    
    mock_async_context_t *l_ctx = malloc(sizeof(mock_async_context_t));
    l_ctx->response_cb = a_response_callback;
    l_ctx->error_cb = a_error_callback;
    l_ctx->cb_arg = a_callbacks_arg;
    
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, mock_async_callback_thread, l_ctx);
    pthread_detach(l_thread);
}
```

**CMakeLists.txt:**
```cmake
# Include auto-wrap helper
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_http_client 
    test_http_client_mocks.c 
    test_http_client_mocks.h
    test_main.c
)

target_link_libraries(test_http_client
    dap_test     # Test framework with mocks
    dap_core     # DAP core library
    pthread      # Threading support
)

# Auto-generate --wrap linker flags by scanning all sources
dap_mock_autowrap(test_http_client)
```

### 4.5 Dynamic Mock Behavior

```c
// Mock that changes behavior based on call count
// Simulates flaky network: fails first 2 times, then succeeds
DAP_MOCK_DECLARE(flaky_network_send, {.return_value.i = 0}, {
    int call_count = DAP_MOCK_GET_CALL_COUNT(flaky_network_send);
    
    // Fail first 2 calls (simulate network issues)
    if (call_count < 2) {
        log_it(L_DEBUG, "Simulating network failure (attempt %d)", call_count + 1);
        return (void*)(intptr_t)-1;  // Error code
    }
    
    // Succeed on 3rd and subsequent calls
    log_it(L_DEBUG, "Network call succeeded");
    return (void*)(intptr_t)0;  // Success code
});

void test_retry_logic() {
    // Test function that retries on failure
    int result = send_with_retry(data, 3);  // Max 3 retries
    
    // Should succeed on 3rd attempt
    assert(result == 0);
    assert(DAP_MOCK_GET_CALL_COUNT(flaky_network_send) == 3);
    
    log_it(L_INFO, "[+] Retry logic works correctly");
}
```

### 4.6 Mocking Functions in Static Libraries

Example test that mocks functions inside static library `dap_stream`:

**CMakeLists.txt:**
```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_stream_mocks
    test_stream_mocks.c
    test_stream_mocks_wrappers.c
)

target_link_libraries(test_stream_mocks
    dap_test
    dap_stream       # Static library - need to mock functions inside
    dap_net
    dap_core
    pthread
)

target_include_directories(test_stream_mocks PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework
    ${CMAKE_SOURCE_DIR}/dap-sdk/core/include
)

# Step 1: Auto-generate --wrap flags from test sources
dap_mock_autowrap(test_stream_mocks)

# Step 2: Wrap static library with --whole-archive
# This forces linker to include all symbols from dap_stream,
# including internal functions that need to be mocked
dap_mock_autowrap_with_static(test_stream_mocks dap_stream)
```

**test_stream_mocks.c:**
```c
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_stream.h"
#include "dap_common.h"
#include <assert.h>

#define LOG_TAG "test_stream_mocks"

// Mock function used inside dap_stream (using DAP_MOCK_CUSTOM)
// Combines declaration and wrapper - no need to write separately!
DAP_MOCK_CUSTOM(int, dap_net_tun_write,
    PARAM(int, a_fd),
    PARAM(const void*, a_buf),
    PARAM(size_t, a_len)
) {
    // Mock logic - simulate successful write
    log_it(L_DEBUG, "Mock: dap_net_tun_write called (fd=%d, len=%zu)", a_fd, a_len);
    
    // Configure delay and return value at runtime if needed:
    // DAP_MOCK_SET_DELAY_MS(dap_net_tun_write, 10);
    // DAP_MOCK_SET_RETURN(dap_net_tun_write, (void*)(intptr_t)0);
    
    return 0;  // Success
}

void test_stream_write_with_mock(void) {
    log_it(L_INFO, "TEST: Stream write with mocked tun_write");
    
    // Create stream (dap_stream uses dap_net_tun_write internally)
    dap_stream_t *stream = dap_stream_create(...);
    assert(stream != NULL);
    
    // Perform write - should use mocked dap_net_tun_write
    int result = dap_stream_write(stream, "test data", 9);
    
    // Verify mock was called
    assert(result == 0);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_write) > 0);
    
    dap_stream_delete(stream);
    log_it(L_INFO, "[+] Test passed");
}

int main() {
    dap_common_init("test_stream_mocks", NULL);
    
    test_stream_write_with_mock();
    
    dap_common_deinit();
    return 0;
}
```

**Key Points:**
1. `dap_mock_autowrap()` must be called **before** `dap_mock_autowrap_with_static()`
2. Specify all static libraries where functions need to be mocked
3. `--whole-archive` may increase executable size
4. Works only with GCC, Clang, and MinGW

### 4.7 Asynchronous Mock Execution

Example demonstrating async mock callbacks with thread pool:

```c
#include "dap_mock.h"
#include "dap_test_async.h"

// Async mock for HTTP request with 50ms delay
// For async mocks with custom logic, use DAP_MOCK_DECLARE with configuration
// Wrapper is automatically generated - no need to write DAP_MOCK_CUSTOM!
DAP_MOCK_DECLARE(dap_client_http_request, {
    .enabled = true,
    .async = true,  // Execute in worker thread
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 50000  // 50ms realistic network latency
    }
}, {
    // Custom callback logic - executes asynchronously in worker thread
    // This code runs after 50ms delay
    const char *response = "{\"status\":\"ok\",\"data\":\"test\"}";
    if (a_arg_count >= 2 && a_args[1]) {
        http_callback_t callback = (http_callback_t)a_args[1];
        callback(response, 200, a_args[2]);  // a_args[2] is user_data
    }
    return (void*)(intptr_t)0;  // Success
});

static volatile bool s_callback_executed = false;
static volatile int s_http_status = 0;

static void http_response_callback(const char *body, int status, void *arg) {
    s_http_status = status;
    s_callback_executed = true;
    log_it(L_INFO, "HTTP response received: status=%d", status);
}

void test_async_http_request(void) {
    log_it(L_INFO, "TEST: Async HTTP request");
    
   
    s_callback_executed = false;
    s_http_status = 0;
    
    // Call HTTP request - mock will execute asynchronously
    int result = dap_client_http_request(
        "http://test.com/api",
        http_response_callback,
        NULL
    );
    
    assert(result == 0);
    log_it(L_DEBUG, "HTTP request initiated, waiting for callback...");
    
    // Wait for async mock to complete (up to 5 seconds)
    DAP_TEST_WAIT_UNTIL(s_callback_executed, 5000, "HTTP callback");
    
    // Verify
    assert(s_callback_executed);
    assert(s_http_status == 200);
    
    // Alternative: wait for all async mocks
    bool all_completed = dap_mock_async_wait_all(5000);
    assert(all_completed);
    
    log_it(L_INFO, "[+] Async mock test passed");
    
}

// Fast-forward example: test without real delays
void test_async_with_flush(void) {
    
    s_callback_executed = false;
    
    // Schedule async task with long delay
    dap_client_http_request("http://test.com", http_response_callback, NULL);
    
    // Instead of waiting 50ms, execute immediately
    dap_mock_async_flush();  // Fast-forward time
    
    // Callback already executed
    assert(s_callback_executed);
    
    log_it(L_INFO, "[+] Fast-forward test passed");
}
```

**Benefits of Async Mocks:**
- Realistic simulation of network/IO latency
- No need for full `dap_events` infrastructure in unit tests
- Thread-safe execution
- Deterministic testing with `flush()`
- Statistics tracking with `get_pending_count()` / `get_completed_count()`

\newpage
