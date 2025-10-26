# DAP SDK Mock Auto-Wrapper System

Автоматическая система генерации linker wrapping конфигураций для мокирования функций в тестах.

## Обзор

Эта система **полностью автоматизирует** процесс настройки мокирования функций через `--wrap` линкера:

1. **Сканирует** тест на `DAP_MOCK_DECLARE()` вызовы
2. **Генерирует** файл с `--wrap` опциями для линкера
3. **Создаёт** CMake фрагмент для интеграции
4. **Подсказывает** что нужно добавить (wrapper templates)

**Преимущества:**
- ✅ Не нужно вручную писать `-Wl,--wrap=` для каждой функции
- ✅ Автоматическое обнаружение всех моков
- ✅ Кроссплатформенность (Linux/macOS через bash, Windows через PowerShell)
- ✅ Никаких зависимостей (только стандартные утилиты)
- ✅ Генерирует шаблоны для недостающих wrappers

## Структура

```
dap-sdk/test-framework/mocks/
├── dap_mock_autowrap.sh       # Bash версия (Linux/macOS)
├── dap_mock_autowrap.ps1      # PowerShell версия (Windows)
├── dap_mock_autowrap.py       # Python версия (опциональная, для CI/CD)
└── DAPMockAutoWrap.cmake      # CMake интеграция
```

## Быстрый старт

### Вариант 1: CMake функция (рекомендуется)

```cmake
# В tests/unit/CMakeLists.txt

# Подключаем модуль
include(${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

# Создаём тест
add_executable(test_vpn_tun test_vpn_tun.c)

# АВТОМАТИЧЕСКИ генерируем и применяем --wrap опции
dap_mock_autowrap(test_vpn_tun test_vpn_tun.c)

target_link_libraries(test_vpn_tun
    cellframe_srv_vpn_client_lib
    dap_test_mocks
    dap_core
)
```

Готово! При сборке CMake автоматически:
- Запустит `dap_mock_autowrap.sh` (или `.ps1` на Windows)
- Сгенерирует `test_vpn_tun_wrap.txt` с опциями
- Применит их к target

### Вариант 2: Ручная генерация

```bash
# Linux/macOS
./dap_mock_autowrap.sh test_my_module.c [output_dir]

# Windows PowerShell
.\dap_mock_autowrap.ps1 test_my_module.c [output_dir]

# Python (если установлен)
python3 dap_mock_autowrap.py test_my_module.c [output_dir]
```

Сгенерируется:
- `test_my_module_wrap.txt` - опции линкера
- `test_my_module_mocks.cmake` - CMake фрагмент
- `test_my_module_wrappers_template.c` - шаблон для недостающих wrappers

Использование в CMake:
```cmake
# Вариант A: Include сгенерированного фрагмента
include(test_my_module_mocks.cmake)

# Вариант B: Использовать функцию с файлом
dap_mock_wrap_from_file(test_my_module test_my_module_wrap.txt)

# Вариант C: Ручное указание
target_link_options(test_my_module PRIVATE
    -Wl,--wrap=dap_stream_write
    -Wl,--wrap=dap_net_tun_create
    # ... все функции из wrap.txt
)
```

## CMake API

### dap_mock_autowrap(target source)
Автоматическая генерация и применение wrapping

```cmake
add_executable(test_module test_module.c)
dap_mock_autowrap(test_module test_module.c)
```

### dap_mock_wrap_from_file(target wrap_file)
Применить wrap опции из текстового файла

```cmake
dap_mock_wrap_from_file(test_module mocks/module_wraps.txt)
```

**Оптимизация:** На GCC/Clang использует `-Wl,@file` для прямой передачи файла линкеру (эффективнее, чем парсинг).

Формат `wrap_file` (одна функция на строку):
```
# Комментарии начинаются с #
dap_stream_write
dap_net_tun_create
dap_config_get_item_str

# Можно с префиксом или без:
-Wl,--wrap=dap_client_connect
```

### dap_mock_manual_wrap(target func1 func2 ...)
Ручное указание функций для wrapping

```cmake
dap_mock_manual_wrap(test_module
    dap_stream_write
    dap_net_tun_create
    dap_config_get_item_str
)
```

## Пример использования

### 1. Тестовый файл с моками

```c
// test_vpn_tun.c
#include "dap_mock_framework.h"
#include "dap_mock_linker_wrapper.h"

// Объявляем моки
DAP_MOCK_DECLARE(dap_net_tun_create);
DAP_MOCK_DECLARE(dap_net_tun_destroy);
DAP_MOCK_DECLARE(vpn_client_routing_init);

// Создаём wrappers
DAP_MOCK_WRAPPER_PTR(dap_net_tun_create,
    (const char *a_name, dap_net_tun_flags_t a_flags),
    (a_name, a_flags))

DAP_MOCK_WRAPPER_INT(dap_net_tun_destroy,
    (dap_net_tun_t *a_tun),
    (a_tun))

DAP_MOCK_WRAPPER_PTR(vpn_client_routing_init,
    (void),
    ())

// Тесты...
void test_tun_create(void) {
    dap_mock_framework_init();
    
    g_mock_dap_net_tun_create = dap_mock_register("dap_net_tun_create");
    dap_mock_set_enabled(g_mock_dap_net_tun_create, true);
    DAP_MOCK_SET_RETURN_PTR(dap_net_tun_create, (void*)0xDEADBEEF);
    
    // Вызов функции - линкер перенаправит на наш wrapper!
    void *result = dap_net_tun_create("tun0", 0);
    
    assert(result == (void*)0xDEADBEEF);
    assert(dap_mock_get_call_count(g_mock_dap_net_tun_create) == 1);
    
    dap_mock_framework_deinit();
}
```

### 2. Генерация конфигурации

```bash
$ ./dap_mock_autowrap.sh test_vpn_tun.c
============================================================
DAP SDK Mock Auto-Wrapper Generator (Bash)
============================================================
📋 Scanning test_vpn_tun.c for mock declarations...
✅ Found 3 mock declarations:
   - dap_net_tun_create
   - dap_net_tun_destroy
   - vpn_client_routing_init
📋 Scanning for wrapper definitions...
   ✅ dap_net_tun_create: wrapper found
   ✅ dap_net_tun_destroy: wrapper found
   ✅ vpn_client_routing_init: wrapper found
📝 Generating linker response file: test_vpn_tun_wrap.txt
✅ Generated 3 --wrap options
📝 Generating CMake integration: test_vpn_tun_mocks.cmake
✅ Generated CMake integration
✅ All wrappers are defined, no template needed
============================================================
✅ Generation Complete!
============================================================

Generated files:
  📄 test_vpn_tun_wrap.txt
  📄 test_vpn_tun_mocks.cmake
```

### 3. Использование в CMake

```cmake
# tests/unit/CMakeLists.txt

add_executable(test_vpn_tun test_vpn_tun.c)

# Вариант A: Автоматическая генерация при сборке
dap_mock_autowrap(test_vpn_tun test_vpn_tun.c)

# Вариант B: Использовать уже сгенерированный файл
# include(test_vpn_tun_mocks.cmake)

# Вариант C: Загрузить из wrap файла
# dap_mock_wrap_from_file(test_vpn_tun test_vpn_tun_wrap.txt)

target_link_libraries(test_vpn_tun
    vpn_lib
    dap_test_mocks
    dap_core
)
```

## Генерируемые файлы

### test_module_wrap.txt
Список `-Wl,--wrap=` опций для линкера:
```
-Wl,--wrap=dap_net_tun_create
-Wl,--wrap=dap_net_tun_destroy
-Wl,--wrap=vpn_client_routing_init
```

### test_module_mocks.cmake
CMake фрагмент для применения wrapping:
```cmake
# Auto-generated mock configuration
set(TEST_MODULE_WRAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/test_module_wrap.txt)
file(READ ${TEST_MODULE_WRAP_FILE} TEST_MODULE_WRAP_OPTIONS)
string(REPLACE "\n" ";" TEST_MODULE_WRAP_LIST "${TEST_MODULE_WRAP_OPTIONS}")
target_link_options(test_module PRIVATE ${TEST_MODULE_WRAP_LIST})
```

### test_module_wrappers_template.c
Шаблон для функций без wrappers:
```c
// Auto-generated wrapper templates
#include "dap_mock_framework.h"
#include "dap_mock_linker_wrapper.h"

// TODO: Define wrapper for missing_function
// Example for int return:
// DAP_MOCK_WRAPPER_INT(missing_function,
//     (type1 a_param1, type2 a_param2),
//     (a_param1, a_param2))
```

## Workflow

```
1. Пишешь тест с DAP_MOCK_DECLARE()
      ↓
2. Запускаешь dap_mock_autowrap.sh (или CMake это сделает)
      ↓
3. Скрипт сканирует и генерирует wrap файлы
      ↓
4. CMake применяет -Wl,--wrap= опции
      ↓
5. Линкер подменяет функции на __wrap_ версии
      ↓
6. Твои wrappers контролируют поведение!
```

## Требования

### Поддерживаемые компиляторы и линкеры

| Компилятор | Линкер | Платформа | Поддержка `--wrap` | Синтаксис |
|------------|--------|-----------|-------------------|-----------|
| **GCC** | GNU ld | Linux | ✅ Полная | `-Wl,--wrap=func` |
| **GCC** | GNU ld | macOS | ✅ Полная | `-Wl,--wrap=func` |
| **Clang** | GNU ld | Linux | ✅ Полная | `-Wl,--wrap=func` |
| **Clang** | LLD | Linux/macOS | ✅ Полная | `-Wl,--wrap=func` |
| **MinGW-w64** | GNU ld | Windows | ✅ Полная | `-Wl,--wrap=func` |
| **Clang-cl** | LLD | Windows | ✅ Полная | `-Wl,--wrap=func` |
| **MSVC** | link.exe | Windows | ❌ **НЕТ** | `/ALTERNATENAME` (несовместимо) |

**Рекомендации:**
- ✅ **Linux**: используйте GCC или Clang (стандартная установка)
- ✅ **macOS**: используйте Clang (встроенный Xcode)
- ✅ **Windows**: используйте MinGW-w64 или Clang (НЕ MSVC!)
- ⚠️ **MSVC**: не поддерживает `--wrap`, используйте MinGW для тестов

**CMake автоматически определит компилятор** и выдаст предупреждение при MSVC:
```cmake
# При использовании MSVC:
message(WARNING "MSVC does not support --wrap. Please use MinGW/Clang for mock testing.")
```

### Другие требования

- **Linux/macOS**: bash (обычно уже установлен)
- **Windows**: PowerShell 5.0+ (встроен в Windows 10/11)
- **Опционально**: Python 3.6+ (для CI/CD систем)
- **Линкер**: GNU ld или LLD с поддержкой `--wrap`

## Ограничения

### Линкер
- Работает только с GNU ld или LLD (поддержка `--wrap`)
- **MSVC link.exe НЕ поддерживается** - используйте MinGW/Clang на Windows
- Нужно создавать wrapper макросы вручную (но скрипт генерирует шаблон)

### Функциональность
- Maximum 100 tracked calls per function (DAP_MOCK_MAX_CALLS)
- Maximum 10 arguments per function call
- Arguments stored as `void*` (need casting for verification)

## Интеграция в CI/CD

```yaml
# GitHub Actions
- name: Generate mock configurations
  run: |
    find tests -name "test_*.c" -exec \
      ./dap-sdk/test-framework/mocks/dap_mock_autowrap.sh {} \;

# GitLab CI
generate_mocks:
  script:
    - for test in tests/unit/test_*.c; do
        ./dap-sdk/test-framework/mocks/dap_mock_autowrap.sh "$test"
      done
```

## Troubleshooting

### "No mock declarations found"
- Проверь что используешь `DAP_MOCK_DECLARE(func_name)` в тесте
- Проверь синтаксис: `DAP_MOCK_DECLARE(function_name);` (без пробелов внутри)

### "Bash/PowerShell not found"
- Linux: `sudo apt install bash` или `yum install bash`
- Windows: PowerShell встроен, проверь `$env:PATH`

### Wrapper не применяется
- Проверь что добавил `-Wl,--wrap=` в link options
- Проверь что wrapper функция определена (создай её из template)
- Убедись что используешь GNU ld (`ld --version`)

### Функция вызывает реальную версию вместо мока
- Убедись что `dap_mock_set_enabled(..., true)` вызван
- Проверь что wrapper правильно определён (тип возврата, параметры)
- Проверь что линкер применил `--wrap` (посмотри linker command line)

## См. также

- `README.md` - Документация mock framework
- `dap_mock_linker_wrapper.h` - Wrapper макросы
- `test_mock_linker_example.c` - Полный пример использования
- GNU ld manual - https://sourceware.org/binutils/docs/ld/Options.html

