## 2. Quick Start

### 2.1 Your First Test (5 minutes)

**Step 1:** Create test file

```c
// my_test.c
#include "dap_test.h"
#include "dap_common.h"

#define LOG_TAG "my_test"

int main() {
    dap_common_init("my_test", NULL);
    
    // Test code
    int result = 2 + 2;
    dap_assert_PIF(result == 4, "Math should work");
    
    log_it(L_INFO, "[+] Test passed!");
    
    dap_common_deinit();
    return 0;
}
```

**Step 2:** Create CMakeLists.txt

```cmake
add_executable(my_test my_test.c)

# Simple way: link single library
target_link_libraries(my_test dap_core)

add_test(NAME my_test COMMAND my_test)
```

**Step 2 (alternative):** Automatic linking of all SDK modules

```cmake
add_executable(my_test my_test.c)

# Universal way: automatically links ALL DAP SDK modules
# + all external dependencies (XKCP, Kyber, SQLite, PostgreSQL, etc.)
dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES)

add_test(NAME my_test COMMAND my_test)
```

> **Advantage:** `dap_link_all_sdk_modules()` automatically connects all SDK modules and their external dependencies. No need to list dozens of libraries manually!

**Step 3:** Build and run

```bash
cd build
cmake ..
make my_test
./my_test
```

### 2.2 Adding Async Timeout (2 minutes)

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
        return 1;  // Timeout triggered
    }
    
    // Your tests here
    
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

Update CMakeLists.txt:
```cmake
# Link test framework library (includes dap_test, dap_mock, etc.)
target_link_libraries(my_test dap_test dap_core pthread)

# Or use universal way (automatically connects dap_core + all dependencies):
# dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES LINK_LIBRARIES dap_test)
```

### 2.3 Adding Mocks (5 minutes)

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_common.h"
#include <assert.h>

#define LOG_TAG "my_test"

// Declare mock
DAP_MOCK_DECLARE(external_api_call);

int main() {
    dap_common_init("my_test", NULL);
    // Note: dap_mock_init() not needed - auto-initialized!
    
    // Configure mock to return 42
    DAP_MOCK_SET_RETURN(external_api_call, (void*)42);
    
    // Run code that calls external_api_call
    int result = my_code_under_test();
    
    // Verify mock was called once and returned correct value
    assert(DAP_MOCK_GET_CALL_COUNT(external_api_call) == 1);
    assert(result == 42);
    
    log_it(L_INFO, "[+] Test passed!");
    
    // Optional cleanup (if you need to reset mocks)
    // dap_mock_deinit();
    dap_common_deinit();
    return 0;
}
```

Update CMakeLists.txt:
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

# Option 1: Manual linking
target_link_libraries(my_test dap_test dap_core pthread)

# Option 2: Automatic linking of all SDK modules + test framework
# (recommended for complex tests)
dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test)

# Auto-generate --wrap linker flags
dap_mock_autowrap(my_test)

# If you need to mock functions in static libraries:
# dap_mock_autowrap_with_static(my_test dap_static_lib)
```

### 2.4 Universal Linking Function (RECOMMENDED)

To simplify working with tests, use `dap_link_all_sdk_modules()`:

**Simple test (minimal setup):**
```cmake
add_executable(simple_test simple_test.c)
dap_link_all_sdk_modules(simple_test DAP_INTERNAL_MODULES)
```

**Test with mocks (includes test framework):**
```cmake
add_executable(mock_test mock_test.c mock_wrappers.c)
dap_link_all_sdk_modules(mock_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test)
dap_mock_autowrap(mock_test)
```

**Test with additional libraries:**
```cmake
add_executable(complex_test complex_test.c)
dap_link_all_sdk_modules(complex_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test my_custom_lib)
```

**What does `dap_link_all_sdk_modules()` do:**
1. ✅ Links all object files from SDK modules
2. ✅ Automatically finds external dependencies (XKCP, Kyber, SQLite, PostgreSQL, MDBX)
3. ✅ Adds system libraries (pthread, rt, dl)
4. ✅ Links additional libraries from `LINK_LIBRARIES` parameter

**Benefits:**
- 🚀 One line instead of dozens of `target_link_libraries`
- 🔄 Automatic updates when adding new SDK modules
- ✅ Works with parallel builds (`make -j`)
- 🎯 Correct handling of transitive dependencies

\newpage

