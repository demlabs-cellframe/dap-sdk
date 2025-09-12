# DAP Worker Module (dap_worker.h)

## Обзор

Модуль `dap_worker.h` реализует концепцию рабочих потоков в системе асинхронных событий DAP SDK. Каждый рабочий поток представляет собой независимую единицу обработки, оптимизированную для эффективного управления событиями и сокетами.

## Архитектурная роль

Рабочие потоки являются ключевым элементом масштабируемой архитектуры DAP:

```
┌─────────────────┐
│   dap_events    │  ← Координатор рабочих потоков
└─────────────────┘
         │
    ┌────▼────┐
    │ dap_worker │ ← Управление очередями и контекстом
    └────┬────┘
         │
    ┌────▼────┐
    │dap_context│ ← Платформенно-зависимая обработка
    └─────────┘
```

## Структура данных

### `dap_worker_t`
```c
typedef struct dap_worker {
    uint32_t id;                           // Уникальный идентификатор потока
    dap_proc_thread_t *proc_queue_input;   // Входная очередь обработки

    // Очереди сокетов (не для IOCP)
    dap_events_socket_t *queue_es_new;     // Новые сокеты
    dap_events_socket_t *queue_es_delete;  // Сокеты для удаления
    dap_events_socket_t *queue_es_io;      // Сокеты для I/O операций

    dap_timerfd_t *timer_check_activity;   // Таймер проверки активности
    dap_context_t *context;               // Контекст выполнения

    void *_inheritor;                     // Указатель для наследования
} dap_worker_t;
```

## Основные функции

### Инициализация и деинициализация

#### `dap_worker_init()`
```c
int dap_worker_init(size_t a_conn_timeout);
```

Инициализирует систему рабочих потоков.

**Параметры:**
- `a_conn_timeout` - таймаут соединения в миллисекундах

**Возвращаемые значения:**
- `0` - успешная инициализация
- `-1` - ошибка инициализации

#### `dap_worker_deinit()`
```c
void dap_worker_deinit();
```

Деинициализирует систему рабочих потоков.

### Управление сокетами

#### `dap_worker_add_events_socket()`
```c
void dap_worker_add_events_socket(dap_worker_t *a_worker,
                                  dap_events_socket_t *a_events_socket);
```

Добавляет сокет событий в рабочий поток.

**Параметры:**
- `a_worker` - указатель на рабочий поток
- `a_events_socket` - сокет событий для добавления

#### `dap_worker_add_events_socket_unsafe()`
```c
int dap_worker_add_events_socket_unsafe(dap_worker_t *a_worker,
                                        dap_events_socket_t *a_esocket);
```

Небезопасная версия добавления сокета (без блокировок).

#### `dap_worker_add_events_socket_auto()`
```c
dap_worker_t *dap_worker_add_events_socket_auto(dap_events_socket_t *a_es);
```

Автоматически добавляет сокет в оптимальный рабочий поток.

**Возвращаемое значение:**
- Указатель на рабочий поток, принявший сокет

### Работа с callback функциями

#### `dap_worker_exec_callback_on()`
```c
void dap_worker_exec_callback_on(dap_worker_t *a_worker,
                                 dap_worker_callback_t a_callback,
                                 void *a_arg);
```

Выполняет callback функцию в указанном рабочем потоке.

**Параметры:**
- `a_worker` - рабочий поток для выполнения
- `a_callback` - функция обратного вызова
- `a_arg` - аргумент для callback функции

**Тип callback функции:**
```c
typedef void (*dap_worker_callback_t)(void *a_arg);
```

### Управление контекстом

#### `dap_worker_get_current()`
```c
dap_worker_t *dap_worker_get_current();
```

Возвращает рабочий поток текущего контекста исполнения.

**Возвращаемое значение:**
- Текущий рабочий поток или NULL

### Вспомогательные функции

#### `dap_worker_check_esocket_polled_now()`
```c
bool dap_worker_check_esocket_polled_now();
```

Проверяет, опрашивается ли сокет в текущий момент.

**Возвращаемое значение:**
- `true` - сокет активно опрашивается
- `false` - сокет не опрашивается

## Вспомогательные структуры

### `dap_worker_msg_reassign_t`
```c
typedef struct dap_worker_msg_reassign {
    dap_events_socket_t *esocket;         // Сокет для переназначения
    dap_events_socket_uuid_t esocket_uuid; // UUID сокета
    dap_worker_t *worker_new;             // Новый рабочий поток
} dap_worker_msg_reassign_t;
```

Структура для переназначения сокета между рабочими потоками.

### `dap_worker_msg_io_t`
```c
typedef struct dap_worker_msg_io {
    dap_events_socket_uuid_t esocket_uuid; // UUID сокета
    size_t data_size;                     // Размер данных
    void *data;                           // Указатель на данные
    uint32_t flags_set;                   // Флаги для установки
    uint32_t flags_unset;                 // Флаги для снятия
} dap_worker_msg_io_t;
```

Структура для I/O операций между рабочими потоками.

## Макросы

### `DAP_WORKER()`
```c
#define DAP_WORKER(a) (dap_worker_t *)((a)->_inheritor)
```

Макрос для получения указателя на рабочий поток из структуры-наследника.

## Алгоритм работы

### 1. Инициализация рабочего потока
```
dap_events_init() → Создание рабочих потоков
    ↓
Для каждого потока: dap_context_new()
    ↓
dap_context_run() с callback функциями
    ↓
Готовность к обработке событий
```

### 2. Обработка сокетов
```
Новый сокет → dap_worker_add_events_socket_auto()
    ↓
Автоматический выбор рабочего потока
    ↓
Добавление в очередь queue_es_new
    ↓
Обработка в контексте рабочего потока
```

### 3. Выполнение callback функций
```
Запрос callback → dap_worker_exec_callback_on()
    ↓
Добавление в очередь callback
    ↓
Выполнение в контексте рабочего потока
    ↓
Возврат результата
```

## Оптимизации производительности

### Lock-free структуры
- Очереди реализованы без блокировок для минимизации задержек
- Использование атомарных операций для синхронизации
- Эффективное распределение нагрузки между потоками

### Автоматическая балансировка
```c
// Автоматический выбор наименее загруженного потока
dap_worker_t *worker = dap_events_worker_get_auto();
```

### Платформенные оптимизации
- **Linux:** использование epoll для эффективного опроса
- **macOS/BSD:** использование kqueue для управления событиями
- **Windows:** использование IOCP для overlapped I/O

## Интеграция с другими модулями

### С dap_events
```c
// Через events API
dap_worker_t *worker = dap_events_worker_get_auto();

// Через worker API
dap_worker_add_events_socket(worker, socket);
```

### С dap_context
```c
// Callback функции для жизненного цикла
int dap_worker_context_callback_started(dap_context_t *context, void *arg);
int dap_worker_context_callback_stopped(dap_context_t *context, void *arg);

// Основной цикл обработки
int dap_worker_thread_loop(dap_context_t *context);
```

### С dap_events_socket
```c
// Добавление сокета в рабочий поток
dap_worker_add_events_socket(worker, events_socket);

// Проверка статуса опроса
if (dap_worker_check_esocket_polled_now()) {
    // Сокет активно обрабатывается
}
```

## Лучшие практики

### 1. Выбор рабочего потока
```c
// Для новых соединений - автоматический выбор
dap_worker_t *worker = dap_events_worker_get_auto();

// Для связанных операций - один и тот же поток
static dap_worker_t *my_worker = NULL;
if (!my_worker) {
    my_worker = dap_events_worker_get_auto();
}
```

### 2. Callback функции
```c
void my_callback(void *arg) {
    // Безопасно выполнять в контексте рабочего потока
    process_data(arg);
}

// Выполнение в рабочем потоке
dap_worker_exec_callback_on(worker, my_callback, data);
```

### 3. Управление ресурсами
```c
// Правильная последовательность завершения
dap_events_stop_all();  // Остановка всех потоков
dap_events_wait();      // Ожидание завершения
dap_events_deinit();    // Освобождение ресурсов
```

## Отладка и мониторинг

### Информация о потоках
```c
// Вывод информации о всех рабочих потоках
dap_worker_print_all();
```

### Проверка состояния
```c
// Получение текущего рабочего потока
dap_worker_t *current = dap_worker_get_current();
if (current) {
    printf("Current worker ID: %u\n", current->id);
}
```

## Типичные проблемы

### 1. Гонка данных
```
Симптом: Нестабильное поведение при добавлении сокетов
Решение: Использовать _unsafe версии только в однопоточном контексте
```

### 2. Перегрузка потоков
```
Симптом: Высокая латентность обработки событий
Решение: Проверить автоматическую балансировку нагрузки
```

### 3. Утечки памяти
```
Симптом: Рост потребления памяти со временем
Решение: Правильно удалять сокеты через queue_es_delete
```

## Заключение

Модуль `dap_worker` предоставляет эффективную абстракцию для управления рабочими потоками в системе асинхронных событий DAP SDK. Его дизайн обеспечивает высокую производительность, масштабируемость и надежность для требовательных сетевых приложений.

