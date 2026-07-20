# Transport Abstraction Layer — Архитектура и интерфейс

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Модуль:** `dap-sdk/net/trans/`

## Обзор

Transport Abstraction Layer (TAL) — второй уровень сетевого стека DAP SDK. Предоставляет единый интерфейс для работы с различными сетевыми транспортами (HTTP, UDP, TLS, DNS, WebSocket), позволяя DAP Stream протоколу быть транспортно-независимым.

**Ключевая идея:** DAP Stream не знает, какой транспорт используется. Он работает с `dap_net_trans_t` через vtable из 17 операций, а конкретная реализация (UDP, TLS mimicry, DNS tunnel) подключается снизу.

## Место в стеке

```
┌──────────────────────────────────────────────────────────┐
│ L3: DAP Stream Protocol                                  │
│     (dap_stream_t, dap_stream_pkt_t, dap_stream_ch_t)   │
├──────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction Layer ← ЭТОТ ДОКУМЕНТ          │
│     (dap_net_trans_t, dap_net_trans_ops_t)               │
│     + Obfuscation Engine Hook                            │
├──────────────────────────────────────────────────────────┤
│ L1: Конкретные транспорты                                │
│     ┌──────┐ ┌─────┐ ┌──────┐ ┌─────┐ ┌───────────┐    │
│     │ HTTP │ │ UDP │ │ TLS  │ │ DNS │ │ WebSocket │    │
│     └──────┘ └─────┘ └──────┘ └─────┘ └───────────┘    │
├──────────────────────────────────────────────────────────┤
│ L0: IO слой (dap_events_socket_t, dap_worker_t)         │
└──────────────────────────────────────────────────────────┘
```

## Регистрация транспортов

Каждый транспорт регистрируется в системе через `dap_net_trans_register()` и становится доступным по типу или имени:

```c
// Регистрация
int dap_net_trans_register(const char *a_name,
                           dap_net_trans_type_t a_type,
                           const dap_net_trans_ops_t *a_ops,
                           dap_net_trans_socket_type_t a_socket_type,
                           void *a_inheritor);

// Поиск
dap_net_trans_t *t = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
dap_net_trans_t *t = dap_net_trans_find_by_name("udp_basic");

// Список всех зарегистрированных
dap_list_t *list = dap_net_trans_list_all(void);
```

## Типы транспортов

```c
typedef enum dap_net_trans_type {
    DAP_NET_TRANS_HTTP           = 0x01,  // HTTP/HTTPS (legacy default)
    DAP_NET_TRANS_UDP_BASIC      = 0x02,  // UDP без гарантий, low latency
    DAP_NET_TRANS_UDP_RELIABLE   = 0x03,  // UDP с ARQ (retransmission)
    DAP_NET_TRANS_UDP_QUIC_LIKE  = 0x04,  // QUIC-inspired multiplexed
    DAP_NET_TRANS_WEBSOCKET      = 0x05,  // WebSocket
    DAP_NET_TRANS_TLS_DIRECT     = 0x06,  // TLS direct connection
    DAP_NET_TRANS_DNS_TUNNEL     = 0x07,  // DNS tunnel
} dap_net_trans_type_t;
```

## Capability Flags

Каждый транспорт декларирует свои возможности через битовую маску:

```c
typedef enum dap_net_trans_cap {
    DAP_NET_TRANS_CAP_RELIABLE        = 0x0001,  // Гарантия доставки
    DAP_NET_TRANS_CAP_ORDERED         = 0x0002,  // Гарантия порядка
    DAP_NET_TRANS_CAP_OBFUSCATION     = 0x0004,  // Поддержка обфускации
    DAP_NET_TRANS_CAP_PADDING         = 0x0008,  // Traffic padding
    DAP_NET_TRANS_CAP_MIMICRY         = 0x0010,  // Мимикрия под легитимный протокол
    DAP_NET_TRANS_CAP_MULTIPLEXING    = 0x0020,  // Мультиплексирование потоков
    DAP_NET_TRANS_CAP_BIDIRECTIONAL   = 0x0040,  // Двунаправленный
    DAP_NET_TRANS_CAP_LOW_LATENCY     = 0x0080,  // Низкая задержка
    DAP_NET_TRANS_CAP_HIGH_THROUGHPUT = 0x0100,  // Высокая пропускная способность
} dap_net_trans_cap_t;
```

Примеры capability sets:

| Транспорт | Capabilities |
|-----------|-------------|
| HTTP | RELIABLE, ORDERED, BIDIRECTIONAL |
| UDP Basic | LOW_LATENCY, BIDIRECTIONAL |
| UDP Reliable | RELIABLE, ORDERED, LOW_LATENCY, BIDIRECTIONAL |
| TLS Direct | RELIABLE, ORDERED, OBFUSCATION, MIMICRY, HIGH_THROUGHPUT, BIDIRECTIONAL |
| DNS Tunnel | OBFUSCATION, LOW_LATENCY, BIDIRECTIONAL |

## Vtable: dap_net_trans_ops_t

Каждый транспорт реализует 17 операций. Большинство операций асинхронные — принимают callback для уведомления о завершении:

```c
// Callback типы для асинхронных операций
typedef void (*dap_net_trans_connect_cb_t)(dap_stream_t *stream, int error);
typedef void (*dap_net_trans_handshake_cb_t)(dap_stream_t *stream,
    const void *response, size_t response_size, int error);
typedef void (*dap_net_trans_session_cb_t)(dap_stream_t *a_stream,
    uint32_t a_session_id, const char *a_response_data,
    size_t a_response_size, int a_error_code);
typedef void (*dap_net_trans_ready_cb_t)(dap_stream_t *stream, int error);

typedef struct dap_net_trans_ops {
    // Жизненный цикл
    int  (*init)(dap_net_trans_t *a_trans, dap_config_t *a_config);
    void (*deinit)(dap_net_trans_t *a_trans);

    // Клиентские операции (асинхронные)
    int  (*connect)(dap_stream_t *a_stream, const char *a_host,
                    uint16_t a_port, dap_net_trans_connect_cb_t a_callback);
    int  (*handshake_init)(dap_stream_t *a_stream,
                           dap_net_handshake_params_t *a_params,
                           dap_net_trans_handshake_cb_t a_callback);
    int  (*session_create)(dap_stream_t *a_stream,
                           dap_net_session_params_t *a_params,
                           dap_net_trans_session_cb_t a_callback);
    int  (*session_start)(dap_stream_t *a_stream, uint32_t a_session_id,
                          dap_net_trans_ready_cb_t a_callback);
    ssize_t (*read)(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
    ssize_t (*write)(dap_stream_t *a_stream, const void *a_data, size_t a_size);
    void (*close)(dap_stream_t *a_stream);

    // Серверные операции
    int  (*listen)(dap_net_trans_t *a_trans, const char *a_addr,
                   uint16_t a_port, dap_server_t *a_server);
    int  (*accept)(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
    int  (*register_server_handlers)(dap_net_trans_t *a_trans, void *a_trans_ctx);
    int  (*handshake_process)(dap_stream_t *a_stream, const void *a_data,
                              size_t a_data_size, void **a_response,
                              size_t *a_response_size);

    // Утилиты
    uint32_t (*get_capabilities)(dap_net_trans_t *a_trans);
    int  (*stage_prepare)(dap_net_trans_t *a_trans,
                          const dap_net_stage_prepare_params_t *a_params,
                          dap_net_stage_prepare_result_t *a_result);
    void *(*get_client_context)(dap_stream_t *a_stream);
    size_t (*get_max_packet_size)(dap_net_trans_t *a_trans);  // MTU
} dap_net_trans_ops_t;
```

### Назначение ключевых операций

| Оperation | Описание |
|-----------|----------|
| `stage_prepare` | Подготовка транспортных ресурсов для клиентского подключения. Возвращает esocket и stream. |
| `handshake_init` | Инициация обмена ключами (клиент). Заменяет legacy HTTP POST `/enc/gd4y5yh78w42aaagh`. |
| `handshake_process` | Обработка handshake на стороне сервера. Принимает данные клиента, возвращает ответ. |
| `session_create` | Создание streaming сессии. Заменяет legacy HTTP к `/stream_ctl`. |
| `session_start` | Запуск streaming с session ID. Заменяет legacy HTTP GET `/stream/globaldb`. |
| `get_max_packet_size` | MTU для фрагментации. UDP=1200, DNS=1200, TCP=0 (без ограничения). |

## Структура транспорта: dap_net_trans_t

```c
typedef struct dap_net_trans {
    dap_net_trans_type_t        type;           // Тип транспорта
    const dap_net_trans_ops_t   *ops;           // Vtable операций
    void                        *_inheritor;    // Данные наследника
    dap_stream_obfuscation_t    *obfuscation;   // Движок обфускации
    uint32_t                    capabilities;   // Битовая маска возможностей
    dap_net_trans_socket_type_t socket_type;    // TCP, UDP или OTHER
    char                        name[64];       // Имя транспорта
    bool                        is_close_session;
    bool                        has_session_control;
    uint16_t                    mtu;            // MTU
    UT_hash_handle              hh;             // Хеш-таблица (key = type)
} dap_net_trans_t;
```

## Per-stream контекст: dap_net_trans_ctx_t

Для каждого активного stream создаётся контекст транспорта, содержащий все необходимое состояние:

```c
typedef struct dap_net_trans_ctx {
    dap_net_trans_t     *trans;              // Общий конфиг транспорта (разделяемый)
    dap_stream_t        *stream;             // Владеет жизненным циклом stream
    dap_http_client_t   *http_client;        // HTTP клиент (NULL для UDP/DNS)
    dap_events_socket_t *esocket;            // Указатель на stream esocket

    // Криптографические ключи
    dap_enc_key_t       *session_key_open;   // Асимметричный обмен (KEM)
    dap_enc_key_t       *session_key;        // Симметричный сессионный ключ
    dap_enc_key_t       *stream_key;         // Ключ шифрования потока
    char                *session_key_id;

    uint32_t            stream_id;           // ID сессии
    uint32_t            uplink_protocol_version;
    uint32_t            remote_protocol_version;
    bool                authorized;
    bool                session_create_sent;     // Защита от повторной отправки session_create

    // Callbacks
    dap_net_trans_handshake_cb_t  handshake_cb;
    dap_net_trans_session_cb_t    session_create_cb;

    char                remote_addr_str[INET6_ADDRSTRLEN];
    uint16_t            remote_port;
    void                *transport_priv;     // Транспорт-специфичные данные
    void                *_inheritor;         // Вышележащий контекст
    struct dap_worker   *esocket_worker;     // Worker, владеющий esocket транспорта
} dap_net_trans_ctx_t;
```

### Архитектура ключей

Три уровня ключей обеспечивают многослойную защиту:

```
session_key_open (асимметричный, KEM)
    ↓ обмен публичными ключами
session_key (симметричный, сессионный)
    ↓ производит
stream_key (шифрование данных потока)
```

## Этапы подключения

Transport Abstraction Layer управляет четырьмя этапами установки соединения:

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ stage_prepare │───→│  handshake   │───→│session_create│───→│stream_ready  │
│              │    │              │    │              │    │              │
│ TCP connect  │    │ Обмен       │    │ Создание     │    │ Начало       │
│ DNS resolve  │    │ ключами     │    │ сессии       │    │ streaming    │
│ TLS mimicry  │    │ (DSHP)      │    │ (encrypted)  │    │              │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

1. **stage_prepare** — подготовка транспортных ресурсов: DNS resolution, TCP connect, TLS mimicry handshake. Возвращает esocket и stream.
2. **handshake** — обмен криптографическими ключами через DSHP (DAP Stream Handshake Protocol). Создаёт session_key.
3. **session_create** — создание streaming сессии поверх зашифрованного канала. Указывает какие каналы открыть.
4. **stream_ready** — подтверждение готовности, начало передачи данных.

## Обфускация

Transport Abstraction Layer поддерживает прозрачную обфускацию через hook:

```c
// Подключение движка обфускации к транспорту
dap_net_trans_attach_obfuscation(trans, obfuscation);

// Прозрачная обфускация при записи
dap_net_trans_write_obfuscated(dap_stream_t *a_stream, const void *a_data, size_t a_size);

// Прозрачная деобфускация при чтении
dap_net_trans_read_deobfuscated(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
```

Обфускация работает на уровне handshake пакетов: padding → KDF-SHAKE256 → SALSA2012 encryption. Подробнее в [04 — Обфускация](04-obfuscation_ru.md).

## QoS (Quality of Service)

TAL предоставляет обёртки для измерения качества транспорта:

```c
int dap_net_trans_probe_latency(dap_net_trans_t *a_trans, const char *a_host,
                                uint16_t a_port, uint32_t a_timeout_ms);    // Задержка (мс)
int dap_net_trans_measure_rtt(dap_net_trans_t *a_trans, const char *a_host, uint16_t a_port,
                              uint32_t a_count, uint32_t a_timeout_ms,
                              uint32_t *a_out_rtt, uint32_t *a_out_ok);    // Round-trip time
int dap_net_trans_measure_throughput(dap_net_trans_t *a_trans, const char *a_host, uint16_t a_port,
                                     uint32_t a_timeout_ms,
                                     float *a_out_down_mbps, float *a_out_up_mbps); // Пропускная способность
```

Эти данные используются для выбора оптимального транспорта и QoS-based routing.

## Связанные документы

- [01 — IO Layer](01-io-layer_ru.md) — нижележащий слой
- [03 — Конкретные транспорты](03-transports_ru.md) — реализации TLS, UDP, HTTP, DNS, WS
- [04 — Обфускация](04-obfuscation_ru.md) — DPI bypass
- [05 — Client Transport](05-client-transport_ru.md) — клиентская сторона
- Заголовочные файлы: `net/trans/include/dap_net_trans.h`, `dap_net_trans_ctx.h`
