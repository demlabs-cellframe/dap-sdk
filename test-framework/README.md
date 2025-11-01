# DAP SDK Test Framework

Reusable testing infrastructure for DAP SDK and Cellframe SDK projects.

## Overview

This framework provides common testing utilities, mock implementations, and test fixtures that can be shared across all Cellframe ecosystem projects.

## Structure

```
test-framework/
├── CMakeLists.txt           # Main build configuration
├── README.md                # This file
├── dap_test.h              # Basic test utilities
├── dap_test.c              # Basic test utilities implementation
├── dap_test_async.h        # Async testing utilities
├── dap_test_async.c        # Async testing implementation
├── dap_mock.h              # Mock framework API
├── dap_mock.c              # Mock framework implementation
├── dap_mock_async.h        # Async mock execution
├── dap_mock_async.c        # Async mock implementation
├── dap_mock_linker_wrapper.h # Linker wrapper macros
├── mocks/                   # Mock autowrap scripts
│   ├── DAPMockAutoWrap.cmake
│   ├── dap_mock_autowrap.sh
│   ├── dap_mock_autowrap.ps1
│   └── dap_mock_autowrap.py
└── test/                    # Self-tests
    ├── CMakeLists.txt
    ├── test_async_framework.c
    └── test_mock_framework.c
```

## Components

### Mock Framework (`mocks/`)

Generic function mocking with call tracking and verification. See `mocks/README.md` for detailed documentation.

**Key Features:**
- Thread-safe mock function state management
- Call history tracking (up to 100 calls per function)
- Argument verification
- Return value control
- DAP SDK coding standards compliant

**Quick Start:**
```c
#include "dap_mock.h"

DAP_MOCK_DECLARE(my_function);

int main() {
    // Note: dap_mock_init() is auto-called via constructor!
    // Manual call not needed, but available for compatibility
    
    // Run tests...
    
    // Optional cleanup (auto-called via destructor)
    // dap_mock_deinit();
}
```

### Test Utilities (`dap_test.h`)

Common test helper functions and assertions (if implemented in the future).

## Integration Guide

### For Cellframe SDK Projects

Add to your test `CMakeLists.txt`:

```cmake
# Include DAP SDK test framework
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework
    ${CMAKE_BINARY_DIR}/dap-test-framework
)

# Link against test framework (includes mocks)
target_link_libraries(your_test
    dap_test          # Test framework with mocks
    dap_core          # DAP core library
    pthread           # Threading support
    # other dependencies...
)

target_include_directories(your_test PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework
    ${CMAKE_SOURCE_DIR}/dap-sdk/core/include
    # other includes...
)
```

### For External Projects (e.g., VPN Client)

Already configured automatically if using Cellframe SDK as submodule:

```cmake
# In your tests/CMakeLists.txt
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework
    ${CMAKE_BINARY_DIR}/dap-test-framework
)

# Use in tests
target_link_libraries(my_test dap_test dap_core pthread)
```

## Build System

The framework exports the following CMake variables:

- `DAP_TEST_FRAMEWORK_DIR` - Root directory of test framework
- `DAP_TEST_LIBRARY` - Test framework library name (`dap_test`)

### Building

```bash
cd build
cmake ..
make dap_test
```

### Artifacts

- `libdap_test.a` - Test framework static library (includes mocks)
- Headers in `test-framework/`:
  - `dap_test.h` - Basic test utilities
  - `dap_test_async.h` - Async testing utilities
  - `dap_mock.h` - Mock framework API
  - `dap_mock_async.h` - Async mock execution
  - `dap_mock_linker_wrapper.h` - Linker wrapper macros

## Coding Standards

All code follows DAP SDK conventions:

- **Prefixes:**
  - `dap_` - Public functions and types
  - `s_` - Static/file-local variables
  - `g_` - Global variables
  - `l_` - Local variables
  - `a_` - Function parameters

- **Logging:**
  - Use `log_it()` for all logging
  - Define `LOG_TAG` in each .c file

- **Memory:**
  - Use `DAP_NEW()`, `DAP_NEW_Z()` for allocation
  - Use `DAP_DELETE()` for deallocation

- **Error Handling:**
  - Return `int` (0 = success, negative = error) or `bool`
  - Use `UNUSED(var)` for intentionally unused parameters

## Usage Examples

### Unit Test with Mocks

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "my_module.h"
#include <assert.h>

DAP_MOCK_DECLARE(dap_client_connect);

void test_connection_retry(void) {
    // Setup
    DAP_MOCK_SET_RETURN(dap_client_connect, NULL);
    
    // Execute
    int result = my_module_connect_with_retry();
    
    // Verify
    assert(result == -1);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_client_connect) == 3); // 3 retries
    
}
```

### Integration Test

```c
#include "dap_test.h"
#include "dap_common.h"
#include "my_module.h"

int main(void) {
    dap_common_init("test_my_module");
    
    // Initialize module
    int ret = my_module_init();
    assert(ret == 0);
    
    // Run tests
    test_basic_operation();
    test_error_handling();
    test_edge_cases();
    
    // Cleanup
    my_module_deinit();
    dap_common_deinit();
    
    return 0;
}
```

## Extending the Framework

### Adding New Mock Implementations

1. Declare mocks using `DAP_MOCK_DECLARE()` macro in your test files
2. Use `DAP_MOCK_WRAPPER_CUSTOM()` or `DAP_MOCK_WRAPPER_PASSTHROUGH()` for linker wrappers
3. Include `dap_mock.h` in your test files
4. Use `dap_mock_autowrap()` in CMakeLists.txt to generate linker flags

### Adding Test Utilities

1. Create utility in test-framework root
2. Name it `dap_test_<utility>.h/.c`
3. Update main `CMakeLists.txt`
4. Document in this README

## Dependencies

- **dap_core**: Logging, memory, common utilities
- **pthread**: Thread synchronization
- **C11**: Standard library

## Maintenance

### Versioning

Test framework version follows DAP SDK versioning scheme.

### Backward Compatibility

Mock framework API is stable. New features added via:
- New functions (not breaking existing API)
- Optional parameters with defaults
- Feature flags

### Testing the Framework

The framework includes comprehensive self-tests:

```bash
cd dap-sdk/test-framework/build
make test
# Runs test_async_framework and test_mock_framework
# Total: 21 test functions validating framework reliability
```

## Contributing

When adding new testing utilities:

1. Follow DAP SDK coding standards strictly
2. Add comprehensive documentation
3. Update this README
4. Ensure thread-safety where needed
5. Add usage examples

## See Also

- `docs/` - Comprehensive documentation (guides, examples, API reference)
- `docs/DAP_MOCK_ASYNC.md` - Async mock execution guide
- `../../core/README.md` - DAP Core library
- `../../../cellframe-sdk/tests/` - Cellframe SDK tests using this framework
- `../../../../cellframe-srv-vpn-client/tests/` - VPN client tests
