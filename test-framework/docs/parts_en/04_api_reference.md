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
// Initialize mock framework (required before using mocks)
// Returns: 0 on success

void dap_mock_deinit(void);
// Cleanup mock framework
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
} dap_mock_config_t;

// Default: enabled=true, return=0, no delay
#define DAP_MOCK_CONFIG_DEFAULT { \
    .enabled = true, \
    .return_value = {0}, \
    .delay = {.type = DAP_MOCK_DELAY_NONE} \
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
- Uses `_Generic()` for proper pointer casting

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

\newpage
