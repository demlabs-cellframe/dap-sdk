# DAP Test Framework Module (dap_test.h)

## Обзор

Модуль `dap_test_framework` предоставляет унифицированную систему тестирования для DAP SDK. Он включает в себя:

- **Макросы для unit-тестов** - удобные assertion функции
- **Бенчмарки производительности** - измерение времени выполнения
- **Генераторы тестовых данных** - создание случайных данных для тестирования
- **Цветной вывод результатов** - визуальная индикация статуса тестов
- **Автоматизированное тестирование** - интеграция с CI/CD системами

## Архитектурная роль

Test Framework является неотъемлемой частью QA-процесса DAP SDK:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK       │───▶│ Test Framework  │
│   Модули        │    └─────────────────┘
└─────────────────┘             │
         │                     │
    ┌────▼────┐           ┌────▼────┐
    │Unit     │           │Benchmarks│
    │тесты    │           │& Perf.   │
    └─────────┘           └─────────┘
         │                     │
    ┌────▼────┐           ┌────▼────┐
    │CI/CD     │◄──────────►│Отчеты    │
    │интеграция│           │результатов│
    └─────────┘           └─────────┘
```

## Основные компоненты

### 1. Основные макросы тестирования

#### `dap_assert(expr, testname)`
```c
#define dap_assert(expr, testname) { \
    if(expr) { \
        printf("\t%s%s PASS.%s\n", TEXT_COLOR_GRN, testname, TEXT_COLOR_RESET); \
        fflush(stdout); \
    } else { \
        printf("\t%s%s FAILED!%s\n", TEXT_COLOR_RED, testname, TEXT_COLOR_RESET); \
        fflush(stdout); \
        abort(); } }
```

**Параметры:**
- `expr` - логическое выражение для проверки
- `testname` - имя теста для вывода

**Пример:**
```c
dap_assert(result == expected, "Basic arithmetic test");
```

#### `dap_assert_PIF(expr, msg)`
```c
#define dap_assert_PIF(expr, msg) { \
    if(expr) {} \
    else { \
    printf("\t%s%s FAILED!%s\n", TEXT_COLOR_RED, msg, TEXT_COLOR_RESET); \
    fflush(stdout); \
    abort(); } }
```

**Особенности:**
- PIF = "Print If Failed" - вывод только при ошибке
- Подходит для проверок в циклах

**Пример:**
```c
for (int i = 0; i < 1000; i++) {
    int result = complex_calculation(i);
    dap_assert_PIF(result > 0, "Complex calculation failed");
}
```

### 2. Вспомогательные макросы

#### `dap_test_msg(...)`
```c
#define dap_test_msg(...) { \
    printf("\t%s", TEXT_COLOR_WHT); \
    printf(__VA_ARGS__); \
    printf("%s\n", TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Назначение:**
- Вывод отладочной информации во время тестирования
- Не влияет на результат теста

**Пример:**
```c
dap_test_msg("Processing item %d of %d", current, total);
```

#### `dap_pass_msg(testname)`
```c
#define dap_pass_msg(testname) { \
    printf("\t%s%s PASS.%s\n", TEXT_COLOR_GRN, testname, TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Назначение:**
- Ручная отметка успешного прохождения теста
- Для случаев, когда автоматическая проверка невозможна

#### `dap_fail(msg)`
```c
#define dap_fail(msg) {\
    printf("\t%s%s!%s\n", TEXT_COLOR_RED, msg, TEXT_COLOR_RESET); \
    fflush(stdout); \
    abort();}
```

**Назначение:**
- Немедленное завершение теста с ошибкой
- Для критических сбоев, требующих остановки

### 3. Макросы для модулей

#### `dap_print_module_name(module_name)`
```c
#define dap_print_module_name(module_name) { \
    printf("%s%s passing the tests... %s\n", TEXT_COLOR_CYN, module_name, TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Назначение:**
- Вывод заголовка для группы тестов модуля
- Визуальное разделение тестов по модулям

**Пример:**
```c
dap_print_module_name("Crypto Module Tests");
dap_assert(test_sha3(), "SHA3 hash function");
dap_assert(test_aes_encrypt(), "AES encryption");
```

### 4. Утилиты для строк

#### `dap_str_equals(str1, str2)`
```c
#define dap_str_equals(str1, str2) strcmp(str1, str2) == 0
```

#### `dap_strn_equals(str1, str2, count)`
```c
#define dap_strn_equals(str1, str2, count) strncmp(str1, str2, count) == 0
```

**Назначение:**
- Удобные функции сравнения строк
- Интеграция с макросами тестирования

**Пример:**
```c
dap_assert(dap_str_equals(result, "expected"), "String comparison");
```

## Система бенчмаркинга

### Функции измерения времени

#### `benchmark_test_time()`
```c
int benchmark_test_time(void (*func_name)(void), int repeat);
```

**Параметры:**
- `func_name` - функция для тестирования
- `repeat` - количество повторений

**Возвращаемое значение:**
- Время выполнения в миллисекундах

**Пример:**
```c
int time_ms = benchmark_test_time(my_function, 1000);
benchmark_mgs_time("My function performance", time_ms);
```

#### `benchmark_test_rate()`
```c
float benchmark_test_rate(void (*func_name)(void), float sec);
```

**Параметры:**
- `func_name` - функция для тестирования
- `sec` - минимальное время выполнения в секундах

**Возвращаемое значение:**
- Количество вызовов в секунду (rate)

**Пример:**
```c
float rate = benchmark_test_rate(my_function, 2.0);
benchmark_mgs_rate("My function throughput", rate);
```

### Функции вывода результатов

#### `benchmark_mgs_time()`
```c
void benchmark_mgs_time(const char *text, int dt);
```

**Вывод:**
- "Operation completed in 150 msec."
- "Operation completed in 2.45 sec."

#### `benchmark_mgs_rate()`
```c
void benchmark_mgs_rate(const char *test_name, float rate);
```

**Вывод:**
- "My function throughput: 1500 times/sec."
- "Data processing: 45.67 times/sec."

## Генератор тестовых данных

### `generate_random_byte_array()`
```c
void generate_random_byte_array(uint8_t* array, const size_t size);
```

**Параметры:**
- `array` - указатель на массив для заполнения
- `size` - размер массива в байтах

**Назначение:**
- Генерация случайных данных для тестирования
- Инициализация массивов случайными значениями

**Пример:**
```c
#define TEST_DATA_SIZE 1024
uint8_t test_data[TEST_DATA_SIZE];
generate_random_byte_array(test_data, TEST_DATA_SIZE);
```

## Цветовая схема вывода

```c
#define TEXT_COLOR_RED   "\x1B[31m"  // Красный - ошибки, провалы
#define TEXT_COLOR_GRN   "\x1B[32m"  // Зеленый - успехи
#define TEXT_COLOR_YEL   "\x1B[33m"  // Желтый - предупреждения
#define TEXT_COLOR_BLU   "\x1B[34m"  // Синий - информация
#define TEXT_COLOR_MAG   "\x1B[35m"  // Магента - заголовки
#define TEXT_COLOR_CYN   "\x1B[36m"  // Циан - модули
#define TEXT_COLOR_WHT   "\x1B[37m"  // Белый - отладка
#define TEXT_COLOR_RESET "\x1B[0m"   // Сброс цвета
```

## Функции времени

### `get_cur_time_msec()`
```c
int get_cur_time_msec(void);
```

**Возвращаемое значение:**
- Текущее время в миллисекундах

### `get_cur_time_nsec()`
```c
uint64_t get_cur_time_nsec(void);
```

**Возвращаемое значение:**
- Текущее время в наносекундах

**Назначение:**
- Точное измерение времени выполнения
- Синхронизация тестов
- Создание уникальных идентификаторов

## Структура тестовой функции

### Базовый шаблон теста

```c
void test_my_function() {
    // Подготовка данных
    int input = 42;
    int expected = 84;

    // Выполнение тестируемой функции
    int result = my_function(input);

    // Проверка результата
    dap_assert(result == expected, "My function basic test");
}

void test_my_function_edge_cases() {
    // Тестирование граничных случаев
    dap_assert(my_function(0) == 0, "Zero input");
    dap_assert(my_function(INT_MAX) == INT_MAX * 2, "Max int overflow");
    dap_assert(my_function(INT_MIN) == INT_MIN * 2, "Min int overflow");
}

void test_my_function_performance() {
    // Бенчмаркинг производительности
    int time_ms = benchmark_test_time([]() {
        volatile int result = my_function(1000);
        (void)result; // Подавление предупреждения
    }, 1000);

    benchmark_mgs_time("My function performance", time_ms);
    dap_assert(time_ms < 100, "Performance test");
}
```

### Тест модуля

```c
void run_my_module_tests() {
    dap_print_module_name("My Module Tests");

    test_my_function();
    test_my_function_edge_cases();
    test_my_function_performance();

    // Дополнительные тесты
    test_my_function_memory_leaks();
    test_my_function_concurrency();
}
```

## Интеграция с CI/CD

### Автоматический запуск тестов

```bash
# В Makefile или CMakeLists.txt
test:
    ./run_tests
    @echo "Tests completed with code: $$?"

# В CI скрипте
run_tests:
    @echo "Running DAP SDK tests..."
    @make test
    @if [ $$? -ne 0 ]; then \
        echo "Tests failed!"; \
        exit 1; \
    fi
```

### Парсинг результатов

```python
# parse_test_results.py
import re

def parse_test_output(output):
    results = {
        'passed': 0,
        'failed': 0,
        'modules': []
    }

    for line in output.split('\n'):
        if 'PASS.' in line:
            results['passed'] += 1
        elif 'FAILED!' in line:
            results['failed'] += 1
        elif line.startswith('\x1B[36m'):  # Циан - модуль
            module = re.search(r'\x1B\[36m(.+?)\x1B\[0m', line)
            if module:
                results['modules'].append(module.group(1))

    return results
```

## Лучшие практики тестирования

### 1. Структура тестов

```c
// test_my_module.c
#include "dap_test.h"
#include "my_module.h"

// Тесты основных функций
void test_basic_functionality() {
    // Тестирование основного функционала
}

// Тесты граничных случаев
void test_edge_cases() {
    // Тестирование крайних значений
}

// Тесты производительности
void test_performance() {
    // Измерение производительности
}

// Тесты памяти
void test_memory_usage() {
    // Проверка утечек памяти
}

// Основная функция запуска тестов
int main() {
    dap_print_module_name("My Module");

    test_basic_functionality();
    test_edge_cases();
    test_performance();
    test_memory_usage();

    return 0;
}
```

### 2. Использование генераторов данных

```c
void test_with_random_data() {
    const size_t TEST_SIZE = 1000;
    uint8_t test_data[TEST_SIZE];

    // Генерация случайных данных
    generate_random_byte_array(test_data, TEST_SIZE);

    // Тестирование с различными входными данными
    for (size_t i = 0; i < 100; i++) {
        size_t offset = rand() % (TEST_SIZE - 10);
        process_data(&test_data[offset], 10);
        dap_assert_PIF(validate_result(), "Random data processing");
    }
}
```

### 3. Бенчмаркинг

```c
void comprehensive_benchmark() {
    printf("Running comprehensive benchmark...\n");

    // Тест на время
    int time_ms = benchmark_test_time(test_function, 10000);
    benchmark_mgs_time("10k iterations", time_ms);

    // Тест на пропускную способность
    float rate = benchmark_test_rate(test_function, 5.0);
    benchmark_mgs_rate("Throughput test", rate);

    // Сравнение с эталоном
    dap_assert(time_ms < BASELINE_TIME, "Performance regression check");
}
```

### 4. Обработка ошибок

```c
void test_error_conditions() {
    // Тестирование корректной обработки ошибок
    dap_assert(my_function(NULL) == ERROR_INVALID_PARAM,
               "NULL parameter handling");

    dap_assert(my_function("") == ERROR_EMPTY_STRING,
               "Empty string handling");

    // Тестирование восстановления после ошибок
    int result = my_function("valid_input");
    dap_assert(result == SUCCESS, "Recovery after error");
}
```

## Отладка и диагностика

### Включение детального вывода

```c
// В начале теста
dap_test_msg("Starting test with parameters: input=%d, expected=%d",
             input, expected);

// Во время выполнения
for (int i = 0; i < iterations; i++) {
    if (i % 100 == 0) {
        dap_test_msg("Progress: %d/%d iterations", i, iterations);
    }
    // ... тестовая логика
}
```

### Создание отчетов

```c
void generate_test_report() {
    printf("\n=== Test Report ===\n");
    printf("Total tests run: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", failed_tests);
    printf("Success rate: %.2f%%\n",
           (float)passed_tests / total_tests * 100);

    if (failed_tests > 0) {
        printf("\nFailed tests:\n");
        for (int i = 0; i < failed_tests; i++) {
            printf("- %s\n", failed_test_names[i]);
        }
    }
}
```

## Интеграция с другими модулями

### DAP Common
- Использование общих структур данных
- Работа с памятью и строками
- Логирование результатов

### DAP Config
- Загрузка конфигурации тестов
- Параметризация тестовых сценариев
- Настройка условий тестирования

### DAP Time
- Синхронизация тестов
- Измерение интервалов времени
- Создание таймаутов

## Типичные проблемы

### 1. Нестабильные тесты

```c
// Проблема: тест зависит от внешних факторов
void unstable_test() {
    int result = network_call(); // Может зависеть от сети
    dap_assert(result == SUCCESS, "Network test"); // Нестабильный
}

// Решение: изоляция тестов
void stable_test() {
    // Mock или stub для network_call
    mock_network_response(SUCCESS);
    int result = network_call();
    dap_assert(result == SUCCESS, "Network test");
}
```

### 2. Утечки памяти

```c
// Проблема: не освобождается память
void memory_leak_test() {
    for (int i = 0; i < 1000; i++) {
        char *data = malloc(1024);
        process_data(data);
        // Забыли free(data)!
    }
}

// Решение: правильное управление памятью
void fixed_memory_test() {
    for (int i = 0; i < 1000; i++) {
        char *data = malloc(1024);
        process_data(data);
        free(data); // Освобождаем память
    }
}
```

### 3. Гонка данных

```c
// Проблема: одновременный доступ к shared ресурсам
static int shared_counter = 0;

void concurrent_test() {
    shared_counter++;
    dap_assert(shared_counter == 1, "Counter test"); // Гонка!
}

// Решение: синхронизация
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void safe_concurrent_test() {
    pthread_mutex_lock(&mutex);
    shared_counter++;
    dap_assert(shared_counter == 1, "Counter test");
    pthread_mutex_unlock(&mutex);
}
```

## Заключение

Модуль `dap_test_framework` предоставляет полный набор инструментов для тестирования DAP SDK. Его простота использования в сочетании с мощными возможностями бенчмаркинга и генерации тестовых данных делает его идеальным инструментом для обеспечения качества и производительности распределенных приложений.
