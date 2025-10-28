# DAP SDK Unit Tests

## Overview

This directory contains **unit tests** for DAP SDK components. Unit tests are:

- ✅ **Fast** - Execute in milliseconds
- ✅ **Isolated** - Use mocks for all dependencies
- ✅ **Deterministic** - Always produce same results
- ✅ **No external dependencies** - No network, files, databases

## Test Structure

```
unit/
├── README.md                        # This file
├── CMakeLists.txt                   # Build configuration
├── test_stream_transport.c          # ✅ 8/8 tests passing
├── test_stream_obfuscation.c        # ✅ 7/7 tests passing
└── mock_gen/                        # Auto-generated mock wrappers
```

## Current Tests

### ✅ test_stream_transport.c (8/8 passing)

Tests for stream transport layer with full mocking:

1. **Transport Registration** - Register HTTP/UDP/WebSocket transports
2. **Transport Unregistration** - Unregister transports
3. **Multiple Transports** - Coexistence of multiple transports
4. **HTTP Capabilities** - Verify HTTP transport capabilities
5. **UDP Capabilities** - Verify UDP transport capabilities  
6. **UDP Configuration** - Test configuration get/set
7. **WebSocket Capabilities** - Verify WebSocket capabilities
8. **WebSocket Configuration** - Test WebSocket config

**Mocks Used**: None (tests transport registry only)

**Run Time**: ~100ms

### ✅ test_stream_obfuscation.c (7/7 active tests passing)

Tests for obfuscation layer:

1. **Engine Lifecycle** - Create/destroy obfuscation engine
2. **Custom Configuration** - Test LOW/MEDIUM/HIGH configs
3. **Small Data Cycle** - Obfuscate/deobfuscate small data
4. **Medium Data Cycle** - Test with medium-sized data
5. **Large Data Cycle** - Test with large data
9. **NULL Pointer Handling** - Error handling for invalid inputs
10. **Multiple Cycles** - Stress test with 5 cycles

**Disabled Tests** (3):
- Test 6: Padding (requires packet metadata)
- Test 7: Fake traffic (requires full implementation)
- Test 8: Timing delays (requires full implementation)

**Mocks Used**: None (tests obfuscation algorithms)

**Run Time**: ~50ms

## Running Unit Tests

```bash
# Build all unit tests
cd build
cmake -DBUILD_DAP_TESTS=ON ..
make

# Run all unit tests
ctest -L unit

# Run specific test
./cellframe-node/dap-sdk/tests/unit/test_stream_transport
./cellframe-node/dap-sdk/tests/unit/test_stream_obfuscation

# Run with verbose output
./test_stream_transport --verbose
```

## Writing Unit Tests

### Template Structure

```c
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "module_to_test.h"

// Setup: Initialize mocks
static void setup_test(void) {
    dap_mock_init();
    // Enable required mocks
    DAP_MOCK_SET_ENABLED(external_function, true);
}

// Teardown: Cleanup mocks
static void teardown_test(void) {
    dap_mock_deinit();
}

// Test with mocking
static void test_my_function(void) {
    setup_test();
    
    TEST_INFO("Testing my_function with mocks...");
    
    // Configure mock behavior
    DAP_MOCK_SET_RETURN_VALUE(external_function, 42);
    
    // Call function under test
    int result = my_function();
    
    // Verify results
    TEST_ASSERT_EQUAL_INT(42, result, "Should return mocked value");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(external_function) == 1, 
                "External function called once");
    
    teardown_test();
    TEST_SUCCESS("Test passed");
}

int main(void) {
    TEST_SUITE_START("My Module Unit Tests");
    TEST_RUN(test_my_function);
    TEST_SUITE_END();
    return 0;
}
```

### Mock Framework Usage

#### Declaring Mocks

```c
// In test file or mock header
#include "dap_mock.h"

// Simple mock declaration (returns 0 by default)
DAP_MOCK_DECLARE(int, my_function, int arg1, const char *arg2);

// Custom mock with callback
DAP_MOCK_WRAPPER_CUSTOM(int, my_custom_function,
    PARAM(int, a_value),
    PARAM(const char *, a_string)
) {
    // Custom logic here
    if (a_value > 0) {
        return a_value * 2;
    }
    return -1;
}
```

#### Using Mocks in Tests

```c
// Enable/disable mock
DAP_MOCK_SET_ENABLED(my_function, true);

// Set return value
DAP_MOCK_SET_RETURN_VALUE(my_function, 123);

// Get call count
int calls = DAP_MOCK_GET_CALL_COUNT(my_function);

// Reset mock state
DAP_MOCK_RESET(my_function);
```

### Best Practices

1. **One Assert Per Test**: Focus each test on single behavior
2. **Test Names**: Use descriptive names (test_01_basic_operation)
3. **Mock All External Deps**: Network, file I/O, system calls
4. **Fast Execution**: Unit tests should complete in <1 second
5. **No Side Effects**: Tests should not modify global state
6. **Cleanup**: Always cleanup mocks in teardown

### Common Mocks

See `../fixtures/dap_common_mocks.h` for reusable mocks:
- `log_it` - Logging functions
- `DAP_NEW_Z_impl` / `DAP_DELETE_impl` - Memory allocation
- `dap_config_get_*` - Configuration access

## Test Helpers

From `../fixtures/dap_test_helpers.h`:

```c
// Suite management
TEST_SUITE_START("Test Suite Name");
TEST_SUITE_END();
TEST_RUN(test_function);

// Assertions (fatal)
TEST_ASSERT(condition, "message", ...);
TEST_ASSERT_EQUAL_INT(expected, actual, "message");
TEST_ASSERT_NULL(ptr, "message");
TEST_ASSERT_NOT_NULL(ptr, "message");

// Expectations (non-fatal)
TEST_EXPECT(condition, "message");
TEST_CHECK_EXPECTATIONS();

// Output
TEST_INFO("Info message", ...);
TEST_SUCCESS("Success message", ...);
TEST_WARN("Warning message", ...);
TEST_ERROR("Error message", ...);
```

## CMake Integration

### Adding New Unit Test

```cmake
# In tests/unit/CMakeLists.txt

project(test_my_module)

add_executable(${PROJECT_NAME}
    test_my_module.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_my_module
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures
    # ... module includes
)

# Auto-wrap mocks
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60)
```

## Coverage Goals

Target coverage for unit tests:
- **Functions**: >80%
- **Branches**: >70%
- **Lines**: >85%

## See Also

- [Integration Tests](../integration/README.md)
- [Test Fixtures](../fixtures/README.md)
- [DAP Mock Framework](../../test-framework/docs/README.md)
- [Test Helpers API](../fixtures/README.md#dap_test_helpersh)

