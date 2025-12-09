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

// Declare mock (RECOMMENDED: simple and clean)
DAP_MOCK(external_api_call);

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

**Update CMakeLists.txt:**

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(my_test my_test.c)

# Step 1: Link all SDK modules as STATIC libraries
dap_test_link_libraries(my_test)

# Step 2: Add all necessary include directories  
dap_test_add_includes(my_test)

# Step 3: Enable automatic mocking (scans sources, wraps libraries, done!)
dap_mock_autowrap(my_test)
```

**What happens automatically:**
1. ✅ All SDK modules linked as **STATIC libraries** (`*_static.a`) - required for `--wrap`
2. ✅ `--wrap` flags generated for all `DAP_MOCK_DECLARE` functions
3. ✅ Static libraries **automatically wrapped** with `--whole-archive`
4. ✅ External dependencies (sqlite3, json-c, ssl) linked transitively
5. ✅ All include directories added

**Why STATIC libraries?**
- `--wrap` only works with static libraries (`.a`), NOT object files (`.o`)
- Object files linked directly have resolved symbols - no way to intercept calls

### 2.4 Universal Test Setup (RECOMMENDED)

**Complete minimal test (4 lines of CMake):**
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(my_test my_test.c)
dap_test_link_libraries(my_test)  # Links all SDK as STATIC libraries
dap_test_add_includes(my_test)    # Adds all includes  
dap_mock_autowrap(my_test)        # Automatic mocking!
```

**What does `dap_test_link_libraries()` do:**
1. ✅ Links all SDK modules as **STATIC libraries** (`*_static.a`) - required for `--wrap`
2. ✅ Automatically propagates dependencies between modules with `_static` suffix
3. ✅ Finds and links external libraries (XKCP, Kyber, SQLite, PostgreSQL, MDBX, json-c)
4. ✅ Links test framework library (`libdap_test.a`)
5. ✅ All constructors are called automatically

**What does `dap_mock_autowrap()` do:**
1. ✅ Scans test sources for `DAP_MOCK_DECLARE` patterns
2. ✅ Generates `-Wl,--wrap=function_name` for each mocked function
3. ✅ **Automatically detects** all `*_static.a` libraries
4. ✅ **Automatically wraps** them with `--whole-archive` and `--start-group`
5. ✅ Adds `--allow-multiple-definition` for duplicate symbols

**Benefits:**
- 🚀 **3-4 lines** instead of dozens of `target_link_libraries` and manual `--wrap` configuration
- 🔄 **Automatic updates** when adding new SDK modules
- ✅ **Proper `--wrap` support** for mocking internal calls between modules
- 🎯 **Correct handling** of transitive dependencies
- 🧪 **Automatic `--whole-archive`** wrapping - no manual configuration
- 📦 **Static libraries** created automatically from object libraries

\newpage

