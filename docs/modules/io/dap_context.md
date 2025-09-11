# DAP Context Module (dap_context.h)

## Обзор

Модуль `dap_context.h` предоставляет абстракцию контекста выполнения для управления потоками и их ресурсами в системе асинхронных событий DAP SDK. Контекст представляет собой независимую единицу исполнения с собственным жизненным циклом и ресурсами.

## Архитектурная роль

Контекст является фундаментом для рабочих потоков:

```
┌─────────────────┐
│   dap_events    │ ← Координация контекстов
└─────────────────┘
         │
    ┌────▼────┐
    │dap_worker │ ← Управление очередями
    └────┬────┘
         │
    ┌────▼────┐
    │dap_context│ ← Платформенная обработка событий
    └─────────┘
```

## Структура данных

### `dap_context_t`
```c
typedef struct dap_context {
    uint32_t id;              // Уникальный идентификатор контекста
    int cpu_id;               // ID CPU (если привязан)
    pthread_t thread_id;      // ID потока

    int type;                 // Тип контекста
    bool started;             // Флаг запуска
    bool signal_exit;         // Сигнал выхода
    bool is_running;          // Статус выполнения

    // Синхронизация
    pthread_cond_t started_cond;
    pthread_mutex_t started_mutex;

    // Платформенно-зависимые поля
    union {
        // Linux
        EPOLL_HANDLE epoll_fd;
        // macOS/BSD
        int kqueue_fd;
        // Windows
        HANDLE iocp;
    };

    // Управление сокетами
    atomic_uint event_sockets_count;
    dap_events_socket_t *esockets;
    dap_events_socket_t *event_exit;

    void *_inheritor;         // Указатель для наследования
} dap_context_t;
```

## Типы контекстов

### `dap_context_type`
```c
enum dap_context_type {
    DAP_CONTEXT_TYPE_WORKER,      // Рабочий поток
    DAP_CONTEXT_TYPE_PROC_THREAD  // Процессный поток
};
```

## Основные функции

### Инициализация и управление

#### `dap_context_init()`
```c
int dap_context_init();
```

Инициализирует систему контекстов.

**Возвращаемые значения:**
- `0` - успешная инициализация
- `-1` - ошибка инициализации

#### `dap_context_deinit()`
```c
void dap_context_deinit();
```

Деинициализирует систему контекстов.

### Создание и уничтожение

#### `dap_context_new()`
```c
dap_context_t *dap_context_new(int a_type);
```

Создает новый контекст указанного типа.

**Параметры:**
- `a_type` - тип контекста (WORKER или PROC_THREAD)

**Возвращаемое значение:**
- Указатель на созданный контекст или NULL при ошибке

#### `dap_context_stop_n_kill()`
```c
void dap_context_stop_n_kill(dap_context_t *a_context);
```

Останавливает контекст и уничтожает связанный поток.

### Запуск и выполнение

#### `dap_context_run()`
```c
int dap_context_run(dap_context_t *a_context, int a_cpu_id,
                    int a_sched_policy, int a_priority, uint32_t a_flags,
                    dap_context_callback_t a_callback_started,
                    dap_context_callback_t a_callback_stopped,
                    void *a_callback_arg);
```

Запускает контекст в выделенном потоке.

**Параметры:**
- `a_context` - контекст для запуска
- `a_cpu_id` - ID CPU для привязки (-1 для автоматического выбора)
- `a_sched_policy` - политика планирования
- `a_priority` - приоритет потока
- `a_flags` - флаги запуска
- `a_callback_started` - callback при запуске
- `a_callback_stopped` - callback при остановке
- `a_callback_arg` - аргумент для callbacks

**Тип callback функции:**
```c
typedef int (*dap_context_callback_t)(dap_context_t *a_context, void *a_arg);
```

#### `dap_context_wait()`
```c
void dap_context_wait(dap_context_t *a_context);
```

Ожидает завершения контекста.

### Управление сокетами

#### `dap_context_add()`
```c
int dap_context_add(dap_context_t *a_context, dap_events_socket_t *a_es);
```

Добавляет сокет событий в контекст.

**Возвращаемые значения:**
- `0` - успешное добавление
- `-1` - ошибка добавления

#### `dap_context_remove()`
```c
int dap_context_remove(dap_events_socket_t *a_es);
```

Удаляет сокет событий из контекста.

#### `dap_context_find()`
```c
dap_events_socket_t *dap_context_find(dap_context_t *a_context,
                                      dap_events_socket_uuid_t a_es_uuid);
```

Ищет сокет событий по UUID.

**Возвращаемое значение:**
- Указатель на найденный сокет или NULL

### Создание специальных сокетов

#### `dap_context_create_queue()`
```c
dap_events_socket_t *dap_context_create_queue(
    dap_context_t *a_context,
    dap_events_socket_callback_queue_ptr_t a_callback);
```

Создает очередь в контексте.

#### `dap_context_create_event()`
```c
dap_events_socket_t *dap_context_create_event(
    dap_context_t *a_context,
    dap_events_socket_callback_event_t a_callback);
```

Создает событие в контексте.

#### `dap_context_create_pipe()`
```c
dap_events_socket_t *dap_context_create_pipe(
    dap_context_t *a_context,
    dap_events_socket_callback_t a_callback,
    uint32_t a_flags);
```

Создает pipe в контексте.

### Системные функции

#### `dap_context_current()`
```c
dap_context_t *dap_context_current();
```

Возвращает текущий контекст исполнения.

**Возвращаемое значение:**
- Текущий контекст или NULL

## Флаги и константы

### Флаги запуска
```c
#define DAP_CONTEXT_FLAG_WAIT_FOR_STARTED 0x00000001  // Ожидать запуска
#define DAP_CONTEXT_FLAG_EXIT_IF_ERROR    0x00000100  // Выход при ошибке
```

### Политики планирования
```c
#define DAP_CONTEXT_POLICY_DEFAULT     0  // По умолчанию
#define DAP_CONTEXT_POLICY_TIMESHARING 1  // Time-sharing
#define DAP_CONTEXT_POLICY_FIFO        2  // FIFO (реального времени)
#define DAP_CONTEXT_POLICY_ROUND_ROBIN 3  // Round-robin (реального времени)
```

### Приоритеты
```c
// Unix-системы
#define DAP_CONTEXT_PRIORITY_NORMAL -1
#define DAP_CONTEXT_PRIORITY_HIGH   -2
#define DAP_CONTEXT_PRIORITY_LOW    -3

// Windows
#define DAP_CONTEXT_PRIORITY_NORMAL THREAD_PRIORITY_NORMAL
#define DAP_CONTEXT_PRIORITY_HIGH   THREAD_PRIORITY_HIGHEST
#define DAP_CONTEXT_PRIORITY_LOW    THREAD_PRIORITY_LOWEST
```

### Таймауты
```c
#define DAP_CONTEXT_WAIT_FOR_STARTED_TIME 15  // Время ожидания запуска (сек)
```

## Алгоритм работы

### 1. Создание контекста
```
dap_context_new() → Выделение памяти
    ↓
Инициализация платформенно-зависимых структур
    ↓
Настройка синхронизации (mutex/cond)
    ↓
Готовность к запуску
```

### 2. Запуск контекста
```
dap_context_run() → Создание потока
    ↓
Привязка к CPU (опционально)
    ↓
Установка политики планирования
    ↓
Callback: started
    ↓
Основной цикл обработки
    ↓
Callback: stopped
```

### 3. Обработка событий
```
Входящее событие → Платформенный механизм опроса
    ↓
Выбор активных сокетов
    ↓
Обработка в цикле
    ↓
Callback функции сокетов
```

## Платформенные особенности

### Linux (epoll)
```c
struct {
    EPOLL_HANDLE epoll_fd;
    struct epoll_event epoll_events[DAP_EVENTS_SOCKET_MAX];
};
```
- Использует `epoll_create()` и `epoll_wait()`
- Поддержка edge-triggered и level-triggered режимов
- Высокая эффективность для большого количества сокетов

### macOS/FreeBSD (kqueue)
```c
struct {
    int kqueue_fd;
    struct kevent *kqueue_events_selected;
    struct kevent *kqueue_events;
};
```
- Использует `kqueue()` и `kevent()`
- Поддержка различных фильтров событий
- Оптимизирован для сетевых приложений

### Windows (IOCP)
```c
struct {
    HANDLE iocp;
};
```
- Использует I/O Completion Ports
- Асинхронные overlapped операции
- Высокая масштабируемость

## Производительность

### Оптимизации
- **Платформенно-специфичные механизмы** опроса событий
- **Атомарные операции** для счетчиков
- **Минимальные накладные расходы** на переключение
- **Эффективное управление памятью** для структур данных

### Рекомендации
- **CPU привязка:** используйте для изоляции нагрузки
- **Политика планирования:** FIFO для низкой латентности
- **Приоритет:** HIGH для критичных потоков

## Интеграция

### С dap_worker
```c
// Worker наследует context
#define DAP_WORKER(a) (dap_worker_t *)((a)->_inheritor)

// Callback функции
int dap_worker_context_callback_started(dap_context_t *context, void *arg);
int dap_worker_context_callback_stopped(dap_context_t *context, void *arg);
```

### С dap_events_socket
```c
// Добавление сокета в контекст
int result = dap_context_add(context, events_socket);

// Обновление опроса
dap_context_poll_update(events_socket);
```

## Лучшие практики

### 1. Создание контекста
```c
// Создание рабочего контекста
dap_context_t *context = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
if (!context) {
    log_error("Failed to create context");
    return NULL;
}
```

### 2. Запуск с параметрами
```c
// Запуск с привязкой к CPU и высоким приоритетом
int result = dap_context_run(context, 0,  // CPU 0
                           DAP_CONTEXT_POLICY_FIFO,
                           DAP_CONTEXT_PRIORITY_HIGH,
                           DAP_CONTEXT_FLAG_WAIT_FOR_STARTED,
                           on_started, on_stopped, user_data);
```

### 3. Управление жизненным циклом
```c
// Правильное завершение
dap_context_stop_n_kill(context);
dap_context_wait(context);  // Ожидание завершения
```

### 4. Обработка ошибок
```c
// Проверка результатов операций
if (dap_context_add(context, socket) != 0) {
    log_error("Failed to add socket to context");
}
```

## Отладка и мониторинг

### Информация о контексте
```c
dap_context_t *current = dap_context_current();
if (current) {
    printf("Context ID: %u, CPU: %d, Running: %d\n",
           current->id, current->cpu_id, current->is_running);
}
```

### Состояние сокетов
```c
printf("Active sockets: %u\n",
       atomic_load(&context->event_sockets_count));
```

## Типичные проблемы

### 1. Утечки памяти
```
Симптом: Рост потребления памяти
Решение: Правильно удалять сокеты через dap_context_remove()
```

### 2. Конкуренция
```
Симптом: Race conditions при добавлении сокетов
Решение: Использовать только в контексте своего потока
```

### 3. Производительность
```
Симптом: Высокая латентность
Решение: Проверить привязку к CPU и политику планирования
```

## Заключение

Модуль `dap_context` предоставляет мощную и гибкую абстракцию для управления потоками исполнения в DAP SDK. Его платформенно-оптимизированная архитектура обеспечивает высокую производительность и надежность для требовательных приложений.
