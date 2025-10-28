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
    
    log_it(L_INFO, "✓ Test passed!");
    
    dap_common_deinit();
    return 0;
}
```

**Step 2:** Create CMakeLists.txt

```cmake
add_executable(my_test my_test.c)
target_link_libraries(my_test dap_core)
add_test(NAME my_test COMMAND my_test)
```

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
target_link_libraries(my_test dap_test dap_core pthread)
```

### 2.3 Adding Mocks (5 minutes)

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_common.h"

#define LOG_TAG "my_test"

// Declare mock
DAP_MOCK_DECLARE(external_api_call);

int main() {
    dap_common_init("my_test", NULL);
    dap_mock_init();
    
    // Configure mock
    DAP_MOCK_SET_RETURN(external_api_call, (void*)42);
    
    // Run code that calls external_api_call
    int result = my_code_under_test();
    
    // Verify
    assert(DAP_MOCK_GET_CALL_COUNT(external_api_call) == 1);
    
    dap_mock_deinit();
    dap_common_deinit();
    return 0;
}
```

Update CMakeLists.txt:
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

target_link_libraries(my_test dap_test dap_core pthread)

# Auto-generate --wrap linker flags
dap_mock_autowrap(my_test)
```

\newpage

