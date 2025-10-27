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
    
    log_it(L_INFO, "✓ Тест пройден!");
    
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

\newpage
