# DAP Mock Async - Асинхронное выполнение моков

## Обзор

`dap_mock_async` - это модуль для асинхронного выполнения mock коллбэков без необходимости инициализации полной инфраструктуры событий DAP SDK (`dap_events`, `dap_worker`).

Предназначен для **unit тестов**, где требуется эмуляция асинхронного поведения в изолированной среде.

## Ключевые возможности

✅ **Легковесная асинхронность** - работает без `dap_events`  
✅ **Пул потоков** - configurable worker threads (обычно 1-2 для unit тестов)  
✅ **Задержки** - симуляция сетевых латенс, I/O операций  
✅ **Очередь задач** - упорядоченное выполнение  
✅ **Полная изоляция** - для pure unit testing  

## Использование

### 1. Базовая инициализация

```c
#include "dap_mock_async.h"

// В начале теста
dap_mock_async_init(2);  // 2 worker потока

// В конце теста  
dap_mock_async_deinit();  // Ждёт завершения всех задач
```

### 2. Объявление асинхронного мока

```c
#include "dap_mock.h"

// Асинхронный мок с задержкой 100±50ms
DAP_MOCK_DECLARE_CUSTOM(
    dap_client_http_request_async,
    { 
        .enabled = true, 
        .async = true,  // ← Асинхронное выполнение
        .delay = {
            .type = DAP_MOCK_DELAY_VARIANCE,
            .variance = { .center_us = 100000, .variance_us = 50000 }
        }
    }
);

// Реализация мока
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request_async,
    PARAM(const char *, a_url),
    PARAM(dap_http_callback_t, a_callback),
    PARAM(void *, a_arg)
) {
    // Логика мока - будет выполнена асинхронно
    a_callback(mock_response, 200, a_arg);
}
```

### 3. Ожидание завершения в тесте

```c
void test_async_http_request(void) {
    TEST_INFO("Starting async HTTP request test");
    
    bool callback_called = false;
    
    // Вызов функции с async моком
    dap_client_http_request_async(
        "http://test.com",
        my_callback,
        &callback_called
    );
    
    // Ждём завершения асинхронных моков (макс 5 секунд)
    bool completed = dap_mock_async_wait_all(5000);
    TEST_ASSERT(completed, "Async mocks should complete");
    
    TEST_ASSERT(callback_called, "Callback should be called");
    TEST_SUCCESS("Async test passed");
}
```

### 4. Использование с DAP_TEST_WAIT_UNTIL

```c
void test_async_with_condition(void) {
    volatile bool result_ready = false;
    
    dap_client_http_request_async("http://test.com", callback, &result_ready);
    
    // Ждём условие (как в настоящих async тестах)
    DAP_TEST_WAIT_UNTIL(result_ready, 5000, "HTTP request");
    
    TEST_ASSERT(result_ready, "Result should be ready");
}
```

## API Reference

### Инициализация

```c
// Инициализация (0 = auto, обычно 2 воркера)
int dap_mock_async_init(uint32_t a_worker_count);

// Деинициализация (ждёт завершения всех задач)
void dap_mock_async_deinit(void);

// Проверка инициализации
bool dap_mock_async_is_initialized(void);
```

### Планирование задач

```c
// Запланировать выполнение callback
dap_mock_async_task_t* dap_mock_async_schedule(
    dap_mock_async_callback_t a_callback,
    void *a_arg,
    uint32_t a_delay_ms  // Задержка перед выполнением
);

// Отменить задачу
bool dap_mock_async_cancel(dap_mock_async_task_t *a_task);
```

### Ожидание завершения

```c
// Ждать конкретную задачу
bool dap_mock_async_wait_task(
    dap_mock_async_task_t *a_task, 
    int a_timeout_ms  // -1 = infinite, 0 = no wait
);

// Ждать все задачи
bool dap_mock_async_wait_all(int a_timeout_ms);
```

### Статистика и контроль

```c
// Получить количество pending задач
size_t dap_mock_async_get_pending_count(void);

// Получить количество completed задач
size_t dap_mock_async_get_completed_count(void);

// Сбросить статистику
void dap_mock_async_reset_stats(void);

// "Промотать время" - выполнить все задачи немедленно
void dap_mock_async_flush(void);
```

### Конфигурация

```c
// Установить дефолтную задержку для async моков
void dap_mock_async_set_default_delay(uint32_t a_delay_ms);

// Получить дефолтную задержку
uint32_t dap_mock_async_get_default_delay(void);
```

## Примеры использования

### Пример 1: HTTP Client Mock с async

```c
// test_http_client_async.c
#include "dap_mock.h"
#include "dap_mock_async.h"
#include "dap_test_helpers.h"

// Мок response structure
static struct {
    const char *body;
    int status_code;
} g_mock_response;

// Мок с async=true
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request,
    { .async = true, .delay = { .type = DAP_MOCK_DELAY_FIXED, .fixed_us = 50000 } }  // 50ms
);

DAP_MOCK_WRAPPER_CUSTOM(int, dap_client_http_request,
    PARAM(const char *, a_url),
    PARAM(dap_http_response_callback_t, a_callback),
    PARAM(void *, a_arg)
) {
    // Этот код выполнится асинхронно через 50ms
    a_callback(g_mock_response.body, g_mock_response.status_code, a_arg);
    return 0;
}

void test_http_async(void) {
    // Setup
    dap_mock_async_init(1);
    g_mock_response.body = "{\"status\":\"ok\"}";
    g_mock_response.status_code = 200;
    
    // Test
    volatile bool done = false;
    dap_client_http_request("http://test.com", my_callback, &done);
    
    // Wait
    DAP_TEST_WAIT_UNTIL(done, 1000, "HTTP request");
    TEST_ASSERT(done, "Should complete");
    
    // Cleanup
    dap_mock_async_deinit();
}
```

### Пример 2: Множественные async запросы

```c
void test_multiple_async_requests(void) {
    dap_mock_async_init(2);  // 2 воркера для параллелизма
    
    int completed_count = 0;
    pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Запустить 10 запросов
    for (int i = 0; i < 10; i++) {
        dap_client_http_request_async(
            "http://test.com",
            count_increment_callback,
            &completed_count
        );
    }
    
    // Ждать все
    bool all_done = dap_mock_async_wait_all(5000);
    TEST_ASSERT(all_done, "All requests should complete");
    TEST_ASSERT_EQUAL_INT(10, completed_count, "All callbacks executed");
    
    dap_mock_async_deinit();
}
```

### Пример 3: Fast-forward времени

```c
void test_with_flush(void) {
    dap_mock_async_init(1);
    
    // Задача с большой задержкой
    volatile bool done = false;
    dap_mock_async_schedule(my_callback, &done, 60000);  // 60 секунд!
    
    // Обычно пришлось бы ждать минуту, но...
    dap_mock_async_flush();  // Выполнить немедленно
    
    // Проверить сразу
    TEST_ASSERT(done, "Should be done after flush");
    
    dap_mock_async_deinit();
}
```

## Интеграция с DAP Mock Framework

### Автоматическая обработка async в моках

Когда мок объявлен с `.async = true`, `DAP_MOCK_WRAPPER_CUSTOM` автоматически:

1. Проверяет `dap_mock_async_is_initialized()`
2. Если да - планирует callback через `dap_mock_async_schedule()`
3. Если нет - выполняет синхронно (fallback)

Это означает, что **один и тот же мок** можно использовать:
- Асинхронно - в тестах с `dap_mock_async_init()`
- Синхронно - в тестах без async инициализации

### Пример fallback поведения

```c
// Тест 1: Асинхронный
void test_with_async(void) {
    dap_mock_async_init(1);  // ← Async enabled
    
    dap_client_http_request(...);  // Выполнится асинхронно
    dap_mock_async_wait_all(5000);
    
    dap_mock_async_deinit();
}

// Тест 2: Синхронный (тот же мок!)
void test_without_async(void) {
    // НЕТ dap_mock_async_init()
    
    dap_client_http_request(...);  // Выполнится синхронно!
    // Callback уже вызван, ничего ждать не надо
}
```

## Best Practices

### ✅ DO

1. **Инициализируй в setup, деинициализируй в teardown**
   ```c
   void setup_test(void) {
       dap_mock_async_init(1);
   }
   
   void teardown_test(void) {
       dap_mock_async_deinit();  // Автоматически ждёт завершения
   }
   ```

2. **Используй малое количество воркеров в unit тестах**
   ```c
   dap_mock_async_init(1);  // Обычно достаточно 1-2
   ```

3. **Всегда жди завершения**
   ```c
   dap_mock_async_wait_all(timeout);  // Явное ожидание
   ```

4. **Используй `dap_mock_async_flush()` для быстрых тестов**
   ```c
   dap_mock_async_flush();  // "Промотать" время
   ```

### ❌ DON'T

1. **НЕ используй большое количество воркеров**
   ```c
   dap_mock_async_init(100);  // ❌ Overkill для unit тестов!
   ```

2. **НЕ забывай deinit**
   ```c
   // ❌ Memory leak, потоки висят
   dap_mock_async_init(1);
   // ... тест ...
   // Нет deinit!
   ```

3. **НЕ смешивай с dap_events в unit тестах**
   ```c
   // ❌ Конфликт: либо dap_events, либо dap_mock_async
   dap_events_init();
   dap_mock_async_init(1);
   ```

4. **НЕ полагайся на порядок выполнения при > 1 воркере**
   ```c
   dap_mock_async_init(2);
   // Задачи могут выполняться в произвольном порядке!
   ```

## Unit Tests vs Integration Tests

|  | Unit Tests (dap_mock_async) | Integration Tests (dap_events) |
|---|---|---|
| **Инициализация** | `dap_mock_async_init(1)` | `dap_events_init(...)` |
| **Изоляция** | ✅ Полная | ❌ Требует реальную инфраструктуру |
| **Скорость** | ✅ Быстро | ⚠️ Медленнее |
| **Сложность** | ✅ Просто | ⚠️ Сложная настройка |
| **Назначение** | Тестирование логики | Тестирование интеграции |

## Troubleshooting

### Проблема: Callback не вызывается

**Возможные причины:**
1. Не вызван `dap_mock_async_wait_all()`
2. Таймаут слишком маленький
3. Мок не инициализирован (проверь `.enabled = true`)

**Решение:**
```c
TEST_ASSERT(dap_mock_async_is_initialized(), "Async должен быть инициализирован");
dap_mock_async_wait_all(10000);  // Увеличь таймаут
```

### Проблема: Тест зависает

**Возможные причины:**
1. Бесконечное ожидание (`wait_all(-1)`)
2. Deadlock в callback
3. Воркеры не запущены

**Решение:**
```c
// Используй конечный таймаут
bool done = dap_mock_async_wait_all(5000);
TEST_ASSERT(done, "Не должно зависнуть");
```

### Проблема: Race conditions

**Возможные причины:**
1. Множественные воркеры без синхронизации
2. Доступ к shared state без mutex

**Решение:**
```c
// Используй 1 воркер для детерминизма
dap_mock_async_init(1);

// Или защищай shared state
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_lock(&mutex);
shared_var++;
pthread_mutex_unlock(&mutex);
```

## См. также

- [DAP Mock Framework](./README.md) - Основная документация по мокам
- [DAP Test Helpers](../fixtures/README.md) - Утилиты для тестов
- [Test Framework Overview](./01_overview.md) - Общий обзор тестового фреймворка

