# DAP SDK Asynchronous Testing Framework

## Overview

The DAP SDK Async Testing Framework provides utilities for testing asynchronous operations with timeouts, condition polling, and thread-safe synchronization.

## Features

- **Global Test Timeout**: Set timeout for entire test suite using alarm-based mechanism
- **Condition Polling**: Wait for conditions with configurable timeout and poll interval
- **pthread Condition Variable Helpers**: Simplified wrappers for pthread_cond_timedwait
- **Cross-platform Utilities**: Monotonic time, sleep functions

## API Components

### 1. Global Test Timeout (Alarm-Based)

Limits execution time of entire test suite using SIGALRM:

```c
int main(int argc, char **argv) {
    dap_common_init("my_test", NULL);
    
    dap_test_global_timeout_t l_timeout;
    if (dap_test_set_global_timeout(&l_timeout, 30, "My Test Suite")) {
        // Timeout triggered - test took > 30 seconds
        log_it(L_CRITICAL, "Test timeout!");
        dap_common_deinit();
        return 1;
    }
    
    // Run tests
    run_all_my_tests();
    
    // Cancel timeout
    dap_test_cancel_global_timeout();
    
    dap_common_deinit();
    return 0;
}
```

### 2. Condition Polling

Wait for a condition to be met with periodic polling:

```c
// Callback to check condition
bool check_state_connected(void *a_data) {
    vpn_sm_t *l_sm = (vpn_sm_t *)a_data;
    return vpn_sm_get_state(l_sm) == VPN_STATE_CONNECTED;
}

// Configure and wait
dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
l_cfg.timeout_ms = 10000;  // 10 sec timeout
l_cfg.poll_interval_ms = 100;  // Poll every 100ms
l_cfg.operation_name = "VPN connection";
l_cfg.fail_on_timeout = true;  // abort() on timeout

bool l_result = dap_test_wait_condition(check_state_connected, l_sm, &l_cfg);
dap_assert(l_result, "VPN should connect within 10 sec");
```

### 3. Pthread Condition Variable Helpers

Simplified API for pthread condition variables with timeout:

```c
// Async completion callback
void my_completion_callback(void *a_arg) {
    dap_test_cond_wait_ctx_t *l_ctx = (dap_test_cond_wait_ctx_t *)a_arg;
    // ... do work ...
    dap_test_cond_signal(l_ctx);  // Signal completion
}

// Test function
void test_async_operation() {
    dap_test_cond_wait_ctx_t l_ctx;
    dap_test_cond_wait_init(&l_ctx);
    
    // Start async operation
    start_async_operation(&l_ctx, my_completion_callback);
    
    // Wait with timeout
    bool l_success = dap_test_cond_wait(&l_ctx, 5000);  // 5 sec
    dap_assert(l_success, "Operation should complete");
    
    dap_test_cond_wait_deinit(&l_ctx);
}
```

### 4. Convenience Macros

Quick condition waiting without explicit callbacks:

```c
vpn_sm_t *l_sm = vpn_sm_init();
vpn_sm_transition(l_sm, VPN_EVENT_USER_CONNECT);

// Wait for state transition (10 sec timeout, 100ms poll)
DAP_TEST_WAIT_UNTIL(
    vpn_sm_get_state(l_sm) == VPN_STATE_CONNECTED,
    10000,
    "VPN should reach CONNECTED state"
);
```

## Configuration

### Default Config

```c
dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
// Equivalent to:
// {
//     .timeout_ms = 5000,
//     .poll_interval_ms = 100,
//     .fail_on_timeout = true,
//     .operation_name = "async operation"
// }
```

### Custom Config

```c
dap_test_async_config_t cfg = {
    .timeout_ms = 30000,       // 30 sec
    .poll_interval_ms = 200,   // Poll every 200ms
    .fail_on_timeout = false,  // Don't abort, just return false
    .operation_name = "Long operation"
};
```

## Integration

### CMakeLists.txt

The framework is automatically built as part of `dap_test` library:

```cmake
target_link_libraries(my_test
    dap_test        # Includes async utilities
    dap_core
    pthread
)
```

### Header Includes

```c
#include "dap_test.h"        // Base test macros
#include "dap_test_async.h"  // Async utilities
```

## Best Practices

1. **Always use global timeout for test suites** to prevent infinite hangs
2. **Choose appropriate poll intervals** (too short = CPU waste, too long = slow tests)
3. **Use descriptive operation names** for better debugging
4. **Clean up resources** (deinit contexts) even on timeout
5. **Don't mix alarm-based and timer-based timeouts** in same test

## Example: State Machine Test

```c
#include "dap_test.h"
#include "dap_test_async.h"

#define TEST_TIMEOUT_SEC 30

int main(int argc, char **argv) {
    dap_common_init("test_state_machine", NULL);
    
    // Set global timeout
    dap_test_global_timeout_t l_timeout;
    if (dap_test_set_global_timeout(&l_timeout, TEST_TIMEOUT_SEC, "State Machine Tests")) {
        log_it(L_CRITICAL, "Test suite timeout!");
        dap_common_deinit();
        return 1;
    }
    
    log_it(L_INFO, "Running state machine tests (timeout: %d sec)", TEST_TIMEOUT_SEC);
    
    // Test 1: Simple transition
    test_simple_transition();
    
    // Test 2: Async transition with condition wait
    test_async_transition();
    
    // Test 3: Complex multi-step flow
    test_complex_flow();
    
    dap_test_cancel_global_timeout();
    log_it(L_INFO, "All tests passed!");
    
    dap_common_deinit();
    return 0;
}

void test_async_transition() {
    log_it(L_INFO, "Test: Async transition");
    
    vpn_sm_t *l_sm = vpn_sm_init();
    vpn_sm_transition(l_sm, VPN_EVENT_USER_CONNECT);
    
    // Wait for CONNECTED state with 10 sec timeout
    dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    l_cfg.timeout_ms = 10000;
    l_cfg.operation_name = "VPN connection";
    
    bool check_connected(void *a_data) {
        return vpn_sm_get_state((vpn_sm_t*)a_data) == VPN_STATE_CONNECTED;
    }
    
    bool l_result = dap_test_wait_condition(check_connected, l_sm, &l_cfg);
    dap_assert(l_result, "Should connect within 10 sec");
    
    vpn_sm_deinit(l_sm);
    log_it(L_INFO, "✓ Async transition test passed");
}
```

## Thread Safety

- All functions are thread-safe
- Condition variable helpers use proper locking
- Global timeout uses signal-safe operations

## Platform Support

- **Linux**: Full support
- **macOS**: Full support  
- **Windows**: Partial (no alarm-based timeout, use condition variables instead)

## See Also

- `dap_test.h` - Base testing macros
- `dap_mock_framework.h` - Mock framework for unit tests
- VPN Client test suite examples in `tests/unit/`

