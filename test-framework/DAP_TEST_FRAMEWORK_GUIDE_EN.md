# DAP SDK Test Framework - Complete Guide

**Version:** 1.0.0  
**Date:** 2025-10-27  
**Language:** English

---

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Quick Start](#quick-start)
4. [Tutorial](#tutorial)
5. [API Reference](#api-reference)
6. [Best Practices](#best-practices)
7. [Troubleshooting](#troubleshooting)
8. [Glossary](#glossary)

---

## Overview

The DAP SDK Test Framework provides a comprehensive infrastructure for testing asynchronous operations, mocking dependencies, and ensuring reliable test execution across the Cellframe ecosystem.

### Key Components

1. **Async Testing Framework** (`dap_test_async.h/c`)
   - Global test timeout (prevent CI/CD hangs)
   - Condition polling with configurable intervals
   - pthread condition variable helpers
   - Cross-platform time utilities

2. **Mock Framework V4** (`dap_mock_framework.h/c`)
   - Linker-based function mocking (`--wrap`)
   - Type-safe return values via union
   - Execution delays (fixed, range, variance)
   - Custom callbacks for dynamic behavior
   - Thread-safe operations

3. **Auto-Wrapper System** (`dap_mock_autowrap.sh/ps1`)
   - Automatic linker flag generation
   - Bash/PowerShell scripts (no Python dependency)
   - CMake integration
   - Build directory generation

### Design Philosophy

- **Zero Technical Debt**: No source code modification required
- **Thread-Safe by Design**: All operations use proper synchronization
- **Self-Tested**: 21 comprehensive self-tests validate framework reliability
- **Cross-Platform**: Linux, macOS, partial Windows support
- **Standards Compliant**: All code follows DAP SDK naming conventions

---

## Core Concepts

### 1. Asynchronous Testing

**Problem:** Traditional tests wait synchronously, blocking test execution and wasting CPU.

**Solution:** Async framework provides multiple strategies:

```c
// Strategy 1: Simple macro for quick checks
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, "Message");

// Strategy 2: Callback-based polling for complex conditions
bool check_state(void *data) {
    return ((state_t*)data)->current == STATE_READY;
}
dap_test_wait_condition(check_state, &state, &config);

// Strategy 3: pthread condition variables for async callbacks
dap_test_cond_wait_ctx_t ctx;
dap_test_cond_wait_init(&ctx);
start_async_operation(&ctx, completion_callback);
dap_test_cond_wait(&ctx, 5000);
```

### 2. Function Mocking

**Problem:** Testing code with external dependencies requires isolation.

**Solution:** Linker wrapping (`--wrap`) replaces functions at link time:

```c
// Declare mock (auto-registered via __attribute__((constructor)))
DAP_MOCK_DECLARE(dap_stream_write);

// Configure return value
DAP_MOCK_DECLARE(dap_net_tun_create, {
    .return_value.l = 0xDEADBEEF  // Pointer return via union
});

// Use in test
assert(DAP_MOCK_GET_CALL_COUNT(dap_stream_write) == 1);
```

### 3. Global Timeout Protection

**Problem:** Hanging tests block CI/CD pipelines indefinitely.

**Solution:** Alarm-based timeout with automatic cleanup:

```c
int main() {
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, 30, "My Tests")) {
        return 1;  // Timeout triggered
    }
    
    run_all_tests();  // Max 30 seconds
    
    dap_test_cancel_global_timeout();
    return 0;
}
```

---

## Quick Start

### Installation

The framework is integrated into DAP SDK. No separate installation needed.

```cmake
# In your CMakeLists.txt
target_link_libraries(my_test
    dap_test        # Async utilities + base macros
    dap_test_mocks  # Mock framework
    dap_core
    pthread
)
```

### Your First Test

```c
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_mock_framework.h"

#define LOG_TAG "my_test"
#define TEST_TIMEOUT_SEC 30

// Declare mocks
DAP_MOCK_DECLARE(external_function);

int main() {
    // Initialize DAP SDK
    dap_common_init("my_test", NULL);
    
    // Setup global timeout
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TEST_TIMEOUT_SEC, "My Tests")) {
        return 1;
    }
    
    // Run test
    my_function_under_test();
    
    // Verify
    assert(DAP_MOCK_GET_CALL_COUNT(external_function) == 1);
    
    // Cleanup
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

---

## Tutorial

### Tutorial 1: Testing State Machine with Async Framework

**Scenario:** VPN State Machine transitions from DISCONNECTED to CONNECTED asynchronously.

**Step 1: Setup**

```c
#include "vpn_state_machine.h"
#include "dap_test.h"
#include "dap_test_async.h"

#define LOG_TAG "test_vpn_sm"
#define TEST_TIMEOUT_SEC 30
```

**Step 2: Create Condition Checker**

```c
typedef struct {
    vpn_sm_t *sm;
    vpn_state_t target_state;
} state_check_ctx_t;

bool check_state_reached(void *a_data) {
    state_check_ctx_t *ctx = (state_check_ctx_t *)a_data;
    vpn_state_t current = vpn_sm_get_state(ctx->sm);
    
    log_it(L_DEBUG, "Checking state: current=%d, target=%d",
           current, ctx->target_state);
    
    return current == ctx->target_state;
}
```

**Step 3: Write Test with Timeout**

```c
void test_vpn_connection() {
    log_it(L_INFO, "=== Test: VPN Connection ===");
    
    // Create state machine
    vpn_sm_t *sm = vpn_sm_init();
    dap_assert_PIF(sm != NULL, "State machine created");
    
    // Start connection
    vpn_sm_transition(sm, VPN_EVENT_USER_CONNECT);
    
    // Wait for CONNECTED state with 10 sec timeout
    state_check_ctx_t ctx = { .sm = sm, .target_state = VPN_STATE_CONNECTED };
    
    dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    cfg.timeout_ms = 10000;
    cfg.poll_interval_ms = 100;
    cfg.operation_name = "VPN connection";
    
    bool ok = dap_test_wait_condition(check_state_reached, &ctx, &cfg);
    
    dap_assert_PIF(ok, "VPN should reach CONNECTED within 10 sec");
    dap_assert_PIF(vpn_sm_is_connected(sm), "Should be connected");
    
    // Cleanup
    vpn_sm_deinit(sm);
    log_it(L_INFO, "✓ Test PASSED");
}
```

**Step 4: Add Global Timeout in main()**

```c
int main() {
    dap_common_init("test_vpn_sm", NULL);
    
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TEST_TIMEOUT_SEC, "VPN SM Tests")) {
        log_it(L_CRITICAL, "Test suite timeout!");
        dap_common_deinit();
        return 1;
    }
    
    test_vpn_connection();
    // ... more tests ...
    
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

**What We Learned:**
- ✅ Condition polling prevents busy-waiting
- ✅ Configurable timeouts adapt to operation latency
- ✅ Global timeout prevents suite hangs
- ✅ Structured config improves diagnostics

---

### Tutorial 2: Mocking External Dependencies

**Scenario:** Testing TUN device code without actual kernel device.

**Step 1: Declare Mocks**

```c
#include "dap_mock_framework.h"
#include "dap_net_tun.h"

// Simple mock with default return (NULL)
DAP_MOCK_DECLARE(dap_net_tun_init);

// Mock with custom pointer return
DAP_MOCK_DECLARE(dap_net_tun_create, {
    .return_value.l = 0xDEADBEEF  // Fake TUN handle
});

// Mock with int return
DAP_MOCK_DECLARE(dap_net_tun_get_fd, {
    .return_value.i = 42  // Fake file descriptor
});

// Mock with delay (simulate slow operation)
DAP_MOCK_DECLARE(dap_net_tun_write, {
    .return_value.i = 0
});
```

**Step 2: Configure Mocks in Test**

```c
void test_tun_creation() {
    log_it(L_INFO, "=== Test: TUN Device Creation ===");
    
    // Reset mock state
    DAP_MOCK_RESET(dap_net_tun_create);
    DAP_MOCK_RESET(dap_net_tun_get_fd);
    
    // Configure mocks
    DAP_MOCK_SET_RETURN(dap_net_tun_create, (void*)0xABCDEF00);
    DAP_MOCK_SET_DELAY_FIXED(dap_net_tun_write, 50000);  // 50ms delay
    
    // Call code under test
    vpn_tun_create(&config);
    
    // Verify mock interactions
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_create) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_get_fd) == 1);
    
    log_it(L_INFO, "✓ Test PASSED");
}
```

**Step 3: CMake Configuration**

```cmake
# Include auto-wrapper system
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_tun test_tun.c)

target_link_libraries(test_tun
    dap_test
    dap_test_mocks
    dap_net_tun
    dap_core
    pthread
)

# Auto-generate --wrap flags
dap_mock_autowrap(
    TARGET test_tun
    SOURCE test_tun.c
)
```

**What We Learned:**
- ✅ Mocks declared with simple macros
- ✅ Return values configured via union (.i, .l, .ptr)
- ✅ Delays simulate realistic operation timing
- ✅ Auto-wrapper generates linker flags automatically
- ✅ No production code modification needed

---

### Tutorial 3: Thread-Safe Async Operation Testing

**Scenario:** Testing async callback that runs in worker thread.

**Step 1: Setup Context**

```c
typedef struct {
    dap_test_cond_wait_ctx_t wait;
    bool success;
    void *result_data;
} async_test_ctx_t;
```

**Step 2: Implement Callback**

```c
void async_operation_callback(void *a_arg) {
    async_test_ctx_t *ctx = (async_test_ctx_t *)a_arg;
    
    // Perform async work
    ctx->result_data = do_something();
    ctx->success = (ctx->result_data != NULL);
    
    // Signal completion (thread-safe)
    dap_test_cond_signal(&ctx->wait);
}
```

**Step 3: Write Test**

```c
void test_async_operation() {
    log_it(L_INFO, "=== Test: Async Operation ===");
    
    async_test_ctx_t ctx = {0};
    dap_test_cond_wait_init(&ctx.wait);
    
    // Start async operation in worker thread
    dap_worker_exec_callback_on(worker, async_operation_callback, &ctx);
    
    // Wait with 5 second timeout
    bool ok = dap_test_cond_wait(&ctx.wait, 5000);
    
    dap_assert_PIF(ok, "Async operation should complete within 5 sec");
    dap_assert_PIF(ctx.success, "Operation should succeed");
    dap_assert_PIF(ctx.result_data != NULL, "Should have result data");
    
    // Cleanup
    dap_test_cond_wait_deinit(&ctx.wait);
    
    log_it(L_INFO, "✓ Test PASSED");
}
```

**What We Learned:**
- ✅ pthread condition variables for thread-safe signaling
- ✅ Context structs encapsulate test state
- ✅ Timeout prevents indefinite waits
- ✅ Worker thread coordination works correctly

---

### Tutorial 4: Custom Mock Callbacks

**Scenario:** Mock function needs dynamic behavior based on arguments.

**Step 1: Declare Mock with Callback**

```c
// Inline callback in declaration
DAP_MOCK_DECLARE(dap_hash_fast, {
    .return_value.i = 0
}, {
    // Callback body: hash input based on first argument
    if (a_arg_count >= 2) {
        void *data = a_args[0];
        size_t size = (size_t)a_args[1];
        
        // Simple hash: sum of bytes
        uint32_t hash = 0;
        for (size_t i = 0; i < size; i++) {
            hash += ((uint8_t*)data)[i];
        }
        
        return (void*)(intptr_t)hash;
    }
    return (void*)0;
});
```

**Step 2: Use in Test**

```c
void test_hash_function() {
    log_it(L_INFO, "=== Test: Hash with Mock Callback ===");
    
    uint8_t data[] = {0x01, 0x02, 0x03};
    
    // Call mocked function
    uint32_t hash = dap_hash_fast(data, sizeof(data));
    
    // Verify: callback should sum bytes (1+2+3 = 6)
    assert(hash == 6);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_hash_fast) == 1);
    
    log_it(L_INFO, "✓ Callback returned correct hash: %u", hash);
}
```

**Step 3: Runtime Callback Assignment**

```c
// Define separate callback function
void* custom_hasher(void **args, int count, void *user_data) {
    // Different hashing algorithm
    return (void*)0xCAFEBABE;
}

void test_runtime_callback() {
    // Override inline callback at runtime
    DAP_MOCK_SET_CALLBACK(dap_hash_fast, custom_hasher, NULL);
    
    uint32_t hash = dap_hash_fast(data, size);
    assert(hash == 0xCAFEBABE);
    
    // Clear callback (reverts to static return value)
    DAP_MOCK_CLEAR_CALLBACK(dap_hash_fast);
}
```

**What We Learned:**
- ✅ Inline callbacks for compile-time behavior
- ✅ Runtime callbacks for dynamic behavior
- ✅ Callbacks receive all function arguments
- ✅ Can switch between static and dynamic mocks

---

### Tutorial 5: Performance Testing with Delays

**Scenario:** Test system behavior under slow network conditions.

**Step 1: Configure Mock Delays**

```c
// Fixed delay: exactly 100ms
DAP_MOCK_SET_DELAY_FIXED(network_send, 100000);  // microseconds

// Range delay: random 50-150ms
DAP_MOCK_SET_DELAY_RANGE(network_send, 50000, 150000);

// Variance delay: 100ms ± 20ms (80-120ms)
DAP_MOCK_SET_DELAY_VARIANCE(network_send, 100000, 20000);
```

**Step 2: Measure Actual Delay**

```c
void test_network_timeout() {
    log_it(L_INFO, "=== Test: Network Timeout Handling ===");
    
    // Configure slow network
    DAP_MOCK_SET_DELAY_FIXED(network_send, 200000);  // 200ms
    
    uint64_t start = dap_test_get_time_ms();
    
    // Call function that uses mocked network_send
    int result = send_data_with_timeout(data, size, 100);  // 100ms timeout
    
    uint64_t elapsed = dap_test_get_time_ms() - start;
    
    // Should timeout because mock takes 200ms but timeout is 100ms
    assert(result == -ETIMEDOUT);
    assert(elapsed >= 100 && elapsed < 250);
    
    log_it(L_INFO, "✓ Timeout handled correctly after %llu ms", elapsed);
}
```

**What We Learned:**
- ✅ Delays simulate realistic operation timing
- ✅ Three delay types for different scenarios
- ✅ Time utilities measure actual execution
- ✅ Can test timeout handling accurately

---

## API Reference

### Async Testing API

#### Global Timeout

```c
typedef struct {
    sigjmp_buf jump_buf;
    volatile sig_atomic_t timeout_triggered;
    uint32_t timeout_sec;
    const char *test_name;
} dap_test_global_timeout_t;

int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);
// Returns: 0 on first call, 1 if timeout occurred

void dap_test_cancel_global_timeout(void);
// Cancels alarm and restores signal handler
```

#### Condition Polling

```c
typedef struct {
    uint32_t timeout_ms;          // Maximum wait time
    uint32_t poll_interval_ms;    // Polling interval (0 = 100ms default)
    bool fail_on_timeout;         // true = abort(), false = return false
    const char *operation_name;   // Name for logging
} dap_test_async_config_t;

#define DAP_TEST_ASYNC_CONFIG_DEFAULT { \
    .timeout_ms = 5000, \
    .poll_interval_ms = 100, \
    .fail_on_timeout = true, \
    .operation_name = "async operation" \
}

typedef bool (*dap_test_condition_cb_t)(void *a_user_data);

bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);
// Returns: true if condition met, false on timeout
```

#### pthread Helpers

```c
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool condition_met;
    void *user_data;
} dap_test_cond_wait_ctx_t;

void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);

void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);
// Signal that condition is met (thread-safe)

bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);
// Returns: true if signaled, false on timeout
```

#### Time Utilities

```c
uint64_t dap_test_get_time_ms(void);
// Get monotonic time in milliseconds

void dap_test_sleep_ms(uint32_t a_delay_ms);
// Cross-platform sleep
```

#### Convenience Macros

```c
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg)
// Quick inline condition waiting
// Example: DAP_TEST_WAIT_UNTIL(ready == true, 5000, "Should be ready");
```

---

### Mock Framework API

#### Mock Declaration

```c
// Variant 1: Default config (enabled, return=0)
DAP_MOCK_DECLARE(function_name);

// Variant 2: With structured config
DAP_MOCK_DECLARE(function_name, {
    .enabled = true,
    .return_value.i = 42  // or .l, .u64, .ptr, .str
});

// Variant 3: With callback
DAP_MOCK_DECLARE(function_name, { .return_value.i = 0 }, {
    // Callback body
    return (void*)(intptr_t)123;
});
```

#### Return Value Union

```c
typedef union {
    int i;              // int return
    long l;             // long (pointers on 64-bit)
    uint64_t u64;       // uint64_t
    void *ptr;          // void*
    char *str;          // char*
} dap_mock_return_value_t;
```

#### Mock Control Macros

```c
DAP_MOCK_ENABLE(func_name)          // Enable mock
DAP_MOCK_DISABLE(func_name)         // Disable mock
DAP_MOCK_RESET(func_name)           // Reset call count and state
DAP_MOCK_SET_RETURN(func_name, val) // Set return value
DAP_MOCK_GET_CALL_COUNT(func_name)  // Get call count (returns int)
```

#### Delay Configuration

```c
// Fixed delay
DAP_MOCK_SET_DELAY_FIXED(func_name, microseconds);
DAP_MOCK_SET_DELAY_FIXED_MS(func_name, milliseconds);

// Range delay (random between min and max)
DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us);

// Variance delay (center ± variance)
DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us);

// Clear delay
DAP_MOCK_CLEAR_DELAY(func_name);
```

#### Callback Configuration

```c
typedef void* (*dap_mock_callback_t)(void **a_args, int a_arg_count, void *a_user_data);

DAP_MOCK_SET_CALLBACK(func_name, callback_func, user_data);
DAP_MOCK_CLEAR_CALLBACK(func_name);
```

#### Internal Functions (Advanced)

```c
void dap_mock_framework_init(void);
void dap_mock_framework_deinit(void);

void dap_mock_record_call(
    dap_mock_function_state_t *a_state,
    void **a_args,
    int a_arg_count,
    void *a_return_value
);

dap_mock_call_record_t* dap_mock_get_last_call(dap_mock_function_state_t *a_state);
void **dap_mock_get_call_args(dap_mock_function_state_t *a_state, int a_call_index);
```

---

## Best Practices

### 1. Always Use Global Timeout

```c
✅ DO:
int main() {
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, 30, "Tests")) return 1;
    run_tests();
    dap_test_cancel_global_timeout();
}

❌ DON'T:
int main() {
    run_tests();  // Can hang indefinitely!
}
```

### 2. Choose Appropriate Poll Intervals

```c
✅ DO:
cfg.poll_interval_ms = 100;  // For normal operations
cfg.poll_interval_ms = 500;  // For slow network operations
cfg.poll_interval_ms = 10;   // For fast in-memory operations

❌ DON'T:
cfg.poll_interval_ms = 1;   // Too fast, wastes CPU
cfg.poll_interval_ms = 5000; // Too slow, tests take forever
```

### 3. Use Descriptive Names

```c
✅ DO:
cfg.operation_name = "VPN connection establishment";
cfg.operation_name = "Database transaction commit";

❌ DON'T:
cfg.operation_name = "operation";  // Not helpful in logs
cfg.operation_name = "test";       // Too generic
```

### 4. Clean Up Resources

```c
✅ DO:
dap_test_cond_wait_ctx_t ctx;
dap_test_cond_wait_init(&ctx);
// ... use ctx ...
dap_test_cond_wait_deinit(&ctx);  // Always cleanup!

❌ DON'T:
// Forget cleanup - leaks mutex/cond resources
```

### 5. Mock What You Need, Not Everything

```c
✅ DO:
// Mock only external dependencies
DAP_MOCK_DECLARE(dap_stream_write);
DAP_MOCK_DECLARE(dap_net_tun_create);

❌ DON'T:
// Mock internal functions you're testing
DAP_MOCK_DECLARE(my_internal_helper);  // Test real implementation!
```

### 6. Reset Mocks Between Tests

```c
✅ DO:
void test_something() {
    DAP_MOCK_RESET(my_function);
    // ... test ...
}

❌ DON'T:
// Reuse mock state from previous test - causes flakiness
```

---

## Troubleshooting

### Issue: Test Hangs Despite Timeout

**Symptoms:**
- Global timeout not triggering
- Test runs indefinitely

**Solutions:**
1. Ensure `dap_common_init()` called before timeout setup
2. Check for signal handler conflicts
3. Verify alarm() is available (Unix/Linux/macOS only)
4. Use condition polling as alternative on Windows

**Example:**
```c
// ✅ Correct order
dap_common_init("test", NULL);
dap_test_set_global_timeout(&timeout, 30, "Tests");

// ❌ Wrong order
dap_test_set_global_timeout(&timeout, 30, "Tests");
dap_common_init("test", NULL);  // May override signal handler
```

### Issue: High CPU Usage During Tests

**Symptoms:**
- 100% CPU usage
- System slow during test execution

**Solutions:**
1. Increase poll interval
2. Use condition variables instead of polling for thread sync

**Example:**
```c
// ❌ Too aggressive polling
cfg.poll_interval_ms = 1;  // CPU intensive!

// ✅ Reasonable polling
cfg.poll_interval_ms = 100;  // 10 checks per second

// ✅ Even better for thread sync
dap_test_cond_wait(&ctx, 5000);  // Blocks, no CPU waste
```

### Issue: Flaky Tests (Intermittent Failures)

**Symptoms:**
- Tests sometimes pass, sometimes fail
- Timing-related failures

**Solutions:**
1. Increase timeout
2. Add tolerance to timing assertions
3. Use structured config instead of hardcoded values

**Example:**
```c
// ❌ Fragile timing
assert(elapsed == 100);  // Exact match rarely works

// ✅ Tolerant timing
assert(elapsed >= 90 && elapsed <= 150);  // ±50ms tolerance
```

### Issue: Mock Not Being Called

**Symptoms:**
- `DAP_MOCK_GET_CALL_COUNT` returns 0
- Real function executes instead of mock

**Solutions:**
1. Verify `--wrap` flag in linker options
2. Check mock is enabled
3. Ensure auto-wrapper ran during build

**Example:**
```bash
# Check linker flags
make VERBOSE=1 my_test | grep -- "--wrap"

# Should see:
-Wl,--wrap=dap_stream_write
```

### Issue: Mock Returns Wrong Value

**Symptoms:**
- Unexpected return value from mocked function

**Solutions:**
1. Check return value type matches union field
2. Verify mock configuration
3. Reset mock if reused from previous test

**Example:**
```c
// ❌ Wrong union field for pointer
DAP_MOCK_SET_RETURN(func, (void*)0xDEAD);
int val = g_mock_func->return_value.i;  // Wrong! Use .l or .ptr

// ✅ Correct
void *ptr = g_mock_func->return_value.ptr;
// or for pointers on 64-bit:
void *ptr = (void*)g_mock_func->return_value.l;
```

---

## Glossary

### A

**Async Operation**  
Operation that completes at an unpredictable future time, typically in a different thread or via event loop.

**Auto-Wrapper**  
System that automatically scans source code for mock declarations and generates linker `--wrap` flags.

### C

**Callback**  
Function pointer executed when event occurs (e.g., async operation completes, mock is called).

**Condition Polling**  
Repeatedly checking a condition until it becomes true or timeout occurs.

**Condition Variable**  
pthread synchronization primitive for waiting/signaling between threads.

**Constructor Attribute**  
GCC/Clang attribute that runs function before main() - used for auto-registration.

### D

**DAP_MOCK_DECLARE**  
Universal macro for declaring mocks with optional configuration and callback.

**DAP_TEST_WAIT_UNTIL**  
Convenience macro for quick inline condition waiting.

**Designated Initializers**  
C99 feature for initializing struct fields by name: `{.field = value}`.

### G

**Global Timeout**  
Time limit for entire test suite, enforced via SIGALRM + siglongjmp.

### L

**Linker Wrapping**  
Technique using `--wrap=function` flag to redirect calls to mock implementation.

### M

**Mock**  
Fake implementation of function for testing purposes.

**Mock Framework**  
Infrastructure for declaring, configuring, and verifying mocks.

**Monotonic Clock**  
Time source that always increases, unaffected by system time changes.

### P

**Poll Interval**  
Time between condition checks in polling loop.

**pthread**  
POSIX threads library for multithreading.

### R

**Return Value Union**  
Tagged union for type-safe mock return values (int, long, ptr, etc.).

### S

**Self-Test**  
Test that validates the testing framework itself.

**siglongjmp/sigsetjmp**  
Signal-safe version of longjmp/setjmp for non-local jumps.

**Structured Configuration**  
Using struct with designated initializers instead of multiple parameters.

### T

**Thread-Safe**  
Code that works correctly when called from multiple threads concurrently.

**Timeout**  
Maximum time to wait for operation before giving up.

### U

**Union**  
C type that can hold different types in same memory location (one at a time).

---

## Platform Support Matrix

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| Global Timeout (alarm) | ✅ Full | ✅ Full | ❌ Not available |
| Condition Polling | ✅ Full | ✅ Full | ✅ Full |
| pthread Helpers | ✅ Full | ✅ Full | ✅ Full (with pthread-win32) |
| Time Utilities | ✅ Full | ✅ Full | ✅ Full |
| Mock Framework | ✅ Full | ✅ Full | ⚠️ MSVC limited |
| Auto-Wrapper | ✅ Bash | ✅ Bash | ✅ PowerShell |

---

## Performance Characteristics

| Operation | Overhead | Notes |
|-----------|----------|-------|
| `dap_test_get_time_ms()` | < 1 μs | Monotonic clock access |
| `dap_test_sleep_ms()` | N/A | System sleep |
| Mock call (enabled) | < 10 ns | Counter increment |
| Mock call (disabled) | 0 ns | Direct to real function |
| Condition polling | Configurable | 100ms default |
| pthread cond wait | < 10 μs | Kernel wait |
| Global timeout setup | < 100 μs | Signal handler registration |

---

## Testing the Framework Itself

The framework includes comprehensive self-tests:

```bash
cd build
ctest -R dap_async_framework -V   # 9 async tests
ctest -R dap_mock_framework -V    # 12 mock tests
```

**Self-Test Coverage:**
- ✅ Time utilities accuracy
- ✅ Condition polling (immediate, delayed, timeout)
- ✅ pthread helpers (signal, wait, timeout)
- ✅ Mock declaration variants
- ✅ Return value types
- ✅ Delays (fixed, range, variance)
- ✅ Callbacks (inline, runtime)
- ✅ Thread safety under concurrent load

---

## Examples Repository

All tutorials and examples are validated against actual code:

- **Async Framework Self-Tests:** `test-framework/test/test_async_framework.c`
- **Mock Framework Self-Tests:** `test-framework/test/test_mock_framework.c`
- **VPN State Machine Test:** `tests/unit/test_vpn_state_machine.c`
- **VPN TUN Test:** `tests/unit/test_vpn_tun.c`

---

## Further Reading

- **ASYNC_TESTING.md** - Detailed async API guide
- **mocks/README.md** - Mock framework deep dive
- **mocks/AUTOWRAP.md** - Auto-wrapper system
- **mocks/COMPILER_SUPPORT.md** - Platform compatibility

---

## Contributing

When adding new async testing patterns:

1. Add self-test to validate behavior
2. Document in this guide with example
3. Update glossary
4. Follow DAP SDK coding standards
5. All comments in English

---

**Version:** 1.0.0  
**Last Updated:** 2025-10-27  
**Maintainers:** Cellframe Core Team  
**License:** See project LICENSE file

