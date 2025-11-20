## 3. API Reference

### 3.1 Async Testing API

#### Global Timeout
```c
int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);
// Returns: 0 on setup, 1 if timeout triggered

void dap_test_cancel_global_timeout(void);
```

#### Condition Polling
```c
bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);
// Returns: true if condition met, false on timeout
// 
// Callback signature:
// typedef bool (*dap_test_condition_cb_t)(void *a_user_data);
//
// Config structure:
// typedef struct {
//     uint32_t timeout_ms;          // Max wait time (ms)
//     uint32_t poll_interval_ms;    // Polling interval (ms)
//     bool fail_on_timeout;         // abort() on timeout?
//     const char *operation_name;   // For logging
// } dap_test_async_config_t;
//
// Default config: DAP_TEST_ASYNC_CONFIG_DEFAULT
//   - timeout_ms: 5000 (5 seconds)
//   - poll_interval_ms: 100 (100 ms)
//   - fail_on_timeout: true
//   - operation_name: "async operation"
```

#### pthread Helpers
```c
void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);
bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);
void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);
```

#### Time Utilities
```c
uint64_t dap_test_get_time_ms(void);  // Monotonic time in ms
void dap_test_sleep_ms(uint32_t a_delay_ms);  // Cross-platform sleep
```

#### Macros
```c
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg)
// Quick inline condition waiting
```

### 3.2 Mock Framework API

**Header:** `dap_mock.h`

#### Framework Initialization
```c
int dap_mock_init(void);
// Optional: Reinitialize mock framework (auto-initialized via constructor)
// Returns: 0 on success
// Note: Framework auto-initializes before main(), manual call not required
// Cross-platform: Uses __attribute__((constructor)) on GCC/Clang/MinGW,
//                 static C++ object on MSVC

void dap_mock_deinit(void);
// Cleanup mock framework (call in teardown if needed)
// Note: Also auto-deinitializes async system if enabled
// Auto-cleanup: Uses __attribute__((destructor)) on GCC/Clang,
//               atexit() on MSVC for automatic cleanup after main()
```

#### Mock Declaration Macros

**Simple Declaration (auto-enabled, return 0):**
```c
DAP_MOCK_DECLARE(function_name);
```

**With Configuration Structure:**
```c
DAP_MOCK_DECLARE(function_name, {
    .enabled = true,
    .return_value.l = 0xDEADBEEF,
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 1000
    }
});
```

**With Inline Callback:**
```c
DAP_MOCK_DECLARE(function_name, {.return_value.i = 0}, {
    // Callback body - custom logic for each call
    if (a_arg_count >= 1) {
        int arg = (int)(intptr_t)a_args[0];
        return (void*)(intptr_t)(arg * 2);  // Double the input
    }
    return (void*)0;
});
```

**For Custom Wrapper (no auto-wrapper generation):**
```c
DAP_MOCK_DECLARE_CUSTOM(function_name, {
    .delay = {
        .type = DAP_MOCK_DELAY_VARIANCE,
        .variance = {.center_us = 100000, .variance_us = 50000}
    }
});
```

#### Configuration Structures

**dap_mock_config_t:**
```c
typedef struct dap_mock_config {
    bool enabled;                      // Enable/disable mock
    dap_mock_return_value_t return_value;  // Return value
    dap_mock_delay_t delay;            // Execution delay
    bool async;                        // Execute callback asynchronously (default: false)
    bool call_original_before;         // Call original function BEFORE mock logic (default: false)
    bool call_original_after;          // Call original function AFTER mock logic (default: false)
} dap_mock_config_t;

// Default: enabled=true, return=0, no delay, sync, no original call
#define DAP_MOCK_CONFIG_DEFAULT { \
    .enabled = true, \
    .return_value = {0}, \
    .delay = {.type = DAP_MOCK_DELAY_NONE}, \
    .async = false, \
    .call_original_before = false, \
    .call_original_after = false \
}

// Passthrough config: track calls but always call original before mock (for integration tests)
#define DAP_MOCK_CONFIG_PASSTHROUGH { \
    .enabled = true, \
    .return_value = {0}, \
    .delay = {.type = DAP_MOCK_DELAY_NONE}, \
    .async = false, \
    .call_original_before = true, \
    .call_original_after = false \
}
```

**dap_mock_return_value_t:**
```c
typedef union dap_mock_return_value {
    int i;         // For int, bool, small types
    long l;        // For pointers (cast with intptr_t)
    uint64_t u64;  // For uint64_t, size_t (64-bit)
    void *ptr;     // For void*, generic pointers
    char *str;     // For char*, strings
} dap_mock_return_value_t;
```

**dap_mock_delay_t:**
```c
typedef enum {
    DAP_MOCK_DELAY_NONE,      // No delay
    DAP_MOCK_DELAY_FIXED,     // Fixed delay
    DAP_MOCK_DELAY_RANGE,     // Random in [min, max]
    DAP_MOCK_DELAY_VARIANCE   // Center ± variance
} dap_mock_delay_type_t;

typedef struct dap_mock_delay {
    dap_mock_delay_type_t type;
    union {
        uint64_t fixed_us;
        struct { uint64_t min_us; uint64_t max_us; } range;
        struct { uint64_t center_us; uint64_t variance_us; } variance;
    };
} dap_mock_delay_t;
```

#### Control Macros
```c
DAP_MOCK_ENABLE(func_name)
// Enable mock (intercept calls)
// Example: DAP_MOCK_ENABLE(dap_stream_write);

DAP_MOCK_DISABLE(func_name)
// Disable mock (call real function)
// Example: DAP_MOCK_DISABLE(dap_stream_write);

DAP_MOCK_RESET(func_name)
// Reset call history and statistics
// Example: DAP_MOCK_RESET(dap_stream_write);

DAP_MOCK_SET_RETURN(func_name, value)
// Set return value (cast with (void*) or (void*)(intptr_t))
// Example: DAP_MOCK_SET_RETURN(dap_stream_write, (void*)(intptr_t)42);

DAP_MOCK_GET_CALL_COUNT(func_name)
// Get number of times mock was called (returns int)
// Example: int count = DAP_MOCK_GET_CALL_COUNT(dap_stream_write);

DAP_MOCK_WAS_CALLED(func_name)
// Returns true if called at least once (returns bool)
// Example: assert(DAP_MOCK_WAS_CALLED(dap_stream_write));

DAP_MOCK_GET_ARG(func_name, call_idx, arg_idx)
// Get specific argument from a specific call
// call_idx: 0-based index of call (0 = first call)
// arg_idx: 0-based index of argument (0 = first argument)
// Returns: void* (cast to appropriate type)
// Example: void *buffer = DAP_MOCK_GET_ARG(dap_stream_write, 0, 1);
//          size_t size = (size_t)DAP_MOCK_GET_ARG(dap_stream_write, 0, 2);
```

#### Delay Configuration Macros
```c
DAP_MOCK_SET_DELAY_FIXED(func_name, microseconds)
DAP_MOCK_SET_DELAY_MS(func_name, milliseconds)
// Set fixed delay

DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us)
DAP_MOCK_SET_DELAY_RANGE_MS(func_name, min_ms, max_ms)
// Set random delay in range

DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us)
DAP_MOCK_SET_DELAY_VARIANCE_MS(func_name, center_ms, variance_ms)
// Set delay with variance (e.g., 100ms ± 20ms)

DAP_MOCK_CLEAR_DELAY(func_name)
// Remove delay
```

#### Callback Configuration
```c
DAP_MOCK_SET_CALLBACK(func_name, callback_func, user_data)
// Set custom callback function

DAP_MOCK_CLEAR_CALLBACK(func_name)
// Remove callback (use return_value instead)

// Callback signature:
typedef void* (*dap_mock_callback_t)(
    void **a_args,
    int a_arg_count,
    void *a_user_data
);
```

### 3.3 Custom Linker Wrapper API

**Header:** `dap_mock_linker_wrapper.h`

#### DAP_MOCK_WRAPPER_CUSTOM Macro

Creates custom linker wrapper with PARAM syntax:

```c
DAP_MOCK_WRAPPER_CUSTOM(return_type, function_name,
    PARAM(type1, name1),
    PARAM(type2, name2),
    ...
) {
    // Custom wrapper implementation
}
```

**Features:**
- Automatically generates function signature
- Automatically creates void* argument array with proper casts
- Automatically checks if mock is enabled
- Automatically executes configured delay
- Automatically records call
- Calls real function if mock disabled

**Example:**
```c
DAP_MOCK_WRAPPER_CUSTOM(int, my_function,
    PARAM(const char*, path),
    PARAM(int, flags),
    PARAM(mode_t, mode)
) {
    // Your custom logic here
    if (strcmp(path, "/dev/null") == 0) {
        return -1;  // Simulate error
    }
    return 0;  // Success
}
```

**PARAM Macro:**
- Format: `PARAM(type, name)`
- Extracts type and name automatically
- Handles casting to void* correctly
- Uses `uintptr_t` for safe casting of pointers and integer types

#### Simpler Wrapper Macros

For common return types:

```c
DAP_MOCK_WRAPPER_INT(func_name, (params), (args))
DAP_MOCK_WRAPPER_PTR(func_name, (params), (args))
DAP_MOCK_WRAPPER_VOID_FUNC(func_name, (params), (args))
DAP_MOCK_WRAPPER_BOOL(func_name, (params), (args))
DAP_MOCK_WRAPPER_SIZE_T(func_name, (params), (args))
```

### 3.4 CMake Integration

**CMake Module:** `mocks/DAPMockAutoWrap.cmake`

```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

# Automatically scan sources and generate --wrap flags
dap_mock_autowrap(target_name)

# Alternative: specify source files explicitly
dap_mock_autowrap(TARGET target_name SOURCE file1.c file2.c)
```

**How it works:**
1. Scans source files for `DAP_MOCK_DECLARE` patterns
2. Extracts function names
3. Adds `-Wl,--wrap=function_name` to linker flags
4. Works with GCC, Clang, MinGW

#### Mocking Functions in Static Libraries (FULLY AUTOMATIC)

**Background:** The `--wrap` linker flag only works with static libraries (`.a`), not with object files (`.o`). When SDK modules are linked as object files, `--wrap` cannot intercept internal function calls between modules.

**Problem:** Functions inside static libraries may be excluded from the final executable if not directly used, causing `--wrap` to fail.

**Solution (FULLY AUTOMATIC):** The `dap_mock_autowrap()` function now **automatically** handles everything:
1. ✅ Detects all `*_static.a` libraries in the project
2. ✅ Wraps them with `--whole-archive` flags  
3. ✅ Adds `--allow-multiple-definition` for duplicate symbols
4. ✅ No manual configuration needed!

**Usage Example:**

```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_SOURCE_DIR}/dap-sdk/tests/cmake/dap_test_helpers.cmake)

add_executable(test_http_client 
    test_http_client.c
)

# Link using helper function (automatically uses STATIC libraries)
dap_test_link_libraries(test_http_client)

# Include directories
dap_test_add_includes(test_http_client)

# Auto-generate --wrap flags AND automatically wrap all *_static libraries
# That's it! One function call does everything!
dap_mock_autowrap(test_http_client)
```

**What happens automatically:**

1. `dap_test_link_libraries()` links all SDK modules as **STATIC libraries** (`*_static.a`)
2. `dap_mock_autowrap()` scans sources for `DAP_MOCK_DECLARE` and generates `--wrap` flags
3. `dap_mock_autowrap()` **automatically** detects all `*_static.a` libraries and wraps them:
   ```
   -Wl,--start-group
   -Wl,--whole-archive libdap_io_static.a -Wl,--no-whole-archive
   -Wl,--whole-archive libdap_http_server_static.a -Wl,--no-whole-archive
   ...other libraries...
   -Wl,--end-group
   ```
4. Adds `-Wl,--allow-multiple-definition` to handle duplicate symbols

**Important Notes:**

1. **Only ONE function call needed:**
   ```cmake
   # Modern approach (RECOMMENDED):
   dap_mock_autowrap(test_target)  # Everything automatic!
   
   # Legacy approach (still works, but not needed):
   dap_mock_autowrap(test_target)
   dap_mock_autowrap_with_static(test_target lib)  # Optional override
   ```

2. **Static libraries are created automatically:**
   - All SDK modules have `*_static.a` versions (created from object libraries)
   - Created in main `dap-sdk/CMakeLists.txt` when tests are enabled
   - All dependencies properly propagated with `_static` suffix

3. **Technical requirements:**
   - **Compiler:** GCC, Clang, or MinGW (MSVC not supported for `--wrap`)
   - **Helper function:** Must use `dap_test_link_libraries()` for static library linking
   - **Critical:** `--wrap` does NOT work with object files (`.o`) - only static libraries (`.a`)
   - **Why:** Object files linked directly have resolved symbols - no indirection for `--wrap` to intercept

**Complete Configuration Example:**

```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_stream_mocks
    test_stream_mocks.c
    test_stream_mocks_wrappers.c
)

target_link_libraries(test_stream_mocks
    dap_test
    dap_stream       # Static library
    dap_net          # Static library
    dap_core
    pthread
)

target_include_directories(test_stream_mocks PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework
    ${CMAKE_SOURCE_DIR}/dap-sdk/core/include
)

# Auto-generate --wrap flags
dap_mock_autowrap(test_stream_mocks)

# Wrap static libraries for mocking internal functions
dap_mock_autowrap_with_static(test_stream_mocks 
    dap_stream
    dap_net
)
```

**Verifying Configuration:**

```bash
# Check linker flags
cd build
make VERBOSE=1 | grep -E "--wrap|--whole-archive"

# Should see:
# -Wl,--wrap=dap_stream_write
# -Wl,--wrap=dap_net_tun_create
# -Wl,--whole-archive ... dap_stream ... -Wl,--no-whole-archive
# -Wl,--whole-archive ... dap_net ... -Wl,--no-whole-archive
```

### 3.5 Async Mock Execution

**Header:** `dap_mock_async.h`

Provides lightweight asynchronous execution for mock callbacks without requiring full `dap_events` infrastructure. Perfect for unit tests that need to simulate async behavior in isolation.

#### Initialization

```c
// Initialize async system with worker threads
int dap_mock_async_init(uint32_t a_worker_count);
// a_worker_count: 0 = auto, typically 1-2 for unit tests
// Returns: 0 on success

// Deinitialize (waits for all pending tasks)
void dap_mock_async_deinit(void);

// Check if initialized
bool dap_mock_async_is_initialized(void);
```

#### Task Scheduling

```c
// Schedule async callback execution
dap_mock_async_task_t* dap_mock_async_schedule(
    dap_mock_async_callback_t a_callback,
    void *a_arg,
    uint32_t a_delay_ms  // 0 = immediate
);

// Cancel pending task
bool dap_mock_async_cancel(dap_mock_async_task_t *a_task);
```

#### Waiting for Completion

```c
// Wait for specific task
bool dap_mock_async_wait_task(
    dap_mock_async_task_t *a_task,
    int a_timeout_ms  // -1 = infinite, 0 = no wait
);

// Wait for all pending tasks
bool dap_mock_async_wait_all(int a_timeout_ms);
// Returns: true if all completed, false on timeout
```

#### Async Mock Configuration

To enable async execution for a mock, set `.async = true` in config:

```c
// Async mock with delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request, {
    .enabled = true,
    .async = true,  // Execute callback asynchronously
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 50000  // 50ms
    }
});

// Mock wrapper (executes asynchronously if dap_mock_async_init() was called)
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request,
    PARAM(const char*, a_url),
    PARAM(callback_t, a_callback),
    PARAM(void*, a_arg)
) {
    // This code runs in worker thread after delay
    a_callback("response data", 200, a_arg);
}
```

#### Utility Functions

```c
// Get pending task count
size_t dap_mock_async_get_pending_count(void);

// Get completed task count
size_t dap_mock_async_get_completed_count(void);

// Execute all pending tasks immediately (fast-forward time)
void dap_mock_async_flush(void);

// Reset statistics
void dap_mock_async_reset_stats(void);

// Set default delay for async mocks
void dap_mock_async_set_default_delay(uint32_t a_delay_ms);
```

#### Usage Pattern

```c
void test_async_http(void) {
    // Note: No manual init needed! Async system auto-initializes with mock framework
    
    volatile bool done = false;
    
    // Call function with async mock (configured with .async = true)
    dap_client_http_request("http://test.com", callback, &done);
    
    // Wait for async completion
    DAP_TEST_WAIT_UNTIL(done, 5000, "HTTP request");
    
    // Or wait for all async mocks
    bool completed = dap_mock_async_wait_all(5000);
    assert(completed && done);
    
    // Cleanup (optional, handled by dap_mock_deinit())
    // dap_mock_deinit();  // Auto-cleans async system too
}
```

**Note:** Async system is automatically initialized when mock framework starts (via constructor). Manual `dap_mock_async_init()` only needed if you want to customize worker count.

\newpage
