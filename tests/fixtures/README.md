# DAP SDK Test Fixtures

This directory contains common test fixtures, mocks, and helper utilities shared across DAP SDK tests.

## Structure

```
fixtures/
├── README.md                    # This file
├── dap_test_helpers.h          # Modern test macros and utilities
└── dap_common_mocks.h          # Common DAP SDK function mocks
```

## Files

### dap_test_helpers.h

Provides modern test macros with enhanced output formatting:

**Test Suite Management:**
- `TEST_SUITE_START(name)` - Start a test suite with formatted header
- `TEST_SUITE_END()` - End suite with summary
- `TEST_RUN(test_func)` - Run a test function

**Assertions (fatal):**
- `TEST_ASSERT(condition, fmt, ...)` - Assert condition is true
- `TEST_ASSERT_EQUAL_INT(expected, actual, fmt, ...)` - Assert integers equal
- `TEST_ASSERT_EQUAL_STRING(expected, actual, fmt, ...)` - Assert strings equal
- `TEST_ASSERT_NULL(ptr, fmt, ...)` - Assert pointer is NULL
- `TEST_ASSERT_NOT_NULL(ptr, fmt, ...)` - Assert pointer is not NULL
- `TEST_FAIL(fmt, ...)` - Unconditionally fail test

**Expectations (non-fatal):**
- `TEST_EXPECT(condition, fmt, ...)` - Check condition, continue if fails
- `TEST_CHECK_EXPECTATIONS()` - Abort if any expectations failed
- `TEST_RESET_EXPECTATIONS()` - Reset expectations counter

**Output:**
- `TEST_INFO(fmt, ...)` - Print informational message
- `TEST_SUCCESS(fmt, ...)` - Print success message
- `TEST_WARN(fmt, ...)` - Print warning message
- `TEST_ERROR(fmt, ...)` - Print error message

**Example:**
```c
#include "dap_test_helpers.h"

void test_example(void) {
    TEST_INFO("Running example test...");
    
    int result = my_function();
    TEST_ASSERT_EQUAL_INT(42, result, "Function should return 42");
    
    TEST_SUCCESS("Test passed!");
}

int main(void) {
    TEST_SUITE_START("My Test Suite");
    TEST_RUN(test_example);
    TEST_SUITE_END();
    return 0;
}
```

### dap_common_mocks.h

Provides ready-to-use mocks for commonly mocked DAP SDK functions:

**Logging:**
- `log_it()` - Mock for log_it function (pass-through by default)

**Memory:**
- `DAP_NEW_Z_impl()` - Mock for zero-initialized allocation
- `DAP_DELETE_impl()` - Mock for deallocation

**Configuration:**
- `dap_config_get_item_bool_default()` - Returns default value
- `dap_config_get_item_str_default()` - Returns default string
- `dap_config_get_item_int32_default()` - Returns default int32
- `dap_config_get_item_uint32_default()` - Returns default uint32

**Helper Functions:**
- `dap_common_mocks_enable_all()` - Enable all common mocks
- `dap_common_mocks_disable_all()` - Disable all common mocks

**Example:**
```c
#include "dap_mock.h"
#include "dap_common_mocks.h"

void setup_test(void) {
    dap_mock_init();
    // Common mocks are already declared, just enable if needed
    DAP_MOCK_SET_ENABLED(log_it, true);
}

void teardown_test(void) {
    dap_mock_deinit();
}
```

## Usage in Tests

### Unit Tests

In `dap-sdk/tests/unit/`, include fixtures as needed:

```c
#include "dap_test.h"
#include "dap_test_helpers.h"  // For modern test macros
#include "dap_mock.h"
#include "dap_common_mocks.h"  // For common mocks
```

CMakeLists.txt should include:
```cmake
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures
    # ... other includes
)
```

### Integration Tests

In `dap-sdk/tests/integration/`, you can use the same fixtures, though integration tests typically use less mocking.

## Adding New Fixtures

When adding new common fixtures:

1. Create a new `.h` file in this directory
2. Use clear naming: `dap_<module>_fixtures.h` or `dap_<module>_mocks.h`
3. Document all functions/macros with comments
4. Update this README.md with the new fixture

## Design Principles

- **Reusability**: Fixtures should be general-purpose, not test-specific
- **Simplicity**: Keep interfaces simple and well-documented
- **Isolation**: Each fixture should be independent
- **Consistency**: Follow existing DAP SDK coding standards
- **Documentation**: All fixtures must be documented

## See Also

- [DAP Mock Framework Documentation](../../test-framework/docs/README.md)
- [DAP Test Framework API Reference](../../test-framework/docs/parts_ru/04_api_reference.md)

