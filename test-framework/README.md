# DAP SDK Test Framework

Reusable testing infrastructure for DAP SDK and Cellframe SDK projects.

## Overview

This framework provides common testing utilities, mock implementations, and test fixtures that can be shared across all Cellframe ecosystem projects.

## Structure

```
test-framework/
├── CMakeLists.txt           # Main build configuration
├── README.md                # This file
├── mocks/                   # Mock implementations
│   ├── CMakeLists.txt
│   ├── dap_mock_framework.h
│   ├── dap_mock_framework.c
│   └── README.md
└── dap_test.h              # Test utilities (if exists)
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
#include "dap_mock_framework.h"

DAP_MOCK_DECLARE(my_function);

int main() {
    dap_mock_framework_init();
    g_mock_my_function = dap_mock_register("my_function");
    dap_mock_set_enabled(g_mock_my_function, true);
    
    // Run tests...
    
    dap_mock_framework_deinit();
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

# Link against mock framework
target_link_libraries(your_test
    dap_test_mocks
    # other dependencies...
)

target_include_directories(your_test PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks
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
target_link_libraries(my_test dap_test_mocks)
```

## Build System

The framework exports the following CMake variables:

- `DAP_TEST_FRAMEWORK_DIR` - Root directory of test framework
- `DAP_TEST_MOCKS_INCLUDE_DIR` - Include directory for mocks

### Building

```bash
cd build
cmake ..
make dap_test_mocks
```

### Artifacts

- `libdap_test_mocks.a` - Mock framework static library
- Headers in `test-framework/mocks/`

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
#include "dap_mock_framework.h"
#include "my_module.h"
#include <assert.h>

DAP_MOCK_DECLARE(dap_client_connect);

void test_connection_retry(void) {
    // Setup
    dap_mock_framework_init();
    g_mock_dap_client_connect = dap_mock_register("dap_client_connect");
    dap_mock_set_enabled(g_mock_dap_client_connect, true);
    DAP_MOCK_SET_RETURN(dap_client_connect, NULL); // Simulate failure
    
    // Execute
    int result = my_module_connect_with_retry();
    
    // Verify
    assert(result == -1);
    assert(dap_mock_get_call_count(g_mock_dap_client_connect) == 3); // 3 retries
    
    // Cleanup
    dap_mock_framework_deinit();
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

1. Create mock header/source in `mocks/` directory
2. Follow naming: `dap_<module>_mock.h/.c`
3. Use `dap_mock_framework.h` as base
4. Update `mocks/CMakeLists.txt` if needed

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

The mock framework itself should be tested:

```bash
cd dap-sdk/test-framework/mocks
# TODO: Add self-tests
```

## Contributing

When adding new testing utilities:

1. Follow DAP SDK coding standards strictly
2. Add comprehensive documentation
3. Update this README
4. Ensure thread-safety where needed
5. Add usage examples

## See Also

- `mocks/README.md` - Mock framework detailed documentation
- `../../core/README.md` - DAP Core library
- `../../../cellframe-sdk/tests/` - Cellframe SDK tests using this framework
- `../../../../cellframe-srv-vpn-client/tests/` - VPN client tests
