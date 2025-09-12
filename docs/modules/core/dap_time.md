# DAP Time Module (dap_time.h/c)

## Обзор

Модуль `dap_time.h/c` предоставляет высокоточные функции для работы со временем в DAP SDK. Модуль обеспечивает кроссплатформенную работу со временем, включая поддержку наносекунд, микросекунд и миллисекунд, а также различные форматы представления времени.

## Основные возможности

- **Высокоточное время**: Поддержка наносекундной точности
- **Кроссплатформенность**: Единая API для Windows, Linux и macOS
- **Множественные форматы**: Поддержка Unix timestamp, RFC822, упрощенный формат
- **Таймеры и задержки**: Функции для пауз и измерения интервалов
- **Безопасные преобразования**: Защищенные от переполнения операции

## Архитектура

```mermaid
graph TB
    subgraph "DAP Time Architecture"
        A[Time Sources] --> B[System Clock]
        A --> C[High-Resolution Timer]

        D[Time Representations] --> E[Seconds (dap_time_t)]
        D --> F[Nanoseconds (dap_nanotime_t)]
        D --> G[Milliseconds (dap_millitime_t)]

        H[Format Conversions] --> I[RFC822 Format]
        H --> J[Simplified Format]
        H --> K[Custom Format]

        L[Utilities] --> M[Sleep Functions]
        L --> N[Time Differences]
        L --> O[Time Validation]
    end
```

## Основные определения типов

### Типы времени

```c
// Время в секундах (Unix timestamp)
typedef uint64_t dap_time_t;

// Время в наносекундах (высокая точность)
typedef uint64_t dap_nanotime_t;

// Время в миллисекундах
typedef uint64_t dap_millitime_t;
```

### Константы и пределы

```c
#define DAP_TIME_STR_SIZE 32              // Максимальный размер строки времени
#define DAP_END_OF_DAYS 4102444799        // 31 декабря 2099 года

// Константы преобразования
#define DAP_NSEC_PER_SEC 1000000000       // Наносекунд в секунде
#define DAP_NSEC_PER_MSEC 1000000         // Наносекунд в миллисекунде
#define DAP_USEC_PER_SEC 1000000          // Микросекунд в секунде
#define DAP_SEC_PER_DAY 86400             // Секунд в сутках
```

### Строковые представления

```c
// Упрощенное представление времени [%y%m%d]
typedef union dap_time_simpl_str {
    const char s[7];  // Формат: YYMMDD (220610)
} dap_time_simpl_str_t;
```

## Основные функции

### Получение текущего времени

#### Текущее время в секундах

```c
static inline dap_time_t dap_time_now(void);
```

**Пример:**
```c
#include <dap_time.h>

// Получить текущее время
dap_time_t current_time = dap_time_now();
printf("Current time: %llu seconds since epoch\n", current_time);
```

#### Текущее время в наносекундах

```c
static inline dap_nanotime_t dap_nanotime_now(void);
```

**Пример:**
```c
// Высокоточное время для измерения производительности
dap_nanotime_t start_time = dap_nanotime_now();

// Выполняем какую-то операцию
do_some_work();

dap_nanotime_t end_time = dap_nanotime_now();
dap_nanotime_t elapsed = end_time - start_time;

printf("Operation took %llu nanoseconds\n", elapsed);
```

### Преобразование между форматами времени

#### Секунды ↔ наносекунды

```c
static inline dap_nanotime_t dap_nanotime_from_sec(dap_time_t a_time);
static inline dap_time_t dap_nanotime_to_sec(dap_nanotime_t a_time);
```

**Пример:**
```c
dap_time_t seconds = 1640995200;  // 2022-01-01 00:00:00 UTC
dap_nanotime_t nanoseconds = dap_nanotime_from_sec(seconds);

printf("Seconds: %llu\n", seconds);
printf("Nanoseconds: %llu\n", nanoseconds);

// Обратное преобразование
dap_time_t back_to_seconds = dap_nanotime_to_sec(nanoseconds);
assert(seconds == back_to_seconds);
```

#### Миллисекунды ↔ наносекунды

```c
static inline dap_millitime_t dap_nanotime_to_millitime(dap_nanotime_t a_time);
static inline dap_nanotime_t dap_millitime_to_nanotime(dap_millitime_t a_time);
```

**Пример:**
```c
// Преобразование для работы с таймерами
dap_nanotime_t nano_time = dap_nanotime_now();
dap_millitime_t milli_time = dap_nanotime_to_millitime(nano_time);

// Использование в JavaScript-подобном коде
// milli_time можно использовать для setTimeout/setInterval аналогов
```

### Преобразование времени в строки

#### Упрощенный формат (YYMMDD)

```c
static inline dap_time_simpl_str_t s_dap_time_to_str_simplified(dap_time_t a_time);
#define dap_time_to_str_simplified(t) s_dap_time_to_str_simplified(t).s
```

**Пример:**
```c
dap_time_t timestamp = 1640995200;  // 2022-01-01
const char *date_str = dap_time_to_str_simplified(timestamp);
printf("Date: %s\n", date_str);  // Вывод: "220101"
```

#### RFC822 формат

```c
int dap_time_to_str_rfc822(char *a_out, size_t a_out_size_max, dap_time_t a_time);
int dap_nanotime_to_str_rfc822(char *a_out, size_t a_out_size_max, dap_nanotime_t a_time);
```

**Пример:**
```c
char rfc822_str[64];
dap_time_t timestamp = dap_time_now();

if (dap_time_to_str_rfc822(rfc822_str, sizeof(rfc822_str), timestamp) > 0) {
    printf("RFC822: %s\n", rfc822_str);
    // Вывод: "Sat, 01 Jan 2022 00:00:00 GMT"
}
```

#### Наносекунды в RFC822

```c
char nano_rfc822_str[64];
dap_nanotime_t nano_time = dap_nanotime_now();

if (dap_nanotime_to_str_rfc822(nano_rfc822_str, sizeof(nano_rfc822_str), nano_time) > 0) {
    printf("Nano RFC822: %s\n", nano_rfc822_str);
}
```

### Парсинг строк времени

#### Из RFC822 формата

```c
dap_time_t dap_time_from_str_rfc822(const char *a_time_str);
```

**Пример:**
```c
const char *rfc822_time = "Sat, 01 Jan 2022 00:00:00 GMT";
dap_time_t parsed_time = dap_time_from_str_rfc822(rfc822_time);

if (parsed_time != 0) {
    printf("Parsed time: %llu\n", parsed_time);
}
```

#### Из упрощенного формата

```c
dap_time_t dap_time_from_str_simplified(const char *a_time_str);
```

**Пример:**
```c
const char *simple_date = "220101";  // 2022-01-01
dap_time_t parsed_time = dap_time_from_str_simplified(simple_date);

if (parsed_time != 0) {
    printf("Parsed timestamp: %llu\n", parsed_time);
}
```

#### Из произвольного формата

```c
dap_time_t dap_time_from_str_custom(const char *a_time_str, const char *a_format_str);
```

**Пример:**
```c
// Парсинг времени в формате ISO 8601
const char *iso_time = "2022-01-01T00:00:00Z";
dap_time_t parsed_time = dap_time_from_str_custom(iso_time, "%Y-%m-%dT%H:%M:%SZ");

if (parsed_time != 0) {
    printf("Parsed ISO time: %llu\n", parsed_time);
}
```

### Задержки и таймеры

#### Микросекундная задержка

```c
void dap_usleep(uint64_t a_microseconds);
```

**Пример:**
```c
// Задержка на 100 миллисекунд
dap_usleep(100000);

// Задержка на 1 секунду
dap_usleep(1000000);
```

#### Измерение интервалов времени

```c
int timespec_diff(struct timespec *a_start, struct timespec *a_stop, struct timespec *a_result);
```

**Пример:**
```c
struct timespec start, end, diff;

// Запуск таймера
clock_gettime(CLOCK_MONOTONIC, &start);

// Выполнение операции
do_operation();

// Остановка таймера
clock_gettime(CLOCK_MONOTONIC, &end);

// Вычисление разницы в миллисекундах
int diff_ms = timespec_diff(&start, &end, &diff);
printf("Operation took %d milliseconds\n", diff_ms);

// Или использование наносекунд
dap_nanotime_t start_ns = dap_nanotime_now();
do_operation();
dap_nanotime_t end_ns = dap_nanotime_now();
dap_nanotime_t elapsed_ns = end_ns - start_ns;
printf("Operation took %llu nanoseconds\n", elapsed_ns);
```

## Примеры использования

### 1. Профилирование производительности

```c
#include <dap_time.h>

typedef struct {
    const char *operation_name;
    dap_nanotime_t start_time;
    dap_nanotime_t end_time;
} perf_timer_t;

// Запуск таймера
void perf_start(perf_timer_t *timer, const char *operation) {
    timer->operation_name = operation;
    timer->start_time = dap_nanotime_now();
}

// Остановка таймера и вывод результата
void perf_stop(perf_timer_t *timer) {
    timer->end_time = dap_nanotime_now();
    dap_nanotime_t elapsed = timer->end_time - timer->start_time;

    printf("Operation '%s' took:\n", timer->operation_name);
    printf("  - %llu nanoseconds\n", elapsed);
    printf("  - %llu microseconds\n", elapsed / 1000);
    printf("  - %llu milliseconds\n", elapsed / 1000000);
    printf("  - %.3f seconds\n", (double)elapsed / 1000000000.0);
}
```

### 2. Работа с временными метками блокчейна

```c
#include <dap_time.h>

typedef struct {
    uint64_t block_number;
    dap_time_t timestamp;
    char *hash;
} blockchain_block_t;

// Создание блока с временной меткой
blockchain_block_t *create_block(uint64_t block_number, const char *data) {
    blockchain_block_t *block = DAP_NEW(blockchain_block_t);
    block->block_number = block_number;
    block->timestamp = dap_time_now();

    // Создание хеша блока (упрощенная версия)
    // В реальности использовался бы dap_hash
    block->hash = strdup("block_hash_placeholder");

    return block;
}

// Проверка временной метки блока
bool validate_block_timestamp(const blockchain_block_t *block, dap_time_t max_age_seconds) {
    dap_time_t current_time = dap_time_now();
    dap_time_t block_age = current_time - block->timestamp;

    return block_age <= max_age_seconds;
}

// Форматирование временной метки для логов
void log_block_creation(const blockchain_block_t *block) {
    char time_str[32];
    if (dap_time_to_str_rfc822(time_str, sizeof(time_str), block->timestamp) > 0) {
        printf("Block #%llu created at %s\n", block->block_number, time_str);
    }
}
```

### 3. Реализация таймера событий

```c
#include <dap_time.h>

typedef struct {
    dap_nanotime_t trigger_time;
    void (*callback)(void *user_data);
    void *user_data;
    bool is_active;
} event_timer_t;

typedef struct {
    event_timer_t *timers;
    size_t count;
    size_t capacity;
} timer_manager_t;

// Инициализация менеджера таймеров
timer_manager_t *timer_manager_create(size_t initial_capacity) {
    timer_manager_t *manager = DAP_NEW(timer_manager_t);
    manager->capacity = initial_capacity;
    manager->count = 0;
    manager->timers = DAP_NEW_Z_COUNT(event_timer_t, initial_capacity);
    return manager;
}

// Добавление таймера
bool timer_manager_add(timer_manager_t *manager, dap_nanotime_t delay_ns,
                      void (*callback)(void *), void *user_data) {
    if (manager->count >= manager->capacity) {
        return false;  // Нет места
    }

    event_timer_t *timer = &manager->timers[manager->count++];
    timer->trigger_time = dap_nanotime_now() + delay_ns;
    timer->callback = callback;
    timer->user_data = user_data;
    timer->is_active = true;

    return true;
}

// Обработка истекших таймеров
void timer_manager_process(timer_manager_t *manager) {
    dap_nanotime_t current_time = dap_nanotime_now();

    for (size_t i = 0; i < manager->count; i++) {
        event_timer_t *timer = &manager->timers[i];
        if (timer->is_active && current_time >= timer->trigger_time) {
            timer->callback(timer->user_data);
            timer->is_active = false;
        }
    }
}

// Ожидание следующего события
void timer_manager_wait_next(timer_manager_t *manager) {
    dap_nanotime_t current_time = dap_nanotime_now();
    dap_nanotime_t next_trigger = UINT64_MAX;

    for (size_t i = 0; i < manager->count; i++) {
        event_timer_t *timer = &manager->timers[i];
        if (timer->is_active && timer->trigger_time < next_trigger) {
            next_trigger = timer->trigger_time;
        }
    }

    if (next_trigger != UINT64_MAX && next_trigger > current_time) {
        uint64_t wait_us = (next_trigger - current_time) / 1000;
        if (wait_us > 0) {
            dap_usleep(wait_us);
        }
    }
}
```

### 4. Синхронизация времени в распределенной системе

```c
#include <dap_time.h>

typedef struct {
    char *node_id;
    dap_nanotime_t local_time;
    dap_nanotime_t network_time;
    int64_t offset_ns;  // Смещение относительно сетевого времени
} time_sync_info_t;

// Синхронизация времени с сетью
void synchronize_time(time_sync_info_t *sync_info) {
    // Запрашиваем время от других узлов сети
    // В реальности это был бы сетевой запрос
    dap_nanotime_t network_time = get_network_time();

    sync_info->local_time = dap_nanotime_now();
    sync_info->network_time = network_time;
    sync_info->offset_ns = (int64_t)network_time - (int64_t)sync_info->local_time;
}

// Получение скорректированного времени
dap_nanotime_t get_adjusted_time(const time_sync_info_t *sync_info) {
    dap_nanotime_t local_now = dap_nanotime_now();
    return local_now + sync_info->offset_ns;
}

// Проверка синхронизации
bool is_time_synchronized(const time_sync_info_t *sync_info, dap_nanotime_t tolerance_ns) {
    dap_nanotime_t current_offset = llabs((int64_t)get_adjusted_time(sync_info) -
                                        (int64_t)dap_nanotime_now());
    return current_offset <= tolerance_ns;
}
```

## Производительность

### Характеристики производительности

| Операция | Производительность | Комментарий |
|----------|-------------------|-------------|
| `dap_time_now()` | ~10-20 ns | Системный вызов |
| `dap_nanotime_now()` | ~20-50 ns | Высокоточное время |
| `dap_usleep()` | Зависит от задержки | Точный сон |
| Преобразования | ~5-10 ns | Арифметические операции |

### Оптимизации

- **Inline функции**: Большинство функций реализованы как `inline` для производительности
- **Минимальные системные вызовы**: Кэширование результатов где возможно
- **Кроссплатформенные оптимизации**: Специфичный код для каждой платформы
- **Безопасная арифметика**: Защита от переполнения в преобразованиях

## Безопасность

### Временные атаки

```c
// НЕБЕЗОПАСНО: может быть подвержено timing attacks
bool insecure_compare(dap_time_t a, dap_time_t b) {
    return a == b;  // Разное время выполнения для разных значений
}

// БЕЗОПАСНО: постоянное время выполнения
bool secure_compare(dap_time_t a, dap_time_t b) {
    return (a - b) == 0;  // Постоянное время выполнения
}
```

### Проверка границ

```c
// Проверка корректности временных меток
bool is_valid_timestamp(dap_time_t timestamp) {
    dap_time_t current = dap_time_now();
    dap_time_t min_time = 0;  // 1970-01-01
    dap_time_t max_time = DAP_END_OF_DAYS;  // 2099-12-31

    return timestamp >= min_time && timestamp <= max_time &&
           timestamp <= current + 3600;  // Не более чем на час в будущем
}
```

## Кроссплатформенность

### Windows-специфичные реализации

```c
#ifdef DAP_OS_WINDOWS
// Windows-specific time functions
#define localtime_r(a, b) localtime_s((b), (a))

// Windows-compatible sleep
void dap_usleep(uint64_t microseconds) {
    Sleep(microseconds / 1000);
}
#endif
```

### Unix-специфичные реализации

```c
#ifndef DAP_OS_WINDOWS
// Unix-specific high-precision time
dap_nanotime_t dap_nanotime_now(void) {
    struct timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    return (dap_nanotime_t)cur_time.tv_sec * DAP_NSEC_PER_SEC + cur_time.tv_nsec;
}
#endif
```

## Интеграция с другими модулями

### Совместимость с DAP SDK

- **dap_common.h**: Использует базовые типы и макросы
- **dap_config.h**: Может использоваться для хранения временных настроек
- **dap_list.h**: Совместим с временными метками в списках
- **dap_hash.h**: Может использоваться для создания временных хешей

## Рекомендации по использованию

### Выбор типа времени

```c
// Для большинства операций используйте секунды
dap_time_t user_registration = dap_time_now();

// Для измерения производительности используйте наносекунды
dap_nanotime_t perf_start = dap_nanotime_now();

// Для таймеров используйте миллисекунды
dap_millitime_t timeout_ms = 5000;  // 5 секунд
```

### Обработка ошибок

```c
// Всегда проверяйте результаты преобразований
char time_str[64];
dap_time_t timestamp = dap_time_now();

int result = dap_time_to_str_rfc822(time_str, sizeof(time_str), timestamp);
if (result <= 0) {
    log_error("Failed to convert time to RFC822 format");
    return false;
}

// Проверяйте входные данные при парсинге
dap_time_t parsed = dap_time_from_str_rfc822(input_str);
if (parsed == 0) {
    log_error("Invalid RFC822 time format: %s", input_str);
    return false;
}
```

## Заключение

Модуль `dap_time.h/c` предоставляет надежную и эффективную систему работы со временем для DAP SDK. Поддержка высокоточной работы со временем, кроссплатформенность и безопасность делают его незаменимым компонентом для любых приложений, требующих точного учета времени.
