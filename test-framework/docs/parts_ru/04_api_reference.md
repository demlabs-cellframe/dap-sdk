## 3. Справочник API

### 3.1 Async Testing API

#### Глобальный таймаут
```c
int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);
// Возвращает: 0 при настройке, 1 если таймаут сработал

void dap_test_cancel_global_timeout(void);
```

#### Опрос условий
```c
bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);
// Возвращает: true если условие выполнено, false при таймауте
// 
// Сигнатура callback:
// typedef bool (*dap_test_condition_cb_t)(void *a_user_data);
//
// Структура конфигурации:
// typedef struct {
//     uint32_t timeout_ms;          // Макс. время ожидания (мс)
//     uint32_t poll_interval_ms;    // Интервал опроса (мс)
//     bool fail_on_timeout;         // abort() при таймауте?
//     const char *operation_name;   // Для логирования
// } dap_test_async_config_t;
//
// Дефолтная конфигурация: DAP_TEST_ASYNC_CONFIG_DEFAULT
//   - timeout_ms: 5000 (5 секунд)
//   - poll_interval_ms: 100 (100 мс)
//   - fail_on_timeout: true
//   - operation_name: "async operation"
```

#### pthread хелперы
```c
void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);
bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);
void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);
```

#### Утилиты времени
```c
uint64_t dap_test_get_time_ms(void);  // Монотонное время в мс
void dap_test_sleep_ms(uint32_t a_delay_ms);  // Кроссплатформенный sleep
```

#### Макросы
```c
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg)
// Быстрое ожидание условия
```

### 3.2 Mock Framework API

**Заголовочный файл:** `dap_mock.h`

#### Инициализация фреймворка
```c
int dap_mock_init(void);
// Опционально: переинициализация мок-фреймворка (авто-инициализируется через конструктор)
// Возвращает: 0 при успехе
// Примечание: Фреймворк авто-инициализируется до main(), ручной вызов не требуется

void dap_mock_deinit(void);
// Очистка мок-фреймворка (вызывать в teardown при необходимости)
// Примечание: Также авто-деинициализирует async систему если она была включена
```

#### Макросы объявления моков

**Простое объявление (авто-включено, возврат 0):**
```c
DAP_MOCK_DECLARE(function_name);
```

**С конфигурационной структурой:**
```c
DAP_MOCK_DECLARE(function_name, {
    .enabled = true,
    .return_value.l = 0xDEADBEEF,
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 1000
    }
});
```

**Со встроенным callback:**
```c
DAP_MOCK_DECLARE(function_name, {.return_value.i = 0}, {
    // Тело callback - пользовательская логика для каждого вызова
    if (a_arg_count >= 1) {
        int arg = (int)(intptr_t)a_args[0];
        return (void*)(intptr_t)(arg * 2);  // Удваиваем входное значение
    }
    return (void*)0;
});
```

**Для пользовательской обертки (без авто-генерации):**
```c
DAP_MOCK_DECLARE_CUSTOM(function_name, {
    .delay = {
        .type = DAP_MOCK_DELAY_VARIANCE,
        .variance = {.center_us = 100000, .variance_us = 50000}
    }
});
```

#### Конфигурационные структуры

**dap_mock_config_t:**
```c
typedef struct dap_mock_config {
    bool enabled;                      // Включить/выключить мок
    dap_mock_return_value_t return_value;  // Возвращаемое значение
    dap_mock_delay_t delay;            // Задержка выполнения
} dap_mock_config_t;

// По умолчанию: enabled=true, return=0, без задержки
#define DAP_MOCK_CONFIG_DEFAULT { \
    .enabled = true, \
    .return_value = {0}, \
    .delay = {.type = DAP_MOCK_DELAY_NONE} \
}
```

**dap_mock_return_value_t:**
```c
typedef union dap_mock_return_value {
    int i;         // Для int, bool, малых типов
    long l;        // Для указателей (приведение через intptr_t)
    uint64_t u64;  // Для uint64_t, size_t (64-бит)
    void *ptr;     // Для void*, общих указателей
    char *str;     // Для char*, строк
} dap_mock_return_value_t;
```

**dap_mock_delay_t:**
```c
typedef enum {
    DAP_MOCK_DELAY_NONE,      // Без задержки
    DAP_MOCK_DELAY_FIXED,     // Фиксированная задержка
    DAP_MOCK_DELAY_RANGE,     // Случайная в [min, max]
    DAP_MOCK_DELAY_VARIANCE   // Центр ± разброс
} dap_mock_delay_type_t;

typedef struct dap_mock_delay {
    dap_mock_delay_type_t type;
    union {
        uint64_t fixed_us;
        struct { uint64_t min_us; uint64_t max_us; } range;
        struct { uint64_t center_us; uint64_t variance_us; } variance;
    };
} dap_mock_delay_t;
```

#### Макросы управления
```c
DAP_MOCK_ENABLE(func_name)
// Включить мок (перехват вызовов)
// Пример: DAP_MOCK_ENABLE(dap_stream_write);

DAP_MOCK_DISABLE(func_name)
// Выключить мок (вызов реальной функции)
// Пример: DAP_MOCK_DISABLE(dap_stream_write);

DAP_MOCK_RESET(func_name)
// Сбросить историю вызовов и статистику
// Пример: DAP_MOCK_RESET(dap_stream_write);

DAP_MOCK_SET_RETURN(func_name, value)
// Установить возвращаемое значение (приведение через (void*) или (void*)(intptr_t))
// Пример: DAP_MOCK_SET_RETURN(dap_stream_write, (void*)(intptr_t)42);

DAP_MOCK_GET_CALL_COUNT(func_name)
// Получить количество вызовов мока (возвращает int)
// Пример: int count = DAP_MOCK_GET_CALL_COUNT(dap_stream_write);

DAP_MOCK_WAS_CALLED(func_name)
// Возвращает true если был вызван хотя бы раз (возвращает bool)
// Пример: assert(DAP_MOCK_WAS_CALLED(dap_stream_write));

DAP_MOCK_GET_ARG(func_name, call_idx, arg_idx)
// Получить конкретный аргумент из конкретного вызова
// call_idx: 0-базированный индекс вызова (0 = первый вызов)
// arg_idx: 0-базированный индекс аргумента (0 = первый аргумент)
// Возвращает: void* (приведите к нужному типу)
// Пример: void *buffer = DAP_MOCK_GET_ARG(dap_stream_write, 0, 1);
//          size_t size = (size_t)DAP_MOCK_GET_ARG(dap_stream_write, 0, 2);
```

#### Макросы конфигурации задержек
```c
DAP_MOCK_SET_DELAY_FIXED(func_name, microseconds)
DAP_MOCK_SET_DELAY_MS(func_name, milliseconds)
// Установить фиксированную задержку

DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us)
DAP_MOCK_SET_DELAY_RANGE_MS(func_name, min_ms, max_ms)
// Установить случайную задержку в диапазоне

DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us)
DAP_MOCK_SET_DELAY_VARIANCE_MS(func_name, center_ms, variance_ms)
// Установить задержку с разбросом (например, 100мс ± 20мс)

DAP_MOCK_CLEAR_DELAY(func_name)
// Убрать задержку
```

#### Конфигурация callback
```c
DAP_MOCK_SET_CALLBACK(func_name, callback_func, user_data)
// Установить пользовательскую функцию callback

DAP_MOCK_CLEAR_CALLBACK(func_name)
// Убрать callback (использовать return_value)

// Сигнатура callback:
typedef void* (*dap_mock_callback_t)(
    void **a_args,
    int a_arg_count,
    void *a_user_data
);
```

### 3.3 API пользовательских линкер-оберток

**Заголовочный файл:** `dap_mock_linker_wrapper.h`

#### Макрос DAP_MOCK_WRAPPER_CUSTOM

Создает пользовательскую линкер-обертку с PARAM синтаксисом:

```c
DAP_MOCK_WRAPPER_CUSTOM(return_type, function_name,
    PARAM(type1, name1),
    PARAM(type2, name2),
    ...
) {
    // Реализация пользовательской обертки
}
```

**Возможности:**
- Автоматически генерирует сигнатуру функции
- Автоматически создает массив void* аргументов с правильным приведением типов
- Автоматически проверяет, включен ли мок
- Автоматически выполняет настроенную задержку
- Автоматически записывает вызов
- Вызывает реальную функцию при выключенном моке

**Пример:**
```c
DAP_MOCK_WRAPPER_CUSTOM(int, my_function,
    PARAM(const char*, path),
    PARAM(int, flags),
    PARAM(mode_t, mode)
) {
    // Ваша пользовательская логика здесь
    if (strcmp(path, "/dev/null") == 0) {
        return -1;  // Симуляция ошибки
    }
    return 0;  // Успех
}
```

**Макрос PARAM:**
- Формат: `PARAM(type, name)`
- Автоматически извлекает тип и имя
- Правильно обрабатывает приведение к void*
- Использует `_Generic()` для корректного приведения указателей

#### Упрощенные макросы оберток

Для распространенных типов возвращаемых значений:

```c
DAP_MOCK_WRAPPER_INT(func_name, (params), (args))
DAP_MOCK_WRAPPER_PTR(func_name, (params), (args))
DAP_MOCK_WRAPPER_VOID_FUNC(func_name, (params), (args))
DAP_MOCK_WRAPPER_BOOL(func_name, (params), (args))
DAP_MOCK_WRAPPER_SIZE_T(func_name, (params), (args))
```

### 3.4 Интеграция с CMake

**CMake модуль:** `mocks/DAPMockAutoWrap.cmake`

```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

# Автоматическое сканирование исходников и генерация --wrap флагов
dap_mock_autowrap(target_name)

# Альтернатива: явно указать исходные файлы
dap_mock_autowrap(TARGET target_name SOURCE file1.c file2.c)
```

**Как работает:**
1. Сканирует исходные файлы на наличие паттернов `DAP_MOCK_DECLARE`
2. Извлекает имена функций
3. Добавляет `-Wl,--wrap=function_name` к флагам линкера
4. Работает с GCC, Clang, MinGW

### 3.5 Асинхронное выполнение моков

**Заголовок:** `dap_mock_async.h`

Предоставляет легковесное асинхронное выполнение mock callback'ов без необходимости полной инфраструктуры `dap_events`. Идеально для unit тестов, требующих симуляции async поведения в изоляции.

#### Инициализация

```c
// Инициализация async системы с worker потоками
int dap_mock_async_init(uint32_t a_worker_count);
// a_worker_count: 0 = auto, обычно 1-2 для unit тестов
// Возвращает: 0 при успехе

// Деинициализация (ждёт завершения всех задач)
void dap_mock_async_deinit(void);

// Проверка инициализации
bool dap_mock_async_is_initialized(void);
```

#### Планирование задач

```c
// Запланировать выполнение async callback
dap_mock_async_task_t* dap_mock_async_schedule(
    dap_mock_async_callback_t a_callback,
    void *a_arg,
    uint32_t a_delay_ms  // 0 = немедленно
);

// Отменить pending задачу
bool dap_mock_async_cancel(dap_mock_async_task_t *a_task);
```

#### Ожидание завершения

```c
// Ждать конкретную задачу
bool dap_mock_async_wait_task(
    dap_mock_async_task_t *a_task,
    int a_timeout_ms  // -1 = бесконечно, 0 = не ждать
);

// Ждать все pending задачи
bool dap_mock_async_wait_all(int a_timeout_ms);
// Возвращает: true если все завершены, false при таймауте
```

#### Конфигурация async мока

Для включения async выполнения установите `.async = true` в конфигурации:

```c
// Async мок с задержкой
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request, {
    .enabled = true,
    .async = true,  // Выполнять callback асинхронно
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 50000  // 50ms
    }
});

// Mock обертка (выполняется асинхронно если был вызван dap_mock_async_init())
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request,
    PARAM(const char*, a_url),
    PARAM(callback_t, a_callback),
    PARAM(void*, a_arg)
) {
    // Этот код выполняется в worker потоке после задержки
    a_callback("response data", 200, a_arg);
}
```

#### Утилиты

```c
// Получить количество pending задач
size_t dap_mock_async_get_pending_count(void);

// Получить количество completed задач
size_t dap_mock_async_get_completed_count(void);

// Выполнить все pending задачи немедленно ("промотать время")
void dap_mock_async_flush(void);

// Сбросить статистику
void dap_mock_async_reset_stats(void);

// Установить дефолтную задержку для async моков
void dap_mock_async_set_default_delay(uint32_t a_delay_ms);
```

#### Паттерн использования

```c
void test_async_http(void) {
    // Примечание: Ручная инициализация не нужна! Async система авто-инициализируется с mock фреймворком
    
    volatile bool done = false;
    
    // Вызвать функцию с async моком (сконфигурированным с .async = true)
    dap_client_http_request("http://test.com", callback, &done);
    
    // Ждать async завершения
    DAP_TEST_WAIT_UNTIL(done, 5000, "HTTP request");
    
    // Или ждать все async моки
    bool completed = dap_mock_async_wait_all(5000);
    assert(completed && done);
    
    // Очистка (опционально, обрабатывается dap_mock_deinit())
    // dap_mock_deinit();  // Также авто-очищает async систему
}
```

**Примечание:** Async система автоматически инициализируется при старте mock фреймворка (через конструктор). Ручной `dap_mock_async_init()` нужен только если хотите настроить количество worker потоков.

\newpage
