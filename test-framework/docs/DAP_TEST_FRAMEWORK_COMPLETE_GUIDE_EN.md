# DAP SDK Test Framework - Complete Guide

**Version:** 1.0.0  
**Date:** October 27, 2025  
**Authors:** Cellframe Development Team  
**Language:** English

---

## Table of Contents

### Part I: Introduction
1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Installation](#3-installation)

### Part II: Core Concepts
4. [Asynchronous Testing](#4-asynchronous-testing)
5. [Function Mocking](#5-function-mocking)
6. [Timeout Management](#6-timeout-management)

### Part III: Tutorial
7. [Quick Start](#7-quick-start)
8. [State Machine Testing](#8-state-machine-testing)
9. [Network Operations Testing](#9-network-operations-testing)
10. [Mock Patterns](#10-mock-patterns)

### Part IV: Reference
11. [Async API Reference](#11-async-api-reference)
12. [Mock API Reference](#12-mock-api-reference)
13. [CMake Integration](#13-cmake-integration)

### Part V: Advanced Topics
14. [Thread Safety](#14-thread-safety)
15. [Performance Optimization](#15-performance-optimization)
16. [Cross-Platform Considerations](#16-cross-platform-considerations)

### Part VI: Appendices
17. [Best Practices](#17-best-practices)
18. [Troubleshooting Guide](#18-troubleshooting-guide)
19. [Glossary](#19-glossary)
20. [Examples Index](#20-examples-index)

---

# Part I: Introduction

## 1. Overview

The DAP SDK Test Framework is a production-ready testing infrastructure designed for the Cellframe blockchain ecosystem. It provides comprehensive tools for testing asynchronous operations, mocking external dependencies, and ensuring reliable test execution across platforms.

### 1.1 Key Features

**Async Testing Framework:**
- Global test timeout (prevents infinite hangs)
- Condition polling with configurable intervals
- pthread condition variable helpers
- Cross-platform time utilities
- Convenience macros for quick tests

**Mock Framework V4:**
- Linker-based function mocking (zero technical debt)
- Type-safe return values via union
- Execution delays (fixed, range, variance)
- Custom callbacks for dynamic behavior
- Thread-safe operations
- Auto-registration (no manual initialization)

**Auto-Wrapper System:**
- Automatic linker flag generation
- Bash/PowerShell scripts (no Python dependency)
- CMake integration
- Compiler detection (GCC, Clang, MSVC, MinGW)

### 1.2 Design Philosophy

1. **Zero Technical Debt**
   - No modification of production code
   - Linker-based mocking preserves original behavior
   - Clean separation of test and production code

2. **Thread-Safe by Design**
   - All mock operations use mutexes
   - Atomic counters for call tracking
   - pthread condition variables for synchronization

3. **Self-Tested**
   - 21 comprehensive self-tests
   - 100% pass rate validates framework reliability
   - Tests serve as usage documentation

4. **Standards Compliant**
   - All code follows DAP SDK naming conventions
   - Proper use of `log_it`, `LOG_TAG`
   - All comments in English

### 1.3 When to Use This Framework

**Use Cases:**
- ✅ Testing state machines with async transitions
- ✅ Testing network operations with timeouts
- ✅ Testing code with external dependencies (files, network, devices)
- ✅ Testing thread-safe concurrent operations
- ✅ Performance testing with timing measurements
- ✅ CI/CD integration (timeout protection)

**Not Suitable For:**
- ❌ Simple synchronous unit tests (use plain assertions)
- ❌ End-to-end system tests (use integration test framework)
- ❌ Performance profiling (use dedicated profilers)

---

## 2. Architecture

### 2.1 Component Overview

```
DAP SDK Test Framework
├── Async Testing Layer (dap_test_async.h/c)
│   ├── Global Timeout (alarm + siglongjmp)
│   ├── Condition Polling (callback-based)
│   ├── pthread Helpers (condition variables)
│   └── Time Utilities (monotonic clock)
│
├── Mock Framework Layer (dap_mock_framework.h/c)
│   ├── Mock Declaration (DAP_MOCK_DECLARE)
│   ├── Return Value Management (union-based)
│   ├── Call Tracking (atomic counters)
│   ├── Delay Execution (3 types)
│   └── Custom Callbacks (inline + runtime)
│
├── Auto-Wrapper System
│   ├── Bash Script (dap_mock_autowrap.sh)
│   ├── PowerShell Script (dap_mock_autowrap.ps1)
│   └── CMake Module (DAPMockAutoWrap.cmake)
│
└── Self-Tests
    ├── test_async_framework.c (9 tests)
    └── test_mock_framework.c (12 tests)
```

### 2.2 Data Flow

**Async Testing Flow:**
```
Test Code
  → dap_test_wait_condition()
  → Polling Loop (every poll_interval_ms)
  → Condition Callback
  → Check Result
  → Return (met) or Continue (timeout)
```

**Mock Invocation Flow:**
```
Production Code Call
  → Linker Redirect (--wrap)
  → __wrap_function_name()
  → Check if Mock Enabled
  → Execute Delay (if configured)
  → Execute Callback (if configured)
  → Record Call (args, return value)
  → Return Mock Value
```

### 2.3 Threading Model

**Async Framework:**
- Main thread: Test execution
- Polling: In test thread (no extra threads)
- pthread condition wait: Blocks test thread efficiently

**Mock Framework:**
- Thread-safe: Uses pthread_mutex_t
- Call tracking: Atomic operations
- Callback execution: In caller's thread context

---

## 3. Installation

### 3.1 Building the Framework

The framework is built automatically with DAP SDK:

```bash
cd cellframe-node/dap-sdk
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ..
make dap_test dap_test_mocks
```

**Outputs:**
- `libdap_test.a` - Async utilities
- `libdap_test_mocks.a` - Mock framework
- Headers in `test-framework/` directory

### 3.2 Running Self-Tests

Validate framework installation:

```bash
cd build
ctest -R dap_async_framework -V
ctest -R dap_mock_framework -V
```

**Expected:**
```
Test #1: dap_async_framework .......   Passed  1.81 sec  ✅
Test #2: dap_mock_framework ........   Passed  1.12 sec  ✅

100% tests passed
```

### 3.3 Integration in Your Project

**CMakeLists.txt:**
```cmake
# Include test framework
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework
    ${CMAKE_BINARY_DIR}/dap-test-framework
)

# Create test executable
add_executable(my_test my_test.c)

# Link libraries
target_link_libraries(my_test
    dap_test        # Async utilities + base macros
    dap_test_mocks  # Mock framework
    dap_core        # DAP SDK core
    pthread         # Threading support
)

# Include directories
target_include_directories(my_test PRIVATE
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework/mocks
)

# Register with CTest
add_test(NAME my_test COMMAND my_test)
```

---

# Part II: Core Concepts

## 4. Asynchronous Testing

### 4.1 The Problem

Traditional testing approaches use synchronous waits:

```c
// ❌ Bad: Busy waiting
while (!operation_complete) {
    usleep(1000);  // 1ms sleep, CPU waste
}

// ❌ Bad: Blocking indefinitely
while (!operation_complete) {
    // Infinite loop if operation fails!
}

// ❌ Bad: Coarse granularity
for (int i = 0; i < 10; i++) {
    if (operation_complete) break;
    sleep(1);  // 1 second chunks, slow tests
}
```

**Problems:**
- CPU waste (busy waiting)
- Infinite hangs (no timeout)
- Slow tests (coarse granularity)
- No diagnostics (why did it timeout?)

### 4.2 The Solution: Async Framework

The framework provides three approaches, from simple to advanced:

**Level 1: Macro (Simplest)**
```c
DAP_TEST_WAIT_UNTIL(operation_complete, 5000, "Operation should finish");
// Polls every 100ms, aborts on timeout, automatic logging
```

**Level 2: Function (Flexible)**
```c
bool check_complete(void *data) {
    return ((state_t*)data)->done;
}

dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
cfg.timeout_ms = 10000;
cfg.poll_interval_ms = 200;
cfg.operation_name = "Database commit";

bool ok = dap_test_wait_condition(check_complete, &state, &cfg);
```

**Level 3: pthread (Advanced)**
```c
dap_test_cond_wait_ctx_t ctx;
dap_test_cond_wait_init(&ctx);

start_async_operation(&ctx, callback);  // Callback signals when done

bool ok = dap_test_cond_wait(&ctx, 5000);
dap_test_cond_wait_deinit(&ctx);
```

### 4.3 Configuration Options

```c
typedef struct {
    uint32_t timeout_ms;          // Max wait time (milliseconds)
    uint32_t poll_interval_ms;    // Check frequency (0 = 100ms default)
    bool fail_on_timeout;         // true = abort(), false = return false
    const char *operation_name;   // For logging/diagnostics
} dap_test_async_config_t;

// Default values
#define DAP_TEST_ASYNC_CONFIG_DEFAULT { \
    .timeout_ms = 5000, \
    .poll_interval_ms = 100, \
    .fail_on_timeout = true, \
    .operation_name = "async operation" \
}
```

**Choosing Poll Interval:**
- Fast operations (< 10ms): `poll_interval_ms = 10`
- Normal operations (10-100ms): `poll_interval_ms = 100` (default)
- Slow operations (network, disk): `poll_interval_ms = 500`
- Very slow (external services): `poll_interval_ms = 1000`

### 4.4 Time Utilities

```c
// Get current time (monotonic, unaffected by system clock changes)
uint64_t start = dap_test_get_time_ms();

// Do operation
do_something();

// Measure elapsed time
uint64_t elapsed = dap_test_get_time_ms() - start;
log_it(L_INFO, "Operation took %llu ms", elapsed);

// Sleep accurately
dap_test_sleep_ms(100);  // Sleep 100ms
```

---

## 5. Function Mocking

### 5.1 The Problem

Testing code with external dependencies is difficult:

```c
// ❌ Hard to test: depends on real network
int my_function() {
    dap_client_t *client = dap_client_create(...);  // Real network!
    dap_stream_write(client, data, size);           // Real I/O!
    return process_response();
}
```

**Problems:**
- Requires real network/services
- Slow tests (real I/O latency)
- Non-deterministic (network conditions vary)
- Hard to test error paths
- Difficult to isolate bugs

### 5.2 The Solution: Linker Wrapping

The framework uses GCC/Clang `--wrap` linker option:

```bash
# Linker redirects calls
gcc test.o -Wl,--wrap=dap_client_create

# Call flow:
# dap_client_create() → __wrap_dap_client_create() → mock
# __real_dap_client_create() → original function
```

**Benefits:**
- ✅ No source code modification
- ✅ Production code unchanged
- ✅ Test-time only mocking
- ✅ Can call real function via __real_ if needed

### 5.3 Mock Declaration Variants

**Variant 1: Simple (Default Config)**
```c
DAP_MOCK_DECLARE(function_name);
// Enabled: true
// Return: 0 (NULL for pointers)
```

**Variant 2: With Configuration**
```c
DAP_MOCK_DECLARE(function_name, {
    .enabled = true,
    .return_value.i = 42  // int return
});

DAP_MOCK_DECLARE(another_func, {
    .return_value.ptr = (void*)0xDEADBEEF  // pointer return
});
```

**Variant 3: With Callback**
```c
DAP_MOCK_DECLARE(hash_function, {
    .return_value.i = 0
}, {
    // Inline callback body
    if (a_arg_count >= 1) {
        int input = (int)(intptr_t)a_args[0];
        return (void*)(intptr_t)(input * 2);
    }
    return (void*)0;
});
```

### 5.4 Return Value Types

```c
typedef union {
    int i;         // For int, bool, small enums
    long l;        // For pointers (cast to void*)
    uint64_t u64;  // For uint64_t, size_t (on 64-bit)
    void *ptr;     // For void*, generic pointers
    char *str;     // For char*, string returns
} dap_mock_return_value_t;

// Usage examples:
.return_value.i = 42              // int
.return_value.l = 0xDEADBEEF      // pointer as long
.return_value.u64 = 1234567890ULL // uint64_t
.return_value.ptr = my_ptr        // void*
.return_value.str = "hello"       // char*
```

### 5.5 Delay Types

Simulate realistic operation timing:

```c
// 1. Fixed Delay (exact duration)
DAP_MOCK_SET_DELAY_FIXED(network_read, 100000);  // 100ms always
DAP_MOCK_SET_DELAY_FIXED_MS(network_read, 100);  // Same, in ms

// 2. Range Delay (random between min-max)
DAP_MOCK_SET_DELAY_RANGE(network_read, 50000, 150000);  // 50-150ms

// 3. Variance Delay (center ± variance)
DAP_MOCK_SET_DELAY_VARIANCE(network_read, 100000, 20000);  // 100±20ms = 80-120ms

// Clear delay
DAP_MOCK_CLEAR_DELAY(network_read);
```

**Use Cases:**
- **Fixed**: Testing timeout boundaries
- **Range**: Simulating variable latency
- **Variance**: Realistic network jitter

---

## 6. Timeout Management

### 6.1 Global Test Timeout

Prevents entire test suite from hanging:

```c
int main() {
    dap_common_init("my_test", NULL);
    
    // Setup 30-second timeout for entire suite
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, 30, "My Test Suite")) {
        // This code runs if timeout triggered
        log_it(L_CRITICAL, "Test suite exceeded 30 seconds!");
        dap_common_deinit();
        return 1;
    }
    
    // Run tests (max 30 seconds total)
    run_all_tests();
    
    // Cancel timeout on normal completion
    dap_test_cancel_global_timeout();
    
    dap_common_deinit();
    return 0;
}
```

**How It Works:**
1. `signal(SIGALRM, handler)` - Register signal handler
2. `sigsetjmp()` - Save execution context
3. `alarm(seconds)` - Start countdown
4. If timeout: `siglongjmp()` - Jump back to setjmp point
5. If complete: `alarm(0)` - Cancel alarm

**Platform Support:**
- ✅ Linux: Full support
- ✅ macOS: Full support
- ❌ Windows: Not available (use condition polling instead)

### 6.2 Per-Operation Timeouts

Individual operation timeouts with polling:

```c
// Configure timeout for specific operation
dap_test_async_config_t cfg = {
    .timeout_ms = 10000,       // 10 seconds
    .poll_interval_ms = 100,   // Check every 100ms
    .fail_on_timeout = false,  // Don't abort, return false
    .operation_name = "Database query"
};

bool ok = dap_test_wait_condition(check_query_done, &ctx, &cfg);
if (!ok) {
    log_it(L_WARNING, "Query timed out, but test continues");
}
```

### 6.3 Thread Synchronization Timeouts

pthread condition variable with timeout:

```c
dap_test_cond_wait_ctx_t ctx;
dap_test_cond_wait_init(&ctx);

// Worker thread will signal when done
start_worker_operation(&ctx);

// Wait up to 5 seconds
if (!dap_test_cond_wait(&ctx, 5000)) {
    log_it(L_ERROR, "Worker did not complete in 5 seconds");
}

dap_test_cond_wait_deinit(&ctx);
```

---

# Part III: Tutorial

## 7. Quick Start

### 7.1 Minimal Test

```c
#include "dap_test.h"
#include "dap_common.h"

#define LOG_TAG "my_test"

int main() {
    dap_common_init("my_test", NULL);
    
    // Your test code
    int result = my_function(42);
    dap_assert_PIF(result == 0, "Function should succeed");
    
    dap_common_deinit();
    return 0;
}
```

### 7.2 With Async Timeout

```c
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_common.h"

#define LOG_TAG "my_test"
#define TIMEOUT_SEC 30

int main() {
    dap_common_init("my_test", NULL);
    
    // Add global timeout
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TIMEOUT_SEC, "My Test")) {
        return 1;
    }
    
    // Your tests
    test_something();
    
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

### 7.3 With Mocks

```c
#include "dap_test.h"
#include "dap_mock_framework.h"
#include "dap_common.h"

#define LOG_TAG "my_test"

// Declare mocks
DAP_MOCK_DECLARE(external_function);

int main() {
    dap_common_init("my_test", NULL);
    dap_mock_framework_init();
    
    // Configure mock
    DAP_MOCK_SET_RETURN(external_function, (void*)42);
    
    // Run test
    int result = code_that_calls_external_function();
    
    // Verify
    assert(DAP_MOCK_GET_CALL_COUNT(external_function) == 1);
    
    dap_mock_framework_deinit();
    dap_common_deinit();
    return 0;
}
```

**CMakeLists.txt for Mock Test:**
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(my_test my_test.c)

target_link_libraries(my_test
    dap_test_mocks
    dap_core
    pthread
)

# Auto-generate --wrap flags
dap_mock_autowrap(TARGET my_test SOURCE my_test.c)
```

---

## 8. State Machine Testing

### 8.1 Scenario

Test VPN state machine transitions:
```
DISCONNECTED → CONNECTING → CONNECTED
```

### 8.2 Setup Test File

```c
// test_vpn_state_machine.c
#include "vpn_state_machine.h"
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_mock_framework.h"

#define LOG_TAG "test_vpn_sm"
#define TEST_TIMEOUT_SEC 30

// Mock external dependencies
DAP_MOCK_DECLARE(dap_chain_node_client_connect_mt);
DAP_MOCK_DECLARE(dap_net_tun_create, {
    .return_value.l = 0xDEADBEEF
});
DAP_MOCK_DECLARE(vpn_client_routing_setup);
```

### 8.3 Write Condition Checker

```c
typedef struct {
    vpn_sm_t *sm;
    vpn_state_t target_state;
} sm_check_ctx_t;

bool check_state(void *a_data) {
    sm_check_ctx_t *ctx = (sm_check_ctx_t *)a_data;
    vpn_state_t current = vpn_sm_get_state(ctx->sm);
    
    log_it(L_DEBUG, "Current state: %d, target: %d", current, ctx->target_state);
    
    return current == ctx->target_state;
}
```

### 8.4 Write Test Function

```c
void test_connection_flow() {
    log_it(L_INFO, "=== Test: Connection Flow ===");
    
    // Create state machine
    vpn_sm_t *sm = vpn_sm_init();
    dap_assert_PIF(sm != NULL, "State machine created");
    
    // Configure mocks for success path
    DAP_MOCK_SET_RETURN(dap_chain_node_client_connect_mt, (void*)0);
    DAP_MOCK_SET_RETURN(vpn_client_routing_setup, (void*)0);
    
    // Trigger transition
    vpn_sm_transition(sm, VPN_EVENT_USER_CONNECT);
    
    // Wait for CONNECTING state
    sm_check_ctx_t ctx1 = { .sm = sm, .target_state = VPN_STATE_CONNECTING };
    dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    cfg.timeout_ms = 5000;
    cfg.operation_name = "CONNECTING state";
    
    bool ok = dap_test_wait_condition(check_state, &ctx1, &cfg);
    dap_assert_PIF(ok, "Should reach CONNECTING state");
    
    // Simulate successful connection
    vpn_sm_transition(sm, VPN_EVENT_CONNECTION_SUCCESS);
    
    // Wait for CONNECTED state
    ctx1.target_state = VPN_STATE_CONNECTED;
    cfg.operation_name = "CONNECTED state";
    ok = dap_test_wait_condition(check_state, &ctx1, &cfg);
    dap_assert_PIF(ok, "Should reach CONNECTED state");
    
    // Verify mocks were called
    assert(DAP_MOCK_GET_CALL_COUNT(dap_chain_node_client_connect_mt) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(vpn_client_routing_setup) == 1);
    
    // Cleanup
    vpn_sm_deinit(sm);
    
    log_it(L_INFO, "✓ Test PASSED");
}
```

### 8.5 Main Function

```c
int main(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize
    dap_common_init("test_vpn_sm", NULL);
    dap_mock_framework_init();
    
    // Global timeout (30 sec for all tests)
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TEST_TIMEOUT_SEC, "VPN SM Tests")) {
        log_it(L_CRITICAL, "Test suite timeout!");
        dap_mock_framework_deinit();
        dap_common_deinit();
        return 1;
    }
    
    log_it(L_INFO, "=== VPN State Machine Tests ===");
    
    // Run tests
    test_connection_flow();
    test_disconnection();
    test_reconnection();
    test_error_handling();
    
    dap_test_cancel_global_timeout();
    log_it(L_INFO, "=== All Tests PASSED ===");
    
    // Cleanup
    dap_mock_framework_deinit();
    dap_common_deinit();
    return 0;
}
```

### 8.6 CMake Configuration

```cmake
# test/CMakeLists.txt
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_vpn_state_machine
    test_vpn_state_machine.c
)

target_link_libraries(test_vpn_state_machine
    vpn_client_lib
    dap_test
    dap_test_mocks
    dap_core
    pthread
)

# Auto-generate --wrap flags
dap_mock_autowrap(
    TARGET test_vpn_state_machine
    SOURCE test_vpn_state_machine.c
)

add_test(NAME test_vpn_sm COMMAND test_vpn_state_machine)
set_tests_properties(test_vpn_sm PROPERTIES TIMEOUT 60)
```

---

## 9. Network Operations Testing

### 9.1 Scenario

Test HTTP client with timeout and retry:

```c
int http_get_with_retry(const char *url, char **response);
```

### 9.2 Test with Async Framework

```c
#include "dap_test.h"
#include "dap_test_async.h"

#define LOG_TAG "test_http_client"

typedef struct {
    bool complete;
    int status_code;
    char *response_data;
} http_test_ctx_t;

bool check_http_complete(void *a_data) {
    http_test_ctx_t *ctx = (http_test_ctx_t *)a_data;
    return ctx->complete;
}

void test_http_request() {
    log_it(L_INFO, "=== Test: HTTP GET ===");
    
    http_test_ctx_t ctx = {0};
    
    // Start async request
    http_get_async("https://example.com", http_callback, &ctx);
    
    // Wait up to 30 seconds (network operation)
    dap_test_async_config_t cfg = {
        .timeout_ms = 30000,
        .poll_interval_ms = 500,  // Check every 500ms for network
        .fail_on_timeout = false,  // Network may be unavailable
        .operation_name = "HTTP GET request"
    };
    
    bool ok = dap_test_wait_condition(check_http_complete, &ctx, &cfg);
    
    if (ok) {
        dap_assert_PIF(ctx.status_code == 200, "Should get 200 OK");
        dap_assert_PIF(ctx.response_data != NULL, "Should have response");
        log_it(L_INFO, "✓ HTTP request successful");
    } else {
        log_it(L_WARNING, "HTTP request timed out - network unavailable?");
    }
    
    // Cleanup
    if (ctx.response_data) free(ctx.response_data);
}
```

### 9.3 Testing with Fallback Hosts

```c
static const char *g_test_hosts[] = {
    "httpbin.org",
    "postman-echo.com",
    "reqres.in",
    NULL
};

void test_with_fallback() {
    for (int i = 0; g_test_hosts[i] != NULL; i++) {
        log_it(L_INFO, "Trying host: %s", g_test_hosts[i]);
        
        http_test_ctx_t ctx = {0};
        http_get_async(g_test_hosts[i], "/get", &ctx);
        
        dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
        cfg.timeout_ms = 10000;
        cfg.fail_on_timeout = false;
        
        if (dap_test_wait_condition(check_http_complete, &ctx, &cfg)) {
            log_it(L_INFO, "✓ Host %s responded", g_test_hosts[i]);
            return;  // Success
        }
        
        log_it(L_WARNING, "Host %s timeout, trying next", g_test_hosts[i]);
    }
    
    log_it(L_ERROR, "All hosts failed");
}
```

---

## 10. Mock Patterns

### 10.1 Basic Mocking

```c
// Declare
DAP_MOCK_DECLARE(dap_stream_write);

// Test
void test_write() {
    DAP_MOCK_RESET(dap_stream_write);
    DAP_MOCK_SET_RETURN(dap_stream_write, (void*)100);  // Bytes written
    
    int result = send_data(buffer, 100);
    
    assert(result == 100);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_stream_write) == 1);
}
```

### 10.2 Simulating Failures

```c
void test_write_failure() {
    // Mock returns error
    DAP_MOCK_SET_RETURN(dap_stream_write, (void*)-1);
    
    int result = send_data(buffer, 100);
    
    assert(result == -1);
    assert(error_handler_was_called());
}
```

### 10.3 Testing Retry Logic

```c
void test_retry_on_failure() {
    // First 2 calls fail, 3rd succeeds
    DAP_MOCK_SET_RETURN(dap_stream_write, (void*)-1);
    
    int result = send_with_retry(buffer, 100, 3);
    
    // Should retry 3 times
    assert(DAP_MOCK_GET_CALL_COUNT(dap_stream_write) == 3);
}
```

### 10.4 Dynamic Mock Behavior

```c
// Callback changes behavior based on call count
DAP_MOCK_DECLARE(flaky_function, {.return_value.i = 0}, {
    int call_count = DAP_MOCK_GET_CALL_COUNT(flaky_function);
    
    if (call_count < 2) {
        return (void*)-1;  // Fail first 2 calls
    }
    return (void*)0;  // Succeed after
});
```

### 10.5 Performance Testing

```c
void test_slow_network() {
    // Simulate 200ms network latency
    DAP_MOCK_SET_DELAY_FIXED_MS(network_send, 200);
    
    uint64_t start = dap_test_get_time_ms();
    
    send_packet(data, size);
    
    uint64_t elapsed = dap_test_get_time_ms() - start;
    
    assert(elapsed >= 190 && elapsed <= 250);
    log_it(L_INFO, "Network operation took %llu ms", elapsed);
}
```

---

[Continued in next part...]
