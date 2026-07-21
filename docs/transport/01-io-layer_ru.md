# IO слой DAP SDK — Архитектура и реализация

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Модуль:** `dap-sdk/io/`

## Обзор

IO слой — фундамент сетевого стека DAP SDK. Предоставляет кроссплатформенный event-driven ввод-вывод с моделью «worker thread pool», где каждый воркер обрабатывает своё подмножество сокетов. Все вышележащие слои (транспорт, stream, каналы) строятся поверх этого абстрактного IO.

**Место в стеке:**

```
┌─────────────────────────────────────────────────────┐
│ L4: Приложения (VPN, Services, Channels)            │
├─────────────────────────────────────────────────────┤
│ L3: DAP Stream Protocol (dap_stream_t)              │
├─────────────────────────────────────────────────────┤
│ L2: Transport Abstraction (dap_net_trans_t)         │
├─────────────────────────────────────────────────────┤
│ L1: Конкретные транспорты (TLS, UDP, HTTP, DNS)    │
├─────────────────────────────────────────────────────┤
│ L0: IO слой ← ЭТОТ ДОКУМЕНТ                        │
│     (dap_events, dap_events_socket, dap_worker)     │
└─────────────────────────────────────────────────────┘
```

## Архитектура Event Loop

### Платформенная абстракция

IO слой автоматически выбирает механизм event notification в зависимости от платформы:

| Платформа | Механизм | Определение |
|-----------|----------|-------------|
| Linux | epoll | `DAP_EVENTS_CAPS_EPOLL` |
| macOS / iOS | kqueue | `DAP_EVENTS_CAPS_KQUEUE` |
| BSD | kqueue | `DAP_EVENTS_CAPS_KQUEUE` |
| Windows | IOCP | `DAP_EVENTS_CAPS_IOCP` |
| Windows (альт.) | wepoll (epoll поверх IOCP) | `DAP_EVENTS_CAPS_WEPOLL` |
| Android / fallback | poll | `DAP_EVENTS_CAPS_POLL` |

### Модель потоков

```
┌──────────────────────────────────────────────────┐
│                  dap_events_t                     │
│              (главный event loop)                 │
│                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ Worker 0 │ │ Worker 1 │ │ Worker N │  ...     │
│  │ (thread) │ │ (thread) │ │ (thread) │         │
│  │          │ │          │ │          │         │
│  │ es[0..M] │ │ es[0..M] │ │ es[0..M] │         │
│  └──────────┘ └──────────┘ └──────────┘         │
│                                                  │
│  ┌──────────────────────────────────────┐        │
│  │         dap_proc_thread_t            │        │
│  │    (processing thread pool)          │        │
│  └──────────────────────────────────────┘        │
└──────────────────────────────────────────────────┘
```

- **Worker thread** (`dap_worker_t`) — каждый воркер имеет свой event loop и обрабатывает набор сокетов. Сокеты привязываются к воркеру при создании или переназначаются балансировщиком.
- **Proc thread** (`dap_proc_thread_t`) — пул потоков для тяжёлых вычислительных задач (не IO).
- Sticky binding: сокет привязан к одному воркеру на весь жизненный цикл. Переназначение между воркерами возможно через `dap_events_socket_reassign_between_workers_mt()`.

## Ключевая структура: dap_events_socket_t

`dap_events_socket_t` — центральная абстракция IO слоя. Представляет любой дескриптор: TCP-сокет, UDP-сокет, pipe, timer, event notification.

### Основные поля

```c
typedef struct dap_events_socket {
    SOCKET              socket;          // Дескриптор сокета
    dap_events_desc_type_t type;         // Тип дескриптора
    dap_events_socket_uuid_t uuid;       // Уникальный ID (uint64_t)

    // Буферы
    byte_t              *buf_in;         // Входной буфер
    byte_t              *buf_out;        // Выходной буфер
    size_t              buf_in_size, buf_in_size_max;
    size_t              buf_out_size, buf_out_size_max;

    // Очередь дейтаграмм (для UDP/SCTP)
    dap_events_socket_packet_queue_t *packet_queue;

    // Адресация
    struct sockaddr_storage addr_storage;
    char                remote_addr_str[256];
    uint16_t            remote_port;

    // Привязки
    dap_context_t       *context;
    dap_worker_t        *worker;
    dap_server_t        *server;

    // Callbacks
    dap_events_socket_callbacks_t callbacks;

    // Пользовательские данные
    void                *_inheritor;     // Публичные данные (наследник)
    void                *_pvt;           // Приватные данные

    // Флаги
    uint32_t            flags;
    atomic_bool         is_initalized;
} dap_events_socket_t;
```

### Типы дескрипторов

```c
typedef enum dap_events_desc_type {
    DESCRIPTOR_TYPE_SOCKET_CLIENT,          // TCP клиент
    DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT,    // Unix domain socket клиент
    DESCRIPTOR_TYPE_SOCKET_LISTENING,       // TCP listening socket
    DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING, // Unix domain listening
    DESCRIPTOR_TYPE_SOCKET_UDP,             // UDP socket
    DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL,      // SSL/TLS клиент
    DESCRIPTOR_TYPE_SOCKET_RAW,             // Raw socket
    DESCRIPTOR_TYPE_FILE,                   // Файловый дескриптор
    DESCRIPTOR_TYPE_PIPE,                   // Pipe
    DESCRIPTOR_TYPE_TIMER,                  // Timer fd
    DESCRIPTOR_TYPE_EVENT                   // Event notification
} dap_events_desc_type_t;
```

### Флаги сокета

| Флаг | Бит | Описание |
|------|-----|----------|
| `DAP_SOCK_READY_TO_READ` | 0 | Сокет готов к чтению |
| `DAP_SOCK_READY_TO_WRITE` | 1 | Сокет готов к записи |
| `DAP_SOCK_SIGNAL_CLOSE` | 2 | Сигнал на закрытие |
| `DAP_SOCK_CONNECTING` | 3 | В процессе подключения |
| `DAP_SOCK_REASSIGN_ONCE` | 4 | Одноразовое переназначение |
| `DAP_SOCK_FILE_MAPPED` | 7 | Memory-mapped файл |
| `DAP_SOCK_MSG_ORIENTED` | 8 | Дейтаграммный сокет (UDP/SCTP) |

## Поток данных

### Чтение (stream socket)

```
1. Event loop (epoll/kqueue) детектирует readability
2. → worker вызывает esocket->callbacks.read_callback()
3. → данные читаются в buf_in
4. → вышележащий слой (stream) парсит пакеты из buf_in
5. → буфер очищается после обработки
```

### Запись (stream socket)

```
1. Вышележащий слой вызывает dap_events_socket_write(esocket, data, size)
2. → данные записываются в buf_out
3. → esocket помечается как DAP_SOCK_READY_TO_WRITE
4. → event loop детектирует writability
5. → worker вызывает write_callback() → отправляет buf_out в socket
6. → buf_out очищается
```

### Дейтаграммы (UDP)

```
1. Event loop детектирует readability на UDP socket
2. → worker вызывает read_callback()
3. → recvfrom() читает дейтаграмму + адрес отправителя
4. → данные + адрес упаковываются в dap_events_socket_packet_t
5. → пакет помещается в packet_queue (ring buffer)
6. → вышележащий слой извлекает пакеты из очереди
```

Для отправки дейтаграмм используется `dap_events_socket_sendto_unsafe()`, который вызывает `sendto()` с указанным адресом назначения.

## Очередь дейтаграмм

```c
typedef struct dap_events_socket_packet_queue {
    dap_events_socket_packet_t  *packets;    // Массив пакетов
    size_t                      count;       // Текущее количество
    size_t                      capacity;    // Максимальная ёмкость
    size_t                      head;        // Индекс головы (ring buffer)
} dap_events_socket_packet_queue_t;

typedef struct dap_events_socket_packet {
    uint8_t     *data;
    size_t      size;
    struct sockaddr_storage addr;
    socklen_t   addr_len;
} dap_events_socket_packet_t;
```

Ring buffer обеспечивает O(1) вставку и извлечение. Ёмкость по умолчанию: `DAP_QUEUE_MAX_MSGS = 1024`.

## IO Flow абстракция

Помимо базовых сокетов, IO слой предоставляет абстракцию «flow» — высокоуровневый поток данных с платформенными оптимизациями:

| Модуль | Описание | Платформа |
|--------|----------|-----------|
| `dap_io_flow.c` | Базовая абстракция flow | Все |
| `dap_io_flow_ctrl.c` | Управление потоком (flow control) | Все |
| `dap_io_flow_datagram.c` | Datagram flow | Все |
| `dap_io_flow_socket.c` | Socket-based flow | Все |
| `dap_io_flow_cbpf.c` | Classic BPF фильтрация | Linux |
| `dap_io_flow_ebpf.c` | eBPF фильтрация и маршрутизация | Linux |
| `dap_io_flow_bsd_lb.c` | Балансировка нагрузки | BSD |
| `dap_io_flow_darwin_gcd.c` | Grand Central Dispatch | macOS |
| `dap_io_flow_win_rio.c` | Registered I/O | Windows |

## Автоматический выбор воркера

При создании нового сокета система автоматически выбирает воркер с наименьшей нагрузкой через `dap_events_worker_get_auto()`:

**Алгоритм:**
1. Найти `l_min_count` — минимальное `event_sockets_count` среди всех воркеров
2. Атомарно инкрементировать `s_worker_rr_counter` (round-robin)
3. Обойти воркеров начиная с `(l_rr + i) % s_threads_count`, вернуть первого с `event_sockets_count == l_min_count`
4. Fallback: `s_workers[l_rr % s_threads_count]`

Это предотвращает thundering herd на воркере 0 и обеспечивает равномерное распределение.

## Activity Check (таймаут неактивных соединений)

Воркеры периодически проверяют активность сокетов:

| Параметр | Значение | Описание |
|----------|----------|----------|
| `s_connection_timeout` | 60 сек | Таймаут неактивности (по умолчанию) |
| Проверка каждые | 30 сек | Таймер срабатывает каждые `timeout / 2` |
| Условие закрытия | `last_time_active + timeout < now` | Сокет без активности дольше 60 сек |

При обнаружении неактивного сокета:
1. Вызов `error_callback(ETIMEDOUT)`
2. Удаление сокета из хеш-таблицы воркера
3. Проверка соответствия счётчика `event_sockets_count` и реального количества в хеш-таблице

## Ключевые константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `DAP_EVENTS_SOCKET_MAX` | 8194 | Максимум сокетов на worker |
| `DAP_STREAM_PKT_FRAGMENT_SIZE` | 16 KB | Размер фрагмента пакета |
| `DAP_STREAM_PKT_SIZE_MAX` | 4 MB | Максимальный размер пакета |
| `DAP_EVENTS_SOCKET_BUF_SIZE` | 256 KB | Размер буфера сокета (16 × 16KB) |
| `DAP_EVENTS_SOCKET_BUF_LIMIT` | 4 MB | Лимит буфера |
| `DAP_QUEUE_MAX_MSGS` | 1024 | Максимум сообщений в очереди |
| `DAP_HOSTADDR_STRLEN` | 256 | Длина строки адреса |
| `DAP_UDP_MAX_DATAGRAM_SIZE` | 65507 | Максимальный размер UDP дейтаграммы |
| `DAP_PACKET_QUEUE_INITIAL_CAPACITY` | 16 | Начальная ёмкость очереди дейтаграмм |
| `DAP_PACKET_QUEUE_MAX_CAPACITY` | 4096 | Максимальная ёмкость очереди дейтаграмм |

## Callback модель

```c
typedef struct dap_events_socket_callbacks {
    union {
        dap_events_socket_callback_t connected_callback;  // TCP connected
        dap_events_socket_callback_t accept_callback;     // New connection
        dap_events_socket_callback_t event_callback;      // Event signal
        dap_events_socket_callback_t queue_callback;      // Queue message
    };
    dap_events_socket_callback_t timer_callback;          // Timer fired
    dap_events_socket_callback_t new_callback;            // Socket created
    dap_events_socket_callback_t delete_callback;         // Socket deleted
    dap_events_socket_callback_t read_callback;           // Data available
    dap_events_socket_callback_t write_callback;          // Ready to write
    dap_events_socket_callback_t write_finished_callback; // Write complete
    dap_events_socket_callback_t error_callback;          // Error occurred
    dap_events_socket_callback_t worker_assign_callback;  // Assigned to worker
    dap_events_socket_callback_t worker_unassign_callback;// Unassigned
    void *arg;                                            // User data
} dap_events_socket_callbacks_t;
```

## Потокобезопасность

- **Unsafe функции** (`_unsafe` суффикс) — вызываются только в контексте воркера, которому принадлежит сокет. Без локов, максимальная производительность.
- **MT-safe функции** (`_mt` суффикс) — используют межпоточную коммуникацию (event signal или queue) для безопасного вызова из любого потока.
- **Inter-context** (`_inter` суффикс) — кросс-воркер операции через context queue.

Типичный паттерн: вышележащий код получает UUID сокета и использует `_mt` функции для записи из другого потока.

## Связанные документы

- [02 — Transport Abstraction Layer](02-transport-abstraction_ru.md)
- [01 — DAP Stream Protocol](../protocol/01-stream-protocol_ru.md)
- Заголовочные файлы: `io/include/dap_events_socket.h`, `dap_events.h`, `dap_worker.h`
