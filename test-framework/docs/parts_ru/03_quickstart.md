## 2. Быстрый Старт

### 2.1 Первый тест (5 минут)

**Шаг 1:** Создайте файл теста

```c
// my_test.c
#include "dap_test.h"
#include "dap_common.h"

#define LOG_TAG "my_test"

int main() {
    dap_common_init("my_test", NULL);
    
    // Код теста
    int result = 2 + 2;
    dap_assert_PIF(result == 4, "Math should work");
    
    log_it(L_INFO, "[+] Тест пройден!");
    
    dap_common_deinit();
    return 0;
}
```

**Шаг 2:** Создайте CMakeLists.txt

```cmake
add_executable(my_test my_test.c)
target_link_libraries(my_test dap_core)
add_test(NAME my_test COMMAND my_test)
```

**Шаг 3:** Соберите и запустите

```bash
cd build
cmake ..
make my_test
./my_test
```

### 2.2 Добавление async таймаута (2 минуты)

```c
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_common.h"

#define LOG_TAG "my_test"
#define TIMEOUT_SEC 30

int main() {
    dap_common_init("my_test", NULL);
    
    // Добавьте глобальный таймаут
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TIMEOUT_SEC, "My Test")) {
        return 1;  // Таймаут сработал
    }
    
    // Ваши тесты здесь
    
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

Обновите CMakeLists.txt:
```cmake
target_link_libraries(my_test dap_test dap_core pthread)
```

### 2.3 Добавление моков (5 минут)

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_common.h"
#include <assert.h>

#define LOG_TAG "my_test"

// Объявите мок
DAP_MOCK_DECLARE(external_api_call);

int main() {
    dap_common_init("my_test", NULL);
    dap_mock_init();
    
    // Настройте мок на возврат 42
    DAP_MOCK_SET_RETURN(external_api_call, (void*)42);
    
    // Запустите код, который вызывает external_api_call
    int result = my_code_under_test();
    
    // Проверьте что мок был вызван один раз и вернул правильное значение
    assert(DAP_MOCK_GET_CALL_COUNT(external_api_call) == 1);
    assert(result == 42);
    
    log_it(L_INFO, "[+] Тест пройден!");
    
    dap_mock_deinit();
    dap_common_deinit();
    return 0;
}
```

Обновите CMakeLists.txt:
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

target_link_libraries(my_test dap_test dap_core pthread)

# Автогенерация --wrap флагов линкера
dap_mock_autowrap(my_test)
```

\newpage
