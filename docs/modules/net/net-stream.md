# DAP Net Stream Module (dap_stream.h)

## Обзор

Модуль `dap_stream` предоставляет высокопроизводительную потоковую передачу данных для DAP SDK. Он реализует:

- **Двустороннюю потоковую передачу** - асинхронное чтение/запись
- **Многоканальную архитектуру** - поддержка множественных каналов в одном потоке
- **Кластеризацию** - распределенная обработка потоков
- **Безопасность** - криптографическая защита и аутентификация
- **Компрессию** - оптимизация трафика

## Архитектурная роль

Stream модуль является основой для высокопроизводительной коммуникации в DAP:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP Net       │───▶│   Stream        │
│   Модуль        │    │   Модуль        │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │TCP/UDP  │             │Каналы & │
    │сокеты   │             │сессии   │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Низкоуров│◄────────────►│Высокоуров│
    │транспорт│             │протоколы │
    └─────────┘             └─────────┘
```

## Основные структуры данных

### `dap_stream`
```c
typedef struct dap_stream {
    dap_stream_node_addr_t node;          // Адрес узла
    bool authorized;                      // Авторизован ли поток
    bool primary;                         // Основной поток
    int id;                               // Идентификатор потока

    // Управление соединением
    dap_events_socket_t *esocket;         // Сокет событий
    dap_stream_session_t *session;        // Сессия потока

    // Каналы
    dap_stream_ch_t **channels;           // Массив каналов
    size_t channels_count;                // Количество каналов

    // Таймеры
    dap_timerfd_t *keepalive_timer;       // Таймер keepalive

    // Состояние
    bool is_active;                       // Активен ли поток
    pthread_mutex_t mutex;                // Мьютекс для синхронизации

    void *_inheritor;                     // Для наследования
} dap_stream_t;
```

### `dap_stream_session`
```c
typedef struct dap_stream_session {
    uint32_t id;                          // ID сессии
    dap_stream_node_addr_t node_addr;     // Адрес узла
    dap_stream_t *stream;                 // Связанный поток

    // Управление состоянием
    bool is_active;                       // Активна ли сессия
    time_t create_time;                   // Время создания
    time_t last_activity;                 // Последняя активность

    // Каналы сессии
    dap_stream_ch_t *channels;            // Каналы сессии
    size_t channels_count;                // Количество каналов

    // Кластеризация
    dap_cluster_t *cluster;               // Кластер
    uint32_t cluster_member_id;           // ID участника кластера

    UT_hash_handle hh;                    // Для хэш-таблицы
} dap_stream_session_t;
```

### `dap_stream_ch`
```c
typedef struct dap_stream_ch {
    uint8_t type;                         // Тип канала
    uint32_t id;                          // ID канала

    // Связи
    dap_stream_t *stream;                 // Родительский поток
    dap_stream_session_t *session;        // Сессия

    // Буферы
    dap_stream_ch_buf_t *buf;             // Буфер канала
    size_t buf_size;                      // Размер буфера

    // Callbacks
    dap_stream_ch_callback_t ready_to_read;
    dap_stream_ch_callback_t ready_to_write;
    dap_stream_ch_callback_packet_t packet_in;
    dap_stream_ch_callback_packet_t packet_out;

    // Состояние
    bool is_active;                       // Активен ли канал
    uint32_t seq_id;                      // Последовательный ID

    void *_inheritor;                     // Для наследования
} dap_stream_ch_t;
```

## Типы каналов

### Стандартные типы каналов
```c
#define DAP_STREAM_CH_ID_CONTROL   0x00   // Канал управления
#define DAP_STREAM_CH_ID_FILE      0x01   // Файловый канал
#define DAP_STREAM_CH_ID_SERVICE   0x02   // Сервисный канал
#define DAP_STREAM_CH_ID_SECURITY  0x03   // Канал безопасности
#define DAP_STREAM_CH_ID_MEDIA     0x04   // Медиа канал
#define DAP_STREAM_CH_ID_CUSTOM    0x10   // Настраиваемый канал
```

## Основные функции

### Инициализация и управление потоками

#### `dap_stream_init()`
```c
int dap_stream_init();
```

Инициализирует систему потоков.

**Возвращаемые значения:**
- `0` - успешная инициализация
- `-1` - ошибка инициализации

#### `dap_stream_deinit()`
```c
void dap_stream_deinit();
```

Деинициализирует систему потоков.

### Создание и управление сессиями

#### `dap_stream_session_create()`
```c
dap_stream_session_t *dap_stream_session_create(dap_stream_node_addr_t a_node_addr);
```

**Параметры:**
- `a_node_addr` - адрес узла для сессии

**Возвращаемое значение:**
- Указатель на созданную сессию или NULL при ошибке

#### `dap_stream_session_delete()`
```c
void dap_stream_session_delete(dap_stream_session_t *a_session);
```

**Параметры:**
- `a_session` - сессия для удаления

### Работа с каналами

#### `dap_stream_ch_new()`
```c
dap_stream_ch_t *dap_stream_ch_new(dap_stream_t *a_stream, uint8_t a_type);
```

**Параметры:**
- `a_stream` - поток для канала
- `a_type` - тип канала

**Возвращаемое значение:**
- Указатель на созданный канал или NULL при ошибке

#### `dap_stream_ch_delete()`
```c
void dap_stream_ch_delete(dap_stream_ch_t *a_ch);
```

**Параметры:**
- `a_ch` - канал для удаления

### Передача данных

#### `dap_stream_ch_packet_write()`
```c
int dap_stream_ch_packet_write(dap_stream_ch_t *a_ch, uint8_t a_type,
                              const void *a_data, size_t a_data_size);
```

**Параметры:**
- `a_ch` - канал для записи
- `a_type` - тип пакета
- `a_data` - данные для отправки
- `a_data_size` - размер данных

**Возвращаемые значения:**
- `0` - успешная отправка
- `-1` - ошибка отправки

#### `dap_stream_ch_packet_read()`
```c
size_t dap_stream_ch_packet_read(dap_stream_ch_t *a_ch, uint8_t *a_type,
                                void *a_data, size_t a_data_max_size);
```

**Параметры:**
- `a_ch` - канал для чтения
- `a_type` - указатель для типа пакета
- `a_data` - буфер для данных
- `a_data_max_size` - максимальный размер буфера

**Возвращаемое значение:**
- Количество прочитанных байт или 0 при ошибке

## Callback функции

### `dap_stream_ch_callback_t`
```c
typedef void (*dap_stream_ch_callback_t)(dap_stream_ch_t *a_ch, void *a_arg);
```

**Параметры:**
- `a_ch` - канал
- `a_arg` - пользовательские аргументы

### `dap_stream_ch_callback_packet_t`
```c
typedef size_t (*dap_stream_ch_callback_packet_t)(dap_stream_ch_t *a_ch,
                                                 uint8_t a_type,
                                                 void *a_data, size_t a_data_size,
                                                 void *a_arg);
```

**Параметры:**
- `a_ch` - канал
- `a_type` - тип пакета
- `a_data` - данные пакета
- `a_data_size` - размер данных
- `a_arg` - пользовательские аргументы

**Возвращаемое значение:**
- Количество обработанных байт

## Протокол потоков

### Структура пакета
```c
typedef struct dap_stream_packet_hdr {
    uint32_t size;                        // Размер данных
    uint8_t type;                         // Тип пакета
    uint32_t seq_id;                      // Последовательный ID
    uint16_t ch_id;                       // ID канала
    uint8_t flags;                        // Флаги
} __attribute__((packed)) dap_stream_packet_hdr_t;
```

### Типы пакетов
```c
#define DAP_STREAM_PKT_TYPE_DATA          0x01   // Данные
#define DAP_STREAM_PKT_TYPE_CONTROL       0x02   // Управление
#define DAP_STREAM_PKT_TYPE_KEEPALIVE     0x03   // Keepalive
#define DAP_STREAM_PKT_TYPE_CLOSE         0x04   // Закрытие
#define DAP_STREAM_PKT_TYPE_ERROR         0x05   // Ошибка
```

### Флаги пакетов
```c
#define DAP_STREAM_PKT_FLAG_COMPRESSED    0x01   // Сжатые данные
#define DAP_STREAM_PKT_FLAG_ENCRYPTED     0x02   // Зашифрованные данные
#define DAP_STREAM_PKT_FLAG_FRAGMENTED    0x04   // Фрагментированный пакет
```

## Кластеризация

### Структура кластера
```c
typedef struct dap_cluster {
    uint32_t id;                          // ID кластера
    char *name;                           // Имя кластера

    // Участники
    dap_list_t *members;                  // Список участников
    size_t members_count;                 // Количество участников

    // Балансировка
    dap_cluster_balancer_t balancer;      // Балансировщик нагрузки

    // Синхронизация
    pthread_mutex_t mutex;                // Мьютекс
    pthread_cond_t cond;                  // Условная переменная
} dap_cluster_t;
```

### Управление кластером

#### `dap_cluster_add_member()`
```c
int dap_cluster_add_member(dap_cluster_t *a_cluster,
                          dap_stream_node_addr_t a_member_addr);
```

**Параметры:**
- `a_cluster` - кластер
- `a_member_addr` - адрес нового участника

**Возвращаемые значения:**
- `0` - успешное добавление
- `-1` - ошибка добавления

#### `dap_cluster_remove_member()`
```c
int dap_cluster_remove_member(dap_cluster_t *a_cluster,
                             dap_stream_node_addr_t a_member_addr);
```

**Параметры:**
- `a_cluster` - кластер
- `a_member_addr` - адрес участника для удаления

**Возвращаемые значения:**
- `0` - успешное удаление
- `-1` - ошибка удаления

## Безопасность и аутентификация

### Аутентификация потоков
```c
// Верификация подписи потока
bool dap_stream_verify_signature(dap_stream_t *a_stream,
                                dap_sign_t *a_sign);

// Аутентификация узла
bool dap_stream_authenticate_node(dap_stream_t *a_stream,
                                 dap_stream_node_addr_t a_node_addr);
```

### Шифрование данных
```c
// Шифрование пакета
int dap_stream_encrypt_packet(dap_stream_packet_t *a_packet,
                             dap_enc_key_t *a_key);

// Расшифровка пакета
int dap_stream_decrypt_packet(dap_stream_packet_t *a_packet,
                             dap_enc_key_t *a_key);
```

## Производительность и оптимизации

### Оптимизации
- **Zero-copy buffers** - минимизация копирования данных
- **Async I/O** - асинхронные операции ввода-вывода
- **Connection pooling** - переиспользование соединений
- **Adaptive compression** - адаптивное сжатие

### Keepalive механизм
```c
#define STREAM_KEEPALIVE_TIMEOUT 3        // Таймаут keepalive в секундах

// Отправка keepalive пакета
void dap_stream_send_keepalive(dap_stream_t *a_stream);

// Обработка keepalive пакета
void dap_stream_process_keepalive(dap_stream_t *a_stream);
```

### Статистика производительности
```c
typedef struct dap_stream_stats {
    uint64_t packets_sent;                // Отправлено пакетов
    uint64_t packets_received;            // Получено пакетов
    uint64_t bytes_sent;                  // Отправлено байт
    uint64_t bytes_received;              // Получено байт
    uint32_t active_channels;             // Активных каналов
    double avg_latency;                   // Средняя задержка
} dap_stream_stats_t;

// Получение статистики
dap_stream_stats_t dap_stream_get_stats(dap_stream_t *a_stream);
```

## Использование

### Базовая настройка потока

```c
#include "dap_stream.h"
#include "dap_stream_session.h"

// Инициализация системы потоков
if (dap_stream_init() != 0) {
    fprintf(stderr, "Failed to initialize stream system\n");
    return -1;
}

// Создание сессии
dap_stream_node_addr_t node_addr = {.addr = inet_addr("127.0.0.1"), .port = 8080};
dap_stream_session_t *session = dap_stream_session_create(node_addr);

if (!session) {
    fprintf(stderr, "Failed to create stream session\n");
    return -1;
}

// Создание потока
dap_stream_t *stream = dap_stream_create(session);
if (!stream) {
    fprintf(stderr, "Failed to create stream\n");
    return -1;
}

// Основная работа с потоком
// ...

// Очистка
dap_stream_delete(stream);
dap_stream_session_delete(session);
dap_stream_deinit();
```

### Работа с каналами

```c
// Callback для обработки входящих пакетов
size_t packet_handler(dap_stream_ch_t *ch, uint8_t type,
                     void *data, size_t data_size, void *arg) {
    printf("Received packet type: %d, size: %zu\n", type, data_size);

    // Обработка данных в зависимости от типа
    switch (type) {
        case DAP_STREAM_PKT_TYPE_DATA:
            process_data_packet(data, data_size);
            break;
        case DAP_STREAM_PKT_TYPE_CONTROL:
            process_control_packet(data, data_size);
            break;
        default:
            fprintf(stderr, "Unknown packet type: %d\n", type);
            return 0;
    }

    return data_size; // Количество обработанных байт
}

// Создание канала
dap_stream_ch_t *channel = dap_stream_ch_new(stream, DAP_STREAM_CH_ID_CONTROL);
if (!channel) {
    fprintf(stderr, "Failed to create channel\n");
    return -1;
}

// Регистрация callback для входящих пакетов
channel->packet_in = packet_handler;

// Отправка данных
const char *message = "Hello, World!";
if (dap_stream_ch_packet_write(channel, DAP_STREAM_PKT_TYPE_DATA,
                              message, strlen(message)) != 0) {
    fprintf(stderr, "Failed to send packet\n");
}
```

### Асинхронная обработка

```c
// Callback для готовности к чтению
void ready_to_read_callback(dap_stream_ch_t *ch, void *arg) {
    uint8_t packet_type;
    char buffer[1024];

    size_t bytes_read = dap_stream_ch_packet_read(ch, &packet_type,
                                                 buffer, sizeof(buffer));

    if (bytes_read > 0) {
        printf("Read %zu bytes of type %d\n", bytes_read, packet_type);
        // Обработка полученных данных
        process_received_data(buffer, bytes_read);
    }
}

// Регистрация callback'ов
channel->ready_to_read = ready_to_read_callback;
channel->ready_to_write = ready_to_write_callback;

// Запуск асинхронной обработки
dap_stream_start_async_processing(stream);
```

### Работа с кластерами

```c
// Создание кластера
dap_cluster_t *cluster = dap_cluster_create("my_cluster");
if (!cluster) {
    fprintf(stderr, "Failed to create cluster\n");
    return -1;
}

// Добавление участников
dap_stream_node_addr_t member1 = {.addr = inet_addr("192.168.1.10"), .port = 8080};
dap_stream_node_addr_t member2 = {.addr = inet_addr("192.168.1.11"), .port = 8080};

dap_cluster_add_member(cluster, member1);
dap_cluster_add_member(cluster, member2);

// Присоединение сессии к кластеру
dap_stream_session_join_cluster(session, cluster);

// Автоматическая балансировка нагрузки
dap_cluster_enable_load_balancing(cluster, true);
```

## Продвинутые возможности

### Кастомные каналы

```c
// Определение нового типа канала
#define DAP_STREAM_CH_ID_CUSTOM_ENCRYPTED 0x20

// Создание кастомного канала
dap_stream_ch_t *encrypted_channel = dap_stream_ch_new(
    stream, DAP_STREAM_CH_ID_CUSTOM_ENCRYPTED);

// Настройка шифрования для канала
dap_enc_key_t *channel_key = dap_enc_key_generate(DAP_ENC_KEY_TYPE_AES, 256);
dap_stream_ch_set_encryption(encrypted_channel, channel_key);

// Использование зашифрованного канала
dap_stream_ch_packet_write(encrypted_channel,
                          DAP_STREAM_PKT_TYPE_DATA,
                          sensitive_data, data_size);
```

### Мониторинг и отладка

```c
// Включение отладки
extern int g_dap_stream_debug_more;
g_dap_stream_debug_more = 1;

// Получение статистики
dap_stream_stats_t stats = dap_stream_get_stats(stream);
printf("Packets sent: %llu\n", stats.packets_sent);
printf("Packets received: %llu\n", stats.packets_received);
printf("Active channels: %u\n", stats.active_channels);
printf("Average latency: %.2f ms\n", stats.avg_latency);

// Мониторинг состояния каналов
for (size_t i = 0; i < stream->channels_count; i++) {
    dap_stream_ch_t *ch = stream->channels[i];
    if (ch->is_active) {
        printf("Channel %u: active, seq_id: %u\n", ch->id, ch->seq_id);
    }
}
```

## Интеграция с другими модулями

### DAP Events
- Асинхронная обработка событий
- Управление таймерами
- Callbacks для готовности I/O

### DAP Crypto
- Шифрование данных каналов
- Цифровые подписи пакетов
- Аутентификация участников

### DAP Net Server
- HTTP-over-Stream
- WebSocket поддержка
- REST API интеграция

## Типичные проблемы

### 1. Потеря пакетов
```
Симптом: Пропадают пакеты в высоконагруженных каналах
Решение: Увеличить размер буферов и настроить flow control
```

### 2. Высокая латентность
```
Симптом: Большая задержка в передаче данных
Решение: Оптимизировать размер пакетов и частоту keepalive
```

### 3. Перегрузка каналов
```
Симптом: Переполнение буферов каналов
Решение: Внедрить rate limiting и backpressure механизм
```

## Заключение

Модуль `dap_stream` предоставляет мощную и гибкую систему потоковой передачи данных с поддержкой множественных каналов, кластеризации и высокой производительности. Его архитектура оптимизирована для сетевых приложений, требующих надежной и эффективной коммуникации.
