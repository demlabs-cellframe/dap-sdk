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

# Простой способ: линковка одной библиотеки
target_link_libraries(my_test dap_core)

add_test(NAME my_test COMMAND my_test)
```

**Шаг 2 (альтернатива):** Автоматическая линковка всех модулей SDK

```cmake
add_executable(my_test my_test.c)

# Универсальный способ: автоматически линкует ВСЕ модули DAP SDK
# + все внешние зависимости (XKCP, Kyber, SQLite, PostgreSQL и т.д.)
dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES)

add_test(NAME my_test COMMAND my_test)
```

> **Преимущество:** `dap_link_all_sdk_modules()` автоматически подключает все модули SDK и их внешние зависимости. Не нужно перечислять десятки библиотек вручную!

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
# Подключите библиотеку test-framework (включает dap_test, dap_mock и т.д.)
target_link_libraries(my_test dap_test dap_core pthread)

# Или используйте универсальный способ (автоматически подключит dap_core + все зависимости):
# dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES LINK_LIBRARIES dap_test)
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
    // Примечание: dap_mock_init() не нужен - авто-инициализация!
    
    // Настройте мок на возврат 42
    DAP_MOCK_SET_RETURN(external_api_call, (void*)42);
    
    // Запустите код, который вызывает external_api_call
    int result = my_code_under_test();
    
    // Проверьте что мок был вызван один раз и вернул правильное значение
    assert(DAP_MOCK_GET_CALL_COUNT(external_api_call) == 1);
    assert(result == 42);
    
    log_it(L_INFO, "[+] Тест пройден!");
    
    // Опциональная очистка (если нужно сбросить моки)
    // dap_mock_deinit();
    dap_common_deinit();
    return 0;
}
```

Обновите CMakeLists.txt:
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../test-framework/mocks/DAPMockAutoWrap.cmake)

# Вариант 1: Ручная линковка
target_link_libraries(my_test dap_test dap_core pthread)

# Вариант 2: Автоматическая линковка всех SDK модулей + test framework
# (рекомендуется для комплексных тестов)
dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test)

# Автогенерация --wrap флагов линкера
dap_mock_autowrap(my_test)

# Если нужно мокировать функции в статических библиотеках:
# dap_mock_autowrap_with_static(my_test dap_static_lib)
```

### 2.4 Универсальная функция линковки (РЕКОМЕНДУЕТСЯ)

Для упрощения работы с тестами используйте `dap_link_all_sdk_modules()`:

**Простой тест (минимальный набор):**
```cmake
add_executable(simple_test simple_test.c)
dap_link_all_sdk_modules(simple_test DAP_INTERNAL_MODULES)
```

**Тест с моками (включает test framework):**
```cmake
add_executable(mock_test mock_test.c mock_wrappers.c)
dap_link_all_sdk_modules(mock_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test)
dap_mock_autowrap(mock_test)
```

**Тест с дополнительными библиотеками:**
```cmake
add_executable(complex_test complex_test.c)
dap_link_all_sdk_modules(complex_test DAP_INTERNAL_MODULES 
    LINK_LIBRARIES dap_test my_custom_lib)
```

**Что делает `dap_link_all_sdk_modules()`:**
1. ✅ Линкует все объектные файлы SDK модулей
2. ✅ Автоматически находит внешние зависимости (XKCP, Kyber, SQLite, PostgreSQL, MDBX)
3. ✅ Добавляет системные библиотеки (pthread, rt, dl)
4. ✅ Линкует дополнительные библиотеки из параметра `LINK_LIBRARIES`

**Преимущества:**
- 🚀 Одна строка вместо десятков `target_link_libraries`
- 🔄 Автоматическое обновление при добавлении новых SDK модулей
- ✅ Работает с параллельной сборкой (`make -j`)
- 🎯 Правильная обработка транзитивных зависимостей

\newpage
