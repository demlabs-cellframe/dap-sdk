# Конкретные реализации транспортов DAP SDK

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/trans/`

## Обзор

Уровень транспортной абстракции (TAL) поддерживает пять конкретных реализаций транспортов, каждая из которых реализует `dap_net_trans_ops_t` vtable. Выбор транспорта определяется требованиями к задержке, пропускной способности, устойчивости к DPI и сетевым ограничениям.

```
┌──────────────────────────────────────────────────────────┐
│ L3: DAP Stream Protocol                                  │
├──────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction Layer (dap_net_trans_t)        │
│     ┌──────┐ ┌──────────┐ ┌──────┐ ┌─────┐ ┌─────────┐ │
│     │ TLS  │ │   UDP    │ │ HTTP │ │ DNS │ │   WS    │ │
│     │Mimicr│ │Basic/Rel │ │      │ │Tunne│ │WebSockt │ │
│     └──────┘ └──────────┘ └──────┘ └─────┘ └─────────┘ │
├──────────────────────────────────────────────────────────┤
│ L1: IO слой (dap_events_socket_t, dap_worker_t)         │
└──────────────────────────────────────────────────────────┘
```

Все транспорты регистрируются через `dap_net_trans_register()` и становятся доступными по типу (`dap_net_trans_type_t`) или по имени. Каждый транспорт декларирует свой набор capabilities через битовую маску `DAP_NET_TRANS_CAP_*`.

---

## 1. TLS Mimicry Transport (`net/trans/tls/`)

### Назначение

Транспорт TLS Mimicry -- основной инструмент обхода DPI. Генерирует на проводе реалистичный хэндшейк TLS 1.3, который системы глубокого инспекции пакетов (DPI) идентифицируют как стандартное TLS-соединение. При этом настоящей криптографии TLS не происходит -- шифрование выполняется вышележащим DAP Stream с собственным DSHP-хэндшейком и `dap_enc`.

### Архитектура

```
┌─────────────────────────────────────────────┐
│ DAP Stream (DSHP + dap_enc)                 │
├─────────────────────────────────────────────┤
│ TLS Mimicry Engine  ← фейковый TLS 1.3     │
│   dap_tls_mimicry_t                         │
│   + Fingerprint profiles (dap_tls_fp_*)     │
│   + JA3 calculator (dap_tls_ja3.c)          │
├─────────────────────────────────────────────┤
│ TCP Socket                                  │
└─────────────────────────────────────────────┘
```

### Состояния конечного автомата

```
INIT  →  CLIENT_HELLO_SENT  →  SERVER_HELLO_RCVD  →  ESTABLISHED
 (0)          (1)                    (2)                  (3)
```

Транспорт регистрируется как `DAP_NET_TRANS_TLS_DIRECT` (0x06). Сокет -- TCP (`DAP_NET_TRANS_SOCKET_TCP`).

### Проводной формат хэндшейка

Mimicry-движок (`dap_tls_mimicry.c`) строит три сообщения, полностью повторяя структуру настоящего TLS 1.3:

**Шаг 1: Client -> Server -- ClientHello**

```
TLS Record Header:
  [0x16] [0x03 0x01] [length_be16]
Handshake Header:
  [0x01] [length_24be]
Body:
  legacy_version: 0x0303 (TLS 1.2 на проводе)
  random: 32 байта (случайные)
  session_id: 32 байта
  cipher_suites: TLS_CS_AES_256_GCM_SHA384 (0x1302),
                 TLS_CS_AES_128_GCM_SHA256 (0x1301),
                 TLS_CS_CHACHA20_POLY1305_SHA256 (0x1303)
  compression: null (0x00)
  extensions: 9 штук (см. ниже)
```

Extensions в ClientHello:

| # | Extension | TLS ID | Назначение |
|---|-----------|--------|------------|
| 1 | server_name (SNI) | 0x0000 | Имя хоста -- патчится профилем |
| 2 | supported_groups | 0x000A | X25519, secp256r1, secp384r1 |
| 3 | signature_algorithms | 0x000D | RSA-PSS-SHA256, ECDSA-SHA256, Ed25519 |
| 4 | supported_versions | 0x002B | TLS 1.3 (0x0304) |
| 5 | key_share | 0x0033 | X25519 fake public key (32 байта) |
| 6 | session_ticket | 0x0023 | Пустой (0 байт данных) |
| 7 | encrypt_then_mac | 0x0016 | Пустой |
| 8 | extended_master_secret | 0x0017 | Пустой |
| 9 | psk_key_exchange_modes | 0x002D | psk_dhe_ke (1) |

**Шаг 2: Server -> Client -- ServerHello + CCS + fake EncryptedExtensions**

Сервер генерирует ответ из трех TLS-записей:

```
Запись 1 -- ServerHello:
  [0x16] [0x03 0x03] [length]
  legacy_version: 0x0303
  server_random: 32 байта
  session_id: эхо client session_id
  cipher_suite: TLS_CS_AES_256_GCM_SHA384 (0x1302)
  compression: null
  extensions: supported_versions (0x0304) + key_share (X25519)

Запись 2 -- ChangeCipherSpec:
  [0x14] [0x03 0x03] [0x00 0x01] [0x01]

Запись 3 -- Fake EncryptedExtensions:
  [0x17] [0x03 0x03] [length] [random_bytes]
  Размер: 1500-2500 байт случайных данных
```

**Шаг 3: Client -> Server -- CCS + fake Finished**

```
Запись 1 -- ChangeCipherSpec:
  [0x14] [0x03 0x03] [0x00 0x01] [0x01]

Запись 2 -- Fake Finished:
  [0x17] [0x03 0x03] [length] [random_bytes]
  Размер: 48-64 байта случайных данных
```

После хэндшейка состояние переходит в `ESTABLISHED`, и все данные оборачиваются в TLS Application Data записи.

### Record Layer (режим ESTABLISHED)

После завершения хэндшейка все данные передаются в формате TLS Application Data:

```
[0x17] [0x03 0x03] [length_be16] [payload]
  │        │           │            │
  │        │           │            └── полезная нагрузка (до 16384 байт)
  │        │           └── длина (big-endian 16 bit)
  │        └── версия TLS 1.2 в record layer
  └── content type: Application Data (0x17)
```

Payload разбивается на чанки по `DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD` = 16384 байта. Каждый чанок получает 5-байтный заголовок (`DAP_TLS_MIMICRY_RECORD_HDR_SIZE`). При чтении (`dap_tls_mimicry_unwrap`) не-APPLICATION_DATA записи (например, CCS после хэндшейка) пропускаются автоматически.

### Профили отпечатков браузеров

Fingerprint registry (`dap_tls_fingerprint.c`) загружает 6 профилей при старте (через `__attribute__((constructor))`). Каждый профиль -- это wire-captured шаблон ClientHello с известными смещениями для патча SNI.

| Профиль | Файл | JA3 Hash | Размер шаблона |
|---------|------|----------|----------------|
| chrome_120 | `chrome_120.c` | cd08e31494f9531f560d64c695473da9 | 152 байта |
| firefox_121 | `firefox_121.c` | b32309a26951912be7dba376398abc3b | 150 байт |
| edge_120 | `edge_120.c` | -- | -- |
| safari_17 | `safari_17.c` | -- | -- |
| android_14 | `android_14.c` | -- | -- |
| telegram_android | `telegram_android.c` | e7d705a3286e19ea42f587b344ee6865 | 205 байт |

Патч SNI в шаблоне затрагивает несколько полей одновременно:

1. `sni_hostname_length_offset` -- 2-байтная BE длина имени хоста
2. `sni_hostname_offset` -- сами байты имени хоста
3. `sni_data_length_offset` -- длина данных SNI extension (hostname_length + 5)
4. `extensions_length_offset` -- общая длина всех extensions (увеличивается на длину хоста)

Дополнительно пересчитывается handshake length (первые 4 байта шаблона: type + 3-байтная длина).

### JA3/JA4 Fingerprinting

Модуль `dap_tls_ja3.c` вычисляет JA3-отпечаток по данным ClientHello. Формат JA3 строки:

```
version,ciphers,extensions,curves,points
```

Пример для chrome_120:
```
771,4866-4865-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,
0-23-65281-10-11-35-16-5-13-18-51-45-43-27-17513,29-23-24,0
```

JA3 hash = MD5(ja3_string) в виде 32 hex-символов. Модуль предоставляет три точки входа:

- `dap_tls_ja3_from_tls_record()` -- парсит TLS record с нуля
- `dap_tls_ja3_from_handshake()` -- парсит Handshake сообщение
- `dap_tls_ja3_from_client_hello_body()` -- парсит тело ClientHello

Алгоритм: извлекает legacy_version, cipher_suites, extension types, supported_groups (curves) и ec_point_formats (points), собирает строку и хеширует через MD5.

### Ключевые API

```c
// Создание/удаление mimicry engine
dap_tls_mimicry_t *dap_tls_mimicry_new(bool a_is_server);
void               dap_tls_mimicry_free(dap_tls_mimicry_t *a_m);

// Настройка
void dap_tls_mimicry_set_sni(dap_tls_mimicry_t *a_m, const char *a_hostname);
int  dap_tls_mimicry_set_profile(dap_tls_mimicry_t *a_m,
                                  const dap_tls_fp_profile_t *a_profile);

// Хэндшейк
int dap_tls_mimicry_create_client_hello(...);     // Client: генерация ClientHello
int dap_tls_mimicry_process_client_hello(...);    // Server: обработка -> ServerHello+CCS+fake
int dap_tls_mimicry_process_server_hello(...);    // Client: обработка -> CCS+fake Finished

// Record layer
int dap_tls_mimicry_wrap(...);    // payload -> TLS Application Data records
int dap_tls_mimicry_unwrap(...);  // TLS records -> payload (пропуск не-APP_DATA)
```

### Особенности

- Cipher suite на проводе: `TLS_CS_AES_256_GCM_SHA384` (0x1302) -- соответствует реальным настройкам Chrome/Firefox
- Версия в record layer: TLS 1.0 (0x0301) для ClientHello, TLS 1.2 (0x0303) для остальных записей -- стандартное поведение
- Fake EncryptedExtensions: 1500-2500 байт случайных данных -- имитирует реальный размер EncryptedExtensions в TLS 1.3
- Fake Finished: 48-64 байта -- имитирует реальный Finished message (verify_data = 32 байта + padding)
- Случайные данные генерируются через `randombytes()` (криптографически стойкий ГСЧ)

---

## 2. UDP Transport (`net/trans/udp/`)

### Назначение

UDP транспорт обеспечивает дейтаграммную передачу с низкой задержкой. Предоставляет два режима: Basic (ненадёжный) и Reliable (с ARQ-ретрансмиссией). Тип транспорта: `DAP_NET_TRANS_UDP_BASIC` (0x02) / `DAP_NET_TRANS_UDP_RELIABLE` (0x03). Сокет: UDP (`DAP_NET_TRANS_SOCKET_UDP`).

### Архитектура

```
┌──────────────────────────────────────────────────┐
│ DAP Stream                                       │
├──────────────────────────────────────────────────┤
│ UDP Trans Adapter                                │
│   dap_net_trans_udp_ctx_t (per-stream)           │
│   + Flow Control (dap_io_flow_ctrl_t)            │
│   + Handshake retransmission (dap_timerfd_t)     │
├──────────────────────────────────────────────────┤
│ UDP Server (dap_net_trans_udp_server_t)          │
│   + Cross-worker packet forwarding               │
│   + Per-worker pipe architecture                 │
├──────────────────────────────────────────────────┤
│ UDP Socket (dap_events_socket_t)                 │
└──────────────────────────────────────────────────┘
```

### MTU и фрагментация

Максимальный безопасный размер payload: 1200 байт (`DAP_STREAM_UDP_MAX_PAYLOAD_SIZE`).

Расчёт:
```
Стандартный IPv4 MTU:           1500 байт
- IPv4 header:                   -20
- UDP header:                     -8
- UDP stream internal header:    -50
- Encryption overhead:           -20
- Safety margin:                -200
= ~1200 байт безопасный payload
```

DAP Stream автоматически фрагментирует каналовые пакеты, если `get_max_packet_size()` возвращает 1200.

### Заголовок пакета

Полный заголовок (`dap_stream_trans_udp_full_header_t`) расширяет базовый Flow Control header:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     seq_num (64 bit)                          |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     ack_seq (64 bit)                          |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           timestamp_ms (32)           |   fc_flags  |  type   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     session_id (64 bit)                       |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Весь пакет шифруется целиком. DPI видит только случайные байты переменной длины. Маршрутизация пакетов -- только по (remote_addr, remote_port).

### Типы пакетов

| Тип | Код | Описание |
|-----|-----|----------|
| HANDSHAKE | 0x01 | Обмен ключами (Kyber512, 800 байт) |
| SESSION_CREATE | 0x02 | Создание сессии (зашифровано) |
| DATA | 0x03 | Данные потока (зашифровано) |
| KEEPALIVE | 0x04 | Heartbeat (зашифровано) |
| CLOSE | 0x05 | Закрытие сессии (зашифровано) |

### Серверная архитектура

UDP сервер (`dap_net_trans_udp_server_t`) использует архитектуру sharded listeners с cross-worker packet forwarding:

- Каждый worker получает свой pipe для приёма пакетов от других workers
- `udp_worker_context_t` содержит массив pipe_write_es (по одному на каждый worker)
- Пакеты, пришедшие на "чужой" worker, пересылается через pipe без промежуточных буферов (zero-copy)
- Сессии хранятся в хеш-таблице, маршрутизация по (remote_addr, remote_port)

### Режим Reliable (ARQ)

В режиме `DAP_NET_TRANS_UDP_RELIABLE` подключается модуль Flow Control (`dap_io_flow_ctrl_t`):

- Ретрансмиссия потерянных пакетов
- Упорядоченная доставка через seq_num / ack_seq
- RTT-измерение через timestamp_ms
- Флаги fc_flags: keepalive, retransmit, FIN

### Ключевые API

```c
// Сервер
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name);
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_server,
                                    const char *a_addr, uint16_t a_port);

// Stream регистрация
int dap_net_trans_udp_stream_register(void);

// Конфигурация
dap_stream_trans_udp_config_t dap_stream_trans_udp_config_default();
// defaults: max_packet_size=1400, keepalive_ms=30000, checksum=true, fragmentation=false
```

---

## 3. HTTP Transport (`net/trans/http/`)

### Назначение

HTTP транспорт -- классическая реализация, обеспечивающая обратную совместимость с legacy-клиентами. Тип: `DAP_NET_TRANS_HTTP` (0x01). Сокет: TCP.

Это транспорт по умолчанию (legacy default). Все существующие клиенты DAP изначально использовали HTTP для handshake и streaming.

### Архитектура

```
┌──────────────────────────────────────────────────┐
│ DAP Stream                                       │
├──────────────────────────────────────────────────┤
│ HTTP Trans Adapter                               │
│   dap_stream_trans_http_private_t (per-stream)   │
│   + Protocol translation (HTTP <-> TLV)          │
├──────────────────────────────────────────────────┤
│ HTTP Server (dap_net_trans_http_server_t)        │
│   └── dap_http_server_t                          │
│       └── Registered URL handlers:               │
│           /enc   -> encryption handshake          │
│           /stream -> stream data                  │
│           /stream_ctl -> session control           │
├──────────────────────────────────────────────────┤
│ TCP Socket                                       │
└──────────────────────────────────────────────────┘
```

### Протокол

HTTP транспорт использует классический request-response паттерн:

1. **Encryption handshake**: POST `/enc/gd4y5yh78w42aaagh` с параметрами шифрования в query string
2. **Session create**: зашифрованный запрос к `/stream_ctl` с параметрами каналов
3. **Streaming**: GET `/stream/globaldb?session_id=X` -- long-polling для получения данных

Параметры handshake передаются через HTTP query string:
```
enc_type=2,pkey_exchange_type=5,pkey_exchange_size=1184,block_key_size=32
```

Модуль `dap_stream_trans_http_stream.c` выполняет трансляцию между HTTP query-параметрами и TLV-форматом handshake протокола.

### Серверный модуль

`dap_net_trans_http_server_t` оборачивает `dap_http_server_t` и регистрирует обработчики DAP-протокола:

- `dap_stream_trans_http_add_proc()` -- регистрация stream endpoint
- `dap_stream_trans_http_add_enc_proc()` -- регистрация encryption endpoint
- Множественные address:port пары через `dap_net_trans_http_server_start()`

### Особенности

- Наиболее зрелый и протестированный транспорт
- Полная обратная совместимость с существующими клиентами
- Двунаправленный перевод протоколов: HTTP query <-> TLV handshake
- Поддержка зашифрованных и незашифрованных HTTP-запросов (`dap_net_trans_http_request()` / `dap_net_trans_http_request_enc()`)
- `get_max_packet_size()` возвращает 0 (streaming, без фрагментации)
- Наименьшая устойчивость к DPI -- HTTP трафик легко идентифицируется

---

## 4. DNS Transport (`net/trans/dns/`)

### Назначение

DNS туннельный транспорт передаёт данные через DNS-запросы и ответы. Предназначен для обхода файрворков, пропускающих только DNS-трафик. Тип: `DAP_NET_TRANS_DNS_TUNNEL` (0x07). Сокет: UDP (порт 53).

### Архитектура

```
┌──────────────────────────────────────────────────┐
│ DAP Stream                                       │
├──────────────────────────────────────────────────┤
│ DNS Tunnel Trans Adapter                         │
│   dap_stream_trans_dns_private_t (per-stream)    │
│   + Base32/Base64 encoding                       │
│   + Chunking                                     │
├──────────────────────────────────────────────────┤
│ DNS Server (dap_net_trans_dns_server_t)          │
│   + Per-client session hash table                │
│   + pthread_mutex for cross-worker safety        │
├──────────────────────────────────────────────────┤
│ UDP Socket (port 53)                             │
└──────────────────────────────────────────────────┘
```

### MTU

Максимальный размер полезной нагрузки: **1200 байт** (`get_max_packet_size()`).

Ограничение обусловлено спецификацией DNS и совпадает с консервативным лимитом UDP-транспорта:
- Максимальный размер DNS query (UDP): 512 байт (RFC 1035)
- Максимальный размер DNS TXT записи: 255 байт
- Накладные расходы на encoding (Base32/Base64): ~30-40%
- Заголовки DNS + IP/UDP: ~40-60 байт

Итого безопасная полезная нагрузка на одну DNS-транзакцию: ~1200 байт (тот же консервативный лимит, что и у UDP-транспорта).

### Протокол

Данные кодируются в DNS TXT записи:

```
┌───────────────────────────┐
│ DAP Stream Packet         │
├───────────────────────────┤
│ DNS Tunnel Encoding       │   Base32 или Base64
│   + Optional compression  │
├───────────────────────────┤
│ DNS TXT Record            │   max 255 bytes
├───────────────────────────┤
│ DNS Query/Response        │   UDP port 53
├───────────────────────────┤
│ IP/UDP                    │
└───────────────────────────┘
```

Клиент отправляет DNS query (например, `data.example.com`), сервер отвечает DNS response с TXT записью, содержащей закодированные данные.

### Серверный модуль

DNS сервер (`dap_net_trans_dns_server_t`) маршрутизирует клиентов по IP:port:

- `dns_server_client_session_t` -- per-client сессия в хеш-таблице
- Каждая сессия содержит свой `handshake_key` и `stream`
- Хеш-таблица защищена `pthread_mutex_t` (доступ из разных workers)
- Handshake: обмен ключами через DNS queries (обфусцированный Kyber512)

### Конфигурация

```c
dap_stream_trans_dns_config_t {
    max_record_size:   255,      // max DNS TXT record (RFC 1035)
    max_query_size:    512,      // max DNS query (UDP)
    query_timeout_ms:  5000,     // timeout DNS query
    use_base32:        true,     // Base32 (true) или Base64 (false)
    enable_compression: false,   // сжатие перед encoding
    domain_suffix:     NULL        // суффикс для DNS queries
};
```

### Устойчивость к DPI

DNS транспорт обладает наивысшей устойчивостью к DPI среди всех транспортов:

- DNS-трафик пропускается практически всеми файрворками
- Выглядит как обычный DNS lookup
- Порт 53 rarely блокируется
- Недостаток: очень низкая пропускная способность и высокая задержка

---

## 5. WebSocket Transport (`net/trans/websocket/`)

### Назначение

WebSocket транспорт реализует полнодуплексную связь поверх HTTP через механизм Upgrade (RFC 6455). Тип: `DAP_NET_TRANS_WEBSOCKET` (0x05). Сокет: TCP.

### Архитектура

```
┌──────────────────────────────────────────────────┐
│ DAP Stream                                       │
├──────────────────────────────────────────────────┤
│ WebSocket Trans Adapter                          │
│   dap_net_trans_websocket_private_t (per-stream) │
│   + Frame assembly/disassembly                   │
│   + Ping/Pong heartbeat                          │
│   + Fragment handling                            │
├──────────────────────────────────────────────────┤
│ WebSocket Server (dap_net_trans_websocket_server)│
│   └── HTTP Server (for Upgrade handshake)        │
│       └── Upgrade handler registration           │
├──────────────────────────────────────────────────┤
│ TCP Socket                                       │
└──────────────────────────────────────────────────┘
```

### Протокол

**Фаза 1: HTTP Upgrade**

Клиент отправляет HTTP запрос с заголовками:
```
GET /stream HTTP/1.1
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <base64 random>
Sec-WebSocket-Version: 13
```

Сервер отвечает:
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <SHA1(key + magic)>
```

**Фаза 2: WebSocket Frames**

После upgrade соединение переходит в режим двунаправленных WebSocket frames:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - -+
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
|          Payload Data         ...
+---------------------------------------------------------------+
```

### Типы frames (opcodes)

| Opcode | Тип | Описание |
|--------|-----|----------|
| 0x00 | CONTINUATION | Продолжение фрагментированного сообщения |
| 0x01 | TEXT | Текстовый frame (UTF-8) |
| 0x02 | BINARY | Бинарный frame |
| 0x08 | CLOSE | Закрытие соединения |
| 0x09 | PING | Heartbeat запрос |
| 0x0A | PONG | Heartbeat ответ |

### Конфигурация

```c
dap_net_trans_websocket_config_t {
    max_frame_size:      1048576,   // max размер frame (1 MB)
    ping_interval_ms:    30000,     // интервал ping
    pong_timeout_ms:     10000,     // timeout pong
    enable_compression:  false,     // permessage-deflate
    client_mask_frames:  true,      // маскировка client->server (RFC 6455)
    server_mask_frames:  false,     // маскировка server->client
    subprotocol:         NULL
};
```

### Особенности

- Coexistence с HTTP: функция `dap_net_trans_websocket_try_upgrade()` проверяет HTTP-заголовки и, если запрос содержит WebSocket Upgrade, переключает протокол. Это позволяет HTTP и WebSocket работать на одном порту.
- `get_max_packet_size()` возвращает 0 (streaming, без MTU-ограничений)
- Статистика: frames_sent/received, bytes_sent/received через `dap_net_trans_websocket_get_stats()`
- Закрытие: graceful close через `dap_net_trans_websocket_send_close()` с кодами RFC 6455

---

## Сравнительная таблица транспортов

| Характеристика | TLS Mimicry | UDP Basic | UDP Reliable | HTTP | DNS Tunnel | WebSocket |
|---|---|---|---|---|---|---|
| **Тип** | `TLS_DIRECT` (0x06) | `UDP_BASIC` (0x02) | `UDP_RELIABLE` (0x03) | `HTTP` (0x01) | `DNS_TUNNEL` (0x07) | `WEBSOCKET` (0x05) |
| **Сокет** | TCP | UDP | UDP | TCP | UDP (port 53) | TCP |
| **Задержка** | Низкая | Минимальная | Средняя | Высокая | Очень высокая | Средняя |
| **Пропускная способность** | Высокая | Высокая | Высокая | Средняя | Очень низкая | Средняя |
| **Надёжность** | Гарантированная (TCP) | Нет | Гарантированная (ARQ) | Гарантированная (TCP) | Нет | Гарантированная (TCP) |
| **Упорядоченность** | Да | Нет | Да | Да | Нет | Да |
| **Устойчивость к DPI** | Высокая (TLS fingerprint) | Средняя | Средняя | Низкая | Очень высокая | Средняя |
| **MTU (фрагментация)** | 0 (streaming) | 1200 | 1200 | 0 (streaming) | 1200 | 0 (streaming) |
| **Полнодуплекс** | Да | Да | Да | Нет (request-response) | Да | Да |
| **Сложность реализации** | Высокая | Средняя | Высокая | Низкая | Средняя | Средняя |
| **Capabilities** | RELIABLE, ORDERED, OBFUSCATION, MIMICRY, HIGH_THROUGHPUT, BIDIRECTIONAL | LOW_LATENCY, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL | OBFUSCATION, LOW_LATENCY, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL, MULTIPLEXING |

### Рекомендации по выбору

| Сценарий | Рекомендуемый транспорт |
|----------|------------------------|
| Обход DPI в странах с цензурой | TLS Mimicry + фейковый SNI |
| Низкая задержка, реальный-time | UDP Basic |
| Надёжная доставка, низкая задержка | UDP Reliable |
| Обратная совместимость, legacy клиенты | HTTP |
| Жёсткий файрволл, только DNS | DNS Tunnel |
| Браузерный клиент, NAT traversal | WebSocket |
| Максимальная скрытность | DNS Tunnel (fallback: TLS Mimicry) |

---

## Связанные документы

- [02-transport-abstraction](02-transport-abstraction_ru.md) -- архитектура TAL, vtable, регистрация транспортов
- [04-obfuscation](04-obfuscation_ru.md) -- движок обфускации, padding, mimicry hooks
- [01-io-layer](01-io-layer_ru.md) -- IO слой, event loop, worker threads
