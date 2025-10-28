## 4. Complete Examples

### 4.1 State Machine Test (Real Project Example)

Example from `cellframe-srv-vpn-client/tests/unit/test_vpn_state_handlers.c`:

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "vpn_state_machine.h"
#include "vpn_state_handlers_internal.h"

#define LOG_TAG "test_vpn_state_handlers"

// Declare mocks with simple configuration
DAP_MOCK_DECLARE(dap_net_tun_deinit);
DAP_MOCK_DECLARE(dap_chain_node_client_close_mt);
DAP_MOCK_DECLARE(vpn_wallet_close);

// Mock with return value configuration
DAP_MOCK_DECLARE(dap_chain_node_client_connect_mt, {
    .return_value.l = 0xDEADBEEF
});

static vpn_sm_t *s_test_sm = NULL;

static void setup_test(void) {
    dap_mock_init();
    s_test_sm = vpn_sm_init();
    assert(s_test_sm != NULL);
}

static void teardown_test(void) {
    if (s_test_sm) {
        vpn_sm_deinit(s_test_sm);
        s_test_sm = NULL;
    }
    dap_mock_deinit();
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

// Declare mock with simulated network latency
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_full, 
                        HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY);

// Mock without delay for cleanup operations (instant execution)
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_close_unsafe, {
    .enabled = true,
    .delay = {.type = DAP_MOCK_DELAY_NONE}
});
```

### 4.4 Custom Linker Wrapper (Advanced)

Example from `test_http_client_mocks.c` using `DAP_MOCK_WRAPPER_CUSTOM`:

```c
#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"
#include "dap_client_http.h"

// Declare mock (registers with framework)
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_async, 
                        HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY);

// Custom wrapper implementation with full control
// DAP_MOCK_WRAPPER_CUSTOM generates:
// - __wrap_dap_client_http_request_async function signature
// - void* args array for mock framework
// - Automatic delay execution
// - Call recording
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request_async,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_path),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg)
) {
    // Custom mock logic - simulate async HTTP behavior
    // This directly invokes callbacks based on mock configuration
    
    if (g_mock_http_response.should_fail && a_error_callback) {
        // Simulate error response
        a_error_callback(g_mock_http_response.error_code, a_callbacks_arg);
    } else if (a_response_callback) {
        // Simulate successful response with configured data
        a_response_callback(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            g_mock_http_response.headers,
            a_callbacks_arg,
            g_mock_http_response.status_code
        );
    }
    // Note: Configured delay is executed automatically before this code
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

### 4.6 Asynchronous Mock Execution

Example demonstrating async mock callbacks with thread pool:

```c
#include "dap_mock.h"
#include "dap_mock_async.h"
#include "dap_test_async.h"

// Async mock for HTTP request with 50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request, {
    .enabled = true,
    .async = true,  // Execute in worker thread
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 50000  // 50ms realistic network latency
    }
});

// Mock wrapper - executes asynchronously
DAP_MOCK_WRAPPER_CUSTOM(int, dap_client_http_request,
    PARAM(const char*, a_url),
    PARAM(http_callback_t, a_callback),
    PARAM(void*, a_arg)
) {
    // This code runs in worker thread after 50ms delay
    const char *response = "{\"status\":\"ok\",\"data\":\"test\"}";
    a_callback(response, 200, a_arg);
    return 0;
}

static volatile bool s_callback_executed = false;
static volatile int s_http_status = 0;

static void http_response_callback(const char *body, int status, void *arg) {
    s_http_status = status;
    s_callback_executed = true;
    log_it(L_INFO, "HTTP response received: status=%d", status);
}

void test_async_http_request(void) {
    log_it(L_INFO, "TEST: Async HTTP request");
    
    // Initialize async mock system with 1 worker thread
    dap_mock_async_init(1);
    
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
    
    // Cleanup async system
    dap_mock_async_deinit();
}

// Fast-forward example: test without real delays
void test_async_with_flush(void) {
    dap_mock_async_init(1);
    
    s_callback_executed = false;
    
    // Schedule async task with long delay
    dap_client_http_request("http://test.com", http_response_callback, NULL);
    
    // Instead of waiting 50ms, execute immediately
    dap_mock_async_flush();  // Fast-forward time
    
    // Callback already executed
    assert(s_callback_executed);
    
    log_it(L_INFO, "[+] Fast-forward test passed");
    dap_mock_async_deinit();
}
```

**Benefits of Async Mocks:**
- Realistic simulation of network/IO latency
- No need for full `dap_events` infrastructure in unit tests
- Thread-safe execution
- Deterministic testing with `flush()`
- Statistics tracking with `get_pending_count()` / `get_completed_count()`

\newpage
