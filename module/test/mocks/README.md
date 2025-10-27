# DAP SDK Mock Framework

Generic mock framework for testing DAP SDK and Cellframe SDK applications using **GNU ld linker wrapping**.

**🚀 NEW: [Auto-Wrapper System](AUTOWRAP.md)** - автоматическая генерация linker wrapping конфигураций!

## Overview

This framework provides **true function mocking** in C using the `--wrap` linker option. When you compile tests with `-Wl,--wrap=function_name`, the linker automatically redirects all calls to `function_name` to `__wrap_function_name`, allowing complete control over function behavior without modifying production code.

### How It Works

```
Your Test Code          Linker Magic              Real Library
    |                        |                          |
    |--call dap_foo()------->|                          |
    |                        |--redirects to-->__wrap_dap_foo()
    |                        |                    (your mock)
    |<------mock returns-----|                          |
    |                        |                          |
    |  (mock can call)       |                          |
    |--__real_dap_foo()----->|--calls original-->dap_foo()
    |<-----real returns------|<---------------------|
```

**Key advantages:**
- ✅ **No source code changes** - production code stays clean
- ✅ **True function replacement** - not just call tracking
- ✅ **Can call real function** - via `__real_` prefix
- ✅ **Link-time substitution** - zero runtime overhead
- ✅ **Standard GCC/ld feature** - no special tools needed

## Architecture

```
dap-sdk/test-framework/mocks/
├── CMakeLists.txt                    # Build configuration
├── dap_mock_framework.h              # Core API (state tracking)
├── dap_mock_framework.c              # Implementation
├── dap_mock_linker_wrapper.h         # Wrapper macros
├── test_mock_linker_example.c        # Complete example
└── README.md                         # This file
```

## Quick Start Example

### 1. Declare Mock State

```c
#include "dap_mock_framework.h"
#include "dap_mock_linker_wrapper.h"

DAP_MOCK_DECLARE(dap_stream_write);
```

### 2. Create Linker Wrapper

```c
// This replaces real dap_stream_write at link time
DAP_MOCK_WRAPPER_INT(dap_stream_write,
    (void *a_stream, void *a_data, size_t a_size),
    (a_stream, a_data, a_size))
```

### 3. Configure CMake

```cmake
add_executable(my_test test.c)

target_link_options(my_test PRIVATE
    -Wl,--wrap=dap_stream_write
)

target_link_libraries(my_test
    dap_test_mocks
    dap_core
)
```

### 4. Write Test

```c
void test_network_write(void) {
    dap_mock_framework_init();
    
    // Register and enable mock
    g_mock_dap_stream_write = dap_mock_register("dap_stream_write");
    dap_mock_set_enabled(g_mock_dap_stream_write, true);
    DAP_MOCK_SET_RETURN(dap_stream_write, 100);
    
    // Call function - linker redirects to our mock!
    int result = dap_stream_write(stream, data, size);
    
    // Verify
    assert(dap_mock_get_call_count(g_mock_dap_stream_write) == 1);
    assert(result == 100);
    
    dap_mock_framework_deinit();
}
```

## API Reference

### Core Functions

```c
int dap_mock_framework_init(void);
void dap_mock_framework_deinit(void);
void dap_mock_framework_reset_all(void);
dap_mock_function_state_t* dap_mock_register(const char *a_name);
void dap_mock_set_enabled(dap_mock_function_state_t *a_state, bool a_enabled);
void dap_mock_record_call(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count, void *a_return_value);
int dap_mock_get_call_count(dap_mock_function_state_t *a_state);
dap_mock_call_record_t* dap_mock_get_last_call(dap_mock_function_state_t *a_state);
bool dap_mock_was_called_with(dap_mock_function_state_t *a_state, int a_arg_index, void *a_expected_value);
void dap_mock_reset(dap_mock_function_state_t *a_state);
```

### Wrapper Macros

#### DAP_MOCK_WRAPPER_INT
For functions returning `int`:
```c
DAP_MOCK_WRAPPER_INT(function_name,
    (param_type1 a_param1, param_type2 a_param2),
    (a_param1, a_param2))
```

#### DAP_MOCK_WRAPPER_PTR
For functions returning pointers:
```c
DAP_MOCK_WRAPPER_PTR(function_name,
    (param_type1 a_param1),
    (a_param1))
```

#### DAP_MOCK_WRAPPER_VOID_FUNC
For `void` functions:
```c
DAP_MOCK_WRAPPER_VOID_FUNC(function_name,
    (param_type1 a_param1),
    (a_param1))
```

#### DAP_MOCK_WRAPPER_BOOL
For functions returning `bool`:
```c
DAP_MOCK_WRAPPER_BOOL(function_name,
    (param_type1 a_param1),
    (a_param1))
```

#### DAP_MOCK_WRAPPER_SIZE_T
For functions returning `size_t`:
```c
DAP_MOCK_WRAPPER_SIZE_T(function_name,
    (param_type1 a_param1),
    (a_param1))
```

### Helper Macros

```c
DAP_MOCK_DECLARE(func_name)              // Declare mock state variable
DAP_MOCK_INIT(func_name)                 // Register mock
DAP_MOCK_ENABLE(func_name)               // Enable mock
DAP_MOCK_DISABLE(func_name)              // Disable mock
DAP_MOCK_SET_RETURN(func_name, value)    // Set return value (integers)
DAP_MOCK_SET_RETURN_PTR(func_name, ptr)  // Set return value (pointers)
DAP_MOCK_GET_CALL_COUNT(func_name)       // Get call count
DAP_MOCK_WAS_CALLED(func_name)           // Check if called
DAP_MOCK_RESET(func_name)                // Reset call history
```

## Complete Example

See `test_mock_linker_example.c` for a full working example with:
- Mock state declaration
- Linker wrapper definitions
- CMake configuration
- Multiple test scenarios

Build and run:
```bash
cd build/dap-test-framework/mocks
make test_mock_linker_example
./test_mock_linker_example
```

## Integration in Projects

### VPN Client Tests

```cmake
# tests/unit/CMakeLists.txt

add_executable(test_vpn_client
    test_vpn_client.c
)

# Specify functions to wrap
target_link_options(test_vpn_client PRIVATE
    -Wl,--wrap=dap_common_init
    -Wl,--wrap=dap_config_get_item_str
    -Wl,--wrap=dap_stream_ch_pkt_write_mt
    -Wl,--wrap=dap_net_tun_create
    # Add all DAP SDK functions you need to mock
)

target_link_libraries(test_vpn_client
    cellframe_srv_vpn_client_lib
    dap_test_mocks
    dap_core
)
```

### In Test File

```c
#include "dap_mock_framework.h"
#include "dap_mock_linker_wrapper.h"

// Declare mocks
DAP_MOCK_DECLARE(dap_common_init);
DAP_MOCK_DECLARE(dap_config_get_item_str);

// Create wrappers
DAP_MOCK_WRAPPER_INT(dap_common_init,
    (const char *a_app_name, const char *a_log_file),
    (a_app_name, a_log_file))

DAP_MOCK_WRAPPER_PTR(dap_config_get_item_str,
    (void *a_config, const char *a_section, const char *a_key),
    (a_config, a_section, a_key))

// Write tests
void test_my_function(void) {
    dap_mock_framework_init();
    
    g_mock_dap_common_init = dap_mock_register("dap_common_init");
    dap_mock_set_enabled(g_mock_dap_common_init, true);
    DAP_MOCK_SET_RETURN(dap_common_init, 0);
    
    // Test your code...
    
    dap_mock_framework_deinit();
}
```

## How Linker Wrapping Works

### Compile-Time

Your code:
```c
int result = dap_stream_write(stream, data, 100);
```

Compiles to call to `dap_stream_write` symbol (unresolved).

### Link-Time with `--wrap`

```bash
gcc -o test test.o -ldap_core -Wl,--wrap=dap_stream_write
```

Linker performs:
1. Renames `dap_stream_write` → `__real_dap_stream_write`
2. Resolves calls to `dap_stream_write` → `__wrap_dap_stream_write`
3. Your `__wrap_` function can call `__real_` to access original

### Runtime

```
test.o calls:           After linking:
dap_stream_write() -->  __wrap_dap_stream_write()
                            |
                            |--if mock enabled: return mock value
                            |
                            |--if mock disabled: call __real_dap_stream_write()
```

## Advantages Over Other Approaches

### ❌ Weak Symbols
```c
__attribute__((weak)) int dap_foo(void) { /* real */ }
```
- Requires modifying production code
- Creates technical debt
- Not portable

### ❌ Preprocessor
```c
#define dap_foo mock_dap_foo
```
- Only works if tester compiles source
- Brittle
- Header pollution

### ✅ Linker Wrapping
- ✅ No source changes
- ✅ Standard GCC feature
- ✅ Clean separation of test/production
- ✅ Can call real function
- ✅ Zero technical debt

## Thread Safety

All mock operations are thread-safe:
- Framework-level mutex for registration
- Per-function mutex for call recording
- Minimal locking overhead

## Limitations

- **Linux/GCC only** - `--wrap` is GNU ld specific (works on BSD too)
- **Must mock at link time** - can't dynamically enable/disable wrapping
- **One wrapper per function** - can't have multiple mocks for same function
- **100 calls max** per function tracked (`DAP_MOCK_MAX_CALLS`)
- **10 arguments max** per call recorded

## Best Practices

1. **Mock external dependencies only** - don't mock your own module
2. **One wrapper file per module** - organize wrappers logically
3. **Document wrapped functions** - list them in test file header
4. **Reset between tests** - call `dap_mock_framework_reset_all()`
5. **Verify call counts** - always check mocks were actually called

## Troubleshooting

### Undefined reference to `__wrap_function`
- Add wrapper macro in test file
- Check function signature matches exactly

### Undefined reference to `__real_function`
- Add `-Wl,--wrap=function` to link options
- Ensure function exists in linked libraries

### Mock not called
- Verify `dap_mock_set_enabled()` was called
- Check wrapper macro is correct
- Ensure linker option is set

### Segfault in mock
- Initialize framework first: `dap_mock_framework_init()`
- Register mock: `g_mock_func = dap_mock_register("func")`
- Check argument array size matches parameter count

## See Also

- **[AUTOWRAP.md](AUTOWRAP.md)** - 🚀 Автоматическая генерация wrap конфигураций (НОВОЕ!)
- `dap_mock_linker_wrapper.h` - Wrapper macro definitions
- `test_mock_linker_example.c` - Complete working example
- `dap-sdk/test-framework/README.md` - Test framework overview
- GNU ld manual: https://sourceware.org/binutils/docs/ld/Options.html (search for `--wrap`)

## Contributing

When adding new wrapper types:
1. Follow `DAP_MOCK_WRAPPER_TYPE` naming
2. Include both parameter types and names
3. Handle NULL mock state gracefully
4. Forward to real function when disabled
5. Document in this README
