# DAP Stream Protocol — Ядро потокового протокола

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Модуль:** `dap-sdk/net/stream/stream/`, `session/`

## Обзор

DAP Stream Protocol — третий уровень сетевого стека DAP SDK. Предоставляет бинарный потоковый протокол с фрагментацией, мультиплексированием каналов, шифрованием и keepalive. Работает поверх любого транспорта (L2) через Transport Abstraction Layer.

## Архитектура

```
┌──────────────────────────────────────────────────────────┐
│ L4: Каналы (dap_stream_ch_t) — VPN, GlobalDB, Chain     │
├──────────────────────────────────────────────────────────┤
│ L3: DAP Stream Protocol ← ЭТОТ ДОКУМЕНТ                  │
│     dap_stream_t → dap_stream_pkt_t → dap_stream_ch_pkt_t│
├──────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction (dap_net_trans_t)              │
├──────────────────────────────────────────────────────────┤
│ L1: TLS / UDP / HTTP / DNS / WebSocket                   │
├──────────────────────────────────────────────────────────┤
│ L0: IO (dap_events_socket_t)                             │
└──────────────────────────────────────────────────────────┘
```

## Ключевая структура: dap_stream_t

`dap_stream_t` представляет один бинарный поток между двумя узлами:

```c
typedef struct dap_stream {
    dap_stream_node_addr_t  node;              // Адрес узла
    bool                    authorized;        // Авторизован
    bool                    primary;           // Основной поток
    int                     id;                // ID потока

    dap_stream_session_t    *session;          // Сессия
    dap_stream_worker_t     *stream_worker;    // Воркер

    // Каналы
    dap_stream_ch_t         **channel;         // Массив каналов
    size_t                  channel_count;

    // Секвенирование
    size_t                  seq_id;            // Текущий sequence ID
    size_t                  stream_size;       // Размер потока
    size_t                  client_last_seq_id_packet;

    // Фрагментация
    uint8_t                 *buf_fragments;    // Буфер реассемблеи
    size_t                  buf_fragments_size_total;
    size_t                  buf_fragments_size_filled;
    uint8_t                 *pkt_cache;        // Кэш пакетов

    // Keepalive
    dap_timerfd_t           *keepalive_timer;
    uint64_t                keepalive_timer_uuid;
    struct dap_worker       *keepalive_timer_worker; // Воркер таймера

    // Транспорт
    struct dap_net_trans    *trans;            // Общий транспорт (не владеем)
    dap_net_trans_ctx_t     *trans_ctx;        // Контекст транспорта
    void                    *flow;             // Datagram flow (для UDP/SCTP)

    // Привязки
    dap_events_socket_t     *esocket;          // UNSAFE: только в контексте worker
    dap_events_socket_uuid_t esocket_uuid;     // SAFE: кросс-поточная ссылка
    dap_worker_t            *esocket_worker;

    // Состояние
    bool                    is_active;
    bool                    is_deleting;       // Защита от double-free
    bool                    is_client_to_uplink;
    char                    *service_key;      // Ключ авторизации

    // Серверные/клиентские ссылки
    void                    *_server_session;  // Серверная сессия (NULL на клиенте)
    dap_stream_t            **client_stream_ref; // Клиентская ссылка (NULL на сервере)

    UT_hash_handle          hh;               // Хеш-таблица по адресу
    struct dap_stream       *prev, *next;      // Связный список
} dap_stream_t;
```

## Stream Packet (dap_stream_pkt_t)

### Заголовок пакета

```c
typedef struct dap_stream_pkt_hdr {
    uint8_t     sig[8];       // Сигнатура для детекции границ пакетов
    uint32_t    size;         // Размер данных
    uint64_t    timestamp;    // Временная метка
    uint8_t     type;         // Тип пакета
    uint64_t    src_addr;     // Адрес источника
    uint64_t    dst_addr;     // Адрес назначения
} __attribute__((packed)) dap_stream_pkt_hdr_t;  // 37 bytes
```

### Сигнатура пакета

8-байтная сигнатура используется для обнаружения границ пакетов в потоке данных:

```c
static const uint8_t c_dap_stream_sig[8] = {
    0xa0, 0x95, 0x96, 0xa9, 0x9e, 0x5c, 0xfb, 0xfa
};
```

Алгоритм детекции:
1. Поиск первого байта сигнатуры через `memchr`
2. Проверка полной 8-байтной сигнатуры
3. Валидация размера пакета против `DAP_STREAM_PKT_SIZE_MAX`

### Типы пакетов

| Тип | Значение | Описание |
|-----|----------|----------|
| `STREAM_PKT_TYPE_DATA_PACKET` | 0x00 | Пакет данных (полный или последний фрагмент) |
| `STREAM_PKT_TYPE_FRAGMENT_PACKET` | 0x01 | Фрагмент большого пакета |
| `STREAM_PKT_TYPE_SERVICE_PACKET` | 0xFF | Служебный пакет (session check) |
| `STREAM_PKT_TYPE_KEEPALIVE` | 0x11 | Keepalive запрос |
| `STREAM_PKT_TYPE_ALIVE` | 0x12 | Keepalive ответ |

### Полный пакет

```c
typedef struct dap_stream_pkt {
    dap_stream_pkt_hdr_t    hdr;
    uint8_t                 data[];    // Flexible array member
} __attribute__((packed)) dap_stream_pkt_t;
```

### Wire format пакета

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        sig[0..7] (8 bytes)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           sig cont.           |         size (4 bytes)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       timestamp (8 bytes)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  timestamp cont.|  type (1)   |       src_addr (8 bytes)      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       src_addr cont.                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       dst_addr (8 bytes)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       dst_addr cont.                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       data[0..size-1]                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Overhead:** 37 bytes заголовок + до 200 bytes шифрования (`DAP_STREAM_PKT_ENCRYPTION_OVERHEAD`).

## Фрагментация

Для передачи больших пакетов через транспорты с ограниченным MTU (UDP=1200, DNS=500) используется фрагментация:

```c
typedef struct dap_stream_fragment_pkt {
    uint32_t    size;         // Размер этого фрагмента
    uint32_t    mem_shift;    // Смещение в оригинальном пакете
    uint32_t    full_size;    // Полный размер оригинального пакета
    uint8_t     data[];       // Данные фрагмента
} __attribute__((packed)) dap_stream_fragment_pkt_t;  // 12 bytes header
```

### Wire format фрагмента

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              size (4)         |         mem_shift (4)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            full_size (4)      |        data[0..size-1]        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Алгоритм фрагментации

1. Получить MTU транспорта: `trans->ops->get_max_packet_size()` (UDP=1200, DNS=500, TCP=0)
2. Вычислить максимальный размер фрагмента: `mtu - DAP_STREAM_PKT_ENCRYPTION_OVERHEAD - sizeof(dap_stream_fragment_pkt_t)`
3. Если данные помещаются в один фрагмент → отправка без фрагментации
4. Если нет → нарезка на фрагменты с заполнением `size`, `mem_shift`, `full_size`
5. Первый фрагмент включает заголовок канала (`dap_stream_ch_pkt_hdr_t`), остальные — чистые данные

### Алгоритм реассемблеи

1. При получении `STREAM_PKT_TYPE_FRAGMENT_PACKET`:
2. Проверить `buf_fragments_size_filled == mem_shift` (порядок)
3. Скопировать данные фрагмента в `buf_fragments` по смещению
4. Если `buf_fragments_size_filled == full_size` → все фрагменты получены
5. Обработать как `STREAM_PKT_TYPE_DATA_PACKET`

## Stream Session (dap_stream_session_t)

Сессия представляет активное streaming-соединение:

```c
typedef struct dap_stream_session {
    bool                    create_empty;      // Создать пустую сессию
    uint32_t                id;                // ID сессии
    uint32_t                media_id;          // Media ID
    dap_enc_key_t           *key;              // Ключ шифрования
    bool                    open_preview;      // Открыть превью
    pthread_mutex_t         mutex;
    int                     opened;
    dap_time_t              time_created;
    uint8_t                 enc_type;          // Тип шифрования
    int32_t                 protocol_version;  // Версия протокола
    char                    *service_key;      // Авторизационный ключ
    char                    active_channels[16]; // Активные каналы (строка, напр. "C,F,N")
    stream_session_connection_type_t conn_type; // Тип соединения (HTTP/UDP)
    stream_session_type_t   type;              // Тип сессии (MEDIA/VPN)
    uint8_t                 *acl;              // Access Control List
    dap_stream_node_addr_t  node;              // Адрес узла
    UT_hash_handle          hh;               // Хеш-таблица по ID
    struct in_addr          tun_client_addr;   // TUN адрес клиента
    void                    *_inheritor;       // Точка расширения
    dap_stream_session_callback_t callback_delete; // Callback при удалении
} dap_stream_session_t;
```

### Типы сессий

```c
typedef enum stream_session_type {
    STREAM_SESSION_TYPE_MEDIA = 0,  // Медиа поток
    STREAM_SESSION_TYPE_VPN,        // VPN соединение
} stream_session_type_t;

typedef enum stream_session_connection_type {
    STEAM_SESSION_HTTP = 0,         // HTTP транспорт
    STREAM_SESSION_UDP,             // UDP транспорт
    STREAM_SESSION_END_TYPE,
} stream_session_connection_type_t;
```

## Keepalive

Поддержание соединения через периодические keepalive сообщения:

- **Интервал:** 3 секунды (`STREAM_KEEPALIVE_TIMEOUT`)
- **Механизм:** Таймер (`dap_timerfd_t`) с UUID для безопасного кросс-поточного доступа
- **Пакеты:** `STREAM_PKT_TYPE_KEEPALIVE` (0x11) → запрос, `STREAM_PKT_TYPE_ALIVE` (0x12) → ответ
- **Защита:** UUID-based таймер предотвращает use-after-free при удалении потока

## Service Packet

Служебный пакет для проверки сессии:

```c
typedef struct dap_stream_srv_pkt {
    uint32_t    session_id;   // ID сессии
    uint8_t     enc_type;     // Тип шифрования
    uint32_t    coockie;      // Cookie (авторизация)
} __attribute__((packed)) dap_stream_srv_pkt_t;
```

## Путь данных (чтение)

```
1. Сокет получает данные → buf_in
2. dap_stream_data_proc_read_ext() ищет sig[8] в потоке
3. При обнаружении полного пакета → s_stream_proc_pkt_in()
4. Тип пакета определяет обработку:
   a. FRAGMENT → реассемблея в buf_fragments
   b. DATA → расшифровка → извлечение dap_stream_ch_pkt_t → dispatch в канал
   c. SERVICE → проверка сессии
5. Пакет передаётся в channel->proc->packet_in_callback()
6. Итерация packet_in_notifiers для нотификаций
```

## Путь данных (запись)

```
1. Вышележащий слой вызывает dap_stream_ch_pkt_write_*()
2. Заголовок канала (dap_stream_ch_pkt_hdr_t) формируется
3. Если данные > MTU → фрагментация
4. Каждый фрагмент/пакет шифруется через dap_stream_pkt_write_unsafe()
5. Выбор пути отправки:
   a. trans->ops->write() (если есть транспорт)
   b. stream->trans->ops->write() (клиентский путь)
   c. Прямая запись в esocket (legacy)
6. Для дейтаграмм: s_stream_send_datagram_unsafe() через dap_io_flow
```

## Ключевые константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `STREAM_KEEPALIVE_TIMEOUT` | 3 сек | Интервал keepalive |
| `STREAM_PKT_SIG_SIZE` | 8 bytes | Размер сигнатуры |
| `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD` | 200 bytes | Overhead шифрования |
| `STREAM_PKT_TYPE_DATA_PACKET` | 0x00 | Пакет данных |
| `STREAM_PKT_TYPE_FRAGMENT_PACKET` | 0x01 | Фрагмент |
| `STREAM_PKT_TYPE_SERVICE_PACKET` | 0xFF | Служебный |
| `STREAM_PKT_TYPE_KEEPALIVE` | 0x11 | Keepalive запрос |
| `STREAM_PKT_TYPE_ALIVE` | 0x12 | Keepalive ответ |

## Связанные документы

- [02 — Каналы](02-channels_ru.md) — мультиплексирование каналов
- [03 — DSHP Handshake](03-handshake_ru.md) — установка соединения
- [04 — Шифрование](04-encryption_ru.md) — криптографическая модель
- [01 — IO Layer](../transport/01-io-layer_ru.md) — нижележащий слой
- Заголовочные файлы: `stream/include/dap_stream.h`, `dap_stream_pkt.h`, `session/include/dap_stream_session.h`
