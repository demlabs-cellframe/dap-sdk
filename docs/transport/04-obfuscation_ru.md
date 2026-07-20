# Обфускация и обход DPI — Архитектура и реализация

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/trans/`

## Обзор

DAP SDK реализует многоуровневую систему обфускации трафика для противодействия Deep Packet Inspection (DPI) и анализу сетевого трафика. Система работает на трёх уровнях: транспортная обфускация handshake-пакетов, потоковая обфускация данных и TLS-мимикрия.

**Ключевой принцип:** обфускация — это НЕ шифрование. Это средство маскировки структуры пакетов от DPI. Криптографическая защита обеспечивается вышележащими слоями (Kyber → KDF → сессионные ключи).

**Место в стеке:**

```
┌──────────────────────────────────────────────────────────────┐
│ L4: Приложения (VPN, Services)                               │
├──────────────────────────────────────────────────────────────┤
│ L3: DAP Stream + DSHP Handshake (реальное шифрование)        │
├──────────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction (dap_net_trans_t)                  │
│     + Transport Obfuscation Hook                             │
├──────────────────────────────────────────────────────────────┤
│ L1: Конкретные транспорты (TLS mimicry, UDP, HTTP, DNS)      │
├──────────────────────────────────────────────────────────────┤
│ L0: IO слой (dap_events_socket_t, dap_worker_t)              │
└──────────────────────────────────────────────────────────────┘
```

## Модель безопасности

Обфускация в DAP SDK строится на трёх слоях, каждый из которых решает свою задачу:

```
┌─────────────────────────────────────────────────────────┐
│ Слой 3: Обфускация транспорта                           │
│ (anti-DPI: маскировка структуры пакетов)                │
│ → dap_transport_obfuscation.c                           │
│ → SALSA2012 + KDF, эфемерные ключи от размера           │
├─────────────────────────────────────────────────────────┤
│ Слой 2: DSHP Handshake                                  │
│ (обмен ключами: Kyber shared secret → KDF → session key)│
│ → dap_stream_ch_pkt.c                                   │
├─────────────────────────────────────────────────────────┤
│ Слой 1: Stream Encryption                               │
│ (защита данных: шифрование потока сессионным ключом)     │
│ → dap_stream.c, dap_enc                                 │
└─────────────────────────────────────────────────────────┘
```

**Важно:** после деобфускации ключ обфускации уничтожается. Он не участвует в криптографической цепочке.

---

## Transport-Level Obfuscation (dap_transport_obfuscation.c)

### Философия дизайна: «подарочная упаковка»

Модуль `dap_transport_obfuscation.c` реализует обфускацию на уровне handshake-пакетов. Это аналог подарочной упаковки: скрывает коробку, но не содержимое. Настоящая безопасность приходит от Kyber shared secret → KDF → session keys.

- Ключи обфускации эфемерны, производятся от размера пакета
- Не являются частью криптографической цепочки
- После деобфускации ключ отбрасывается

### Константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `DAP_TRANSPORT_OBFUSCATION_MIN_SIZE` | 850 байт | Минимальный размер обфусцированного пакета. Вмещает Kyber512 public key (800 байт) + заголовок + padding |
| `DAP_TRANSPORT_OBFUSCATION_MAX_SIZE` | 1350 байт | Максимальный размер. Под типичным MTU. Диапазон [850, 1350] даёт ~500 байт вариативности для сопротивления DPI |
| `DAP_TRANSPORT_OBFUSCATION_SEED` | `"cellframe-transport-obfuscation-v1"` | Статический сид для KDF. Должен совпадать на клиенте и сервере |
| `SALSA20_NONCE_SIZE` | 8 байт | Размер nonce для SALSA2012 |

### Структура заголовка (обфусцированного пакета)

```c
typedef struct {
    uint16_t cleartext_total_size;  // Общий размер cleartext (для KDF)
    uint16_t handshake_size;        // Размер реальных данных handshake
    // Далее: handshake_data + random padding
} DAP_ALIGN_PACKED obfuscated_header_t;
```

Заголовок сериализуется через `dap_serialize` в little-endian формате. Схема сериализации определена статически:

```c
static dap_serialize_schema_t s_obfuscated_header_schema = {
    .magic = DAP_SERIALIZE_MAGIC_NUMBER,
    .version = 1,
    .name = "obfuscated_header",
    .fields = s_obfuscated_header_fields,
    .field_count = 2,
    .struct_size = sizeof(obfuscated_header_t),
    .validate_func = NULL
};
```

### Алгоритм обфускации (dap_transport_obfuscate_handshake)

```
Входные данные:  handshake_data (например, Kyber512 public key ~800 байт)
Выходные данные: обфусцированный пакет переменного размера

1. Выбор случайного финального размера:
   cleartext_size = random(MIN_SIZE - 8, MAX_SIZE - 8)
   (вычитаем 8 байт — overhead nonce SALSA2012, который будет добавлен при шифровании)

2. Сериализация 4-байтного заголовка:
   [cleartext_total_size (uint16 LE)] [handshake_size (uint16 LE)]

3. Сборка cleartext пакета:
   [header (4 байта)] [handshake_data] [random_padding]
   padding_size = cleartext_size - header_size - handshake_size

4. Производство ключа шифрования:
   dap_enc_kdf_derive(SEED, "obfuscation", packet_size) → 40 байт
   = [nonce (8 байт)] + [key (32 байта)] для SALSA2012

5. Шифрование:
   dap_enc_code(SALSA2012, cleartext) → encrypted_data

6. Выходной размер:
   encrypted_size = cleartext_size + 8 (nonce SALSA2012 добавляется автоматически)
```

**Пример использования:**

```c
uint8_t *l_obf = NULL;
size_t l_obf_size = 0;
int ret = dap_transport_obfuscate_handshake(kyber_key, 800, &l_obf, &l_obf_size);
// l_obf содержит 850-1350 байт зашифрованных данных
send(socket, l_obf, l_obf_size);
DAP_DELETE(l_obf);
```

### Алгоритм деобфускации (dap_transport_deobfuscate_handshake)

```
Входные данные:  обфусцированный пакет
Выходные данные: оригинальные handshake-данные

1. Быстрая проверка размера:
   dap_transport_is_obfuscated_handshake_size(size)
   → проверяет попадание в [MIN_SIZE, MAX_SIZE]

2. Вычисление размера cleartext:
   cleartext_size = encrypted_size - 8 (вычитаем nonce SALSA2012)

3. Производство ключа (тот же KDF):
   dap_enc_kdf_derive(SEED, "obfuscation", cleartext_size) → 40 байт

4. Дешифрование:
   dap_enc_decode(SALSA2012, encrypted_data) → cleartext

5. Парсинг заголовка:
   dap_deserialize_from_buffer_raw() → obfuscated_header_t
   Валидация: cleartext_total_size == фактический размер decrypted

6. Извлечение данных:
   handshake_data = cleartext + header_size
   padding отбрасывается
```

### KDF: производство ключа из размера пакета

Функция `s_get_cipher_key_for_size` реализует ключевую логику:

```c
static dap_enc_key_t* s_get_cipher_key_for_size(size_t a_packet_size, 
                                                  dap_enc_key_t *a_existing_key)
{
    uint8_t l_kdf_key[40];
    uint64_t l_size_counter = (uint64_t)a_packet_size;

    dap_enc_kdf_derive(
        DAP_TRANSPORT_OBFUSCATION_SEED,  // "cellframe-transport-obfuscation-v1"
        strlen(DAP_TRANSPORT_OBFUSCATION_SEED),
        "obfuscation",
        strlen("obfuscation"),
        l_size_counter,
        l_kdf_key,
        sizeof(l_kdf_key)
    );

    // SALSA2012 извлекает nonce из байт [0:7], key из [8:39]
    dap_enc_key_t *l_key = dap_enc_key_new_from_raw_bytes(
        DAP_ENC_KEY_TYPE_SALSA2012, l_kdf_key, sizeof(l_kdf_key));

    // Обнуление KDF буфера (volatile для защиты от оптимизации компилятора)
    volatile void *l_volatile_ptr = l_kdf_key;
    memset((void*)l_volatile_ptr, 0, sizeof(l_kdf_key));

    return l_key;
}
```

Ключ определяется однозначно размером пакета — обе стороны (клиент и сервер) вычисляют один и тот же ключ для одного и того же размера.

### Интеграция с Transport Abstraction Layer

Transport Abstraction Layer подключает обфускацию через hook:

```c
// Подключение движка обфускации к транспорту
dap_net_trans_attach_obfuscation(trans, obfuscation);

// Прозрачная обфускация при записи
dap_net_trans_write_obfuscated(ctx, data, size);

// Прозрачная деобфускация при чтении
dap_net_trans_read_deobfuscated(ctx, buf, buf_size);
```

---

## Stream-Level Obfuscation (dap_stream_obfuscation.c)

Потоковая обфускация работает на уровне данных stream-протокола, поверх транспортного уровня. Предоставляет четыре техники обфускации.

### Техники обфускации

```c
typedef enum dap_stream_obfuscation_type {
    DAP_STREAM_OBFS_NONE        = 0x00,  // Без обфускации
    DAP_STREAM_OBFS_PADDING     = 0x01,  // Добавление случайного padding
    DAP_STREAM_OBFS_MIMICRY     = 0x02,  // Мимикрия протокола
    DAP_STREAM_OBFS_TIMING      = 0x04,  // Обфускация тайминга
    DAP_STREAM_OBFS_POLYMORPHIC = 0x08,  // Полиморфные magic numbers
    DAP_STREAM_OBFS_MIXING      = 0x10,  // Смешивание с фейковым трафиком
    DAP_STREAM_OBFS_ALL         = 0x1F   // Все техники
} dap_stream_obfuscation_type_t;
```

### Уровни обфускации

| Уровень | Техники | Padding | Задержка | Фейковый трафик |
|---------|---------|---------|----------|-----------------|
| `NONE` | — | — | — | — |
| `LOW` | PADDING + TIMING | 8-64 байт, 30% вероятность | 5-20 мс | — |
| `MEDIUM` | PADDING + TIMING + MIXING | 16-256 байт, 70% вероятность | 10-50 мс | 1 КБ/с |
| `HIGH` | PADDING + TIMING + MIXING + MIMICRY | 32-512 байт, 90% вероятность | 20-100 мс | 4 КБ/с |
| `PARANOID` | ALL | 64-1024 байт, 100% | 50-200 мс | 10 КБ/с |

### Padding

Padding добавляет случайные байты к реальным данным. Вероятность и размер настраиваются:

```c
// Конфигурация padding
struct {
    size_t min_padding;           // Минимальный размер (байт)
    size_t max_padding;           // Максимальный размер (байт)
    float padding_probability;    // Вероятность добавления (0.0-1.0)
} padding;
```

При деобфускации последний байт содержит размер padding (1-16), что позволяет восстановить оригинальный размер.

### Генерация фейкового трафика

Фейковый трафик маскирует реальные паттерны общения. Генерируется пакеты случайного размера:

```c
int dap_stream_obfuscation_generate_fake_traffic(
    dap_stream_obfuscation_t *a_obfs,
    void **a_fake_data,    // Выход: случайные данные
    size_t *a_fake_size    // Выход: случайный размер [min_packet_size, max_packet_size]
);
```

Конфигурация mixing:
```c
struct {
    uint32_t artificial_traffic_rate;  // Скорость фейкового трафика (байт/с)
    uint32_t min_packet_size;          // Минимальный размер пакета
    uint32_t max_packet_size;          // Максимальный размер пакета
} mixing;
```

### Timing obfuscation

Случайные задержки между пакетами разрушают паттерны потока:

```c
uint32_t dap_stream_obfuscation_calc_delay(dap_stream_obfuscation_t *a_obfs);
// Возвращает задержку в миллисекундах [min_delay_ms, max_delay_ms]
```

---

## Мимикрия протокола (dap_stream_obfuscation_mimicry.c)

### Общая архитектура

Мимикрия оборачивает данные в формат легитимного протокола. DPI видит «настоящий» HTTPS, HTTP/2 или WebSocket трафик.

```c
typedef enum dap_stream_mimicry_protocol {
    DAP_STREAM_MIMICRY_NONE      = 0,  // Без мимикрии
    DAP_STREAM_MIMICRY_HTTPS     = 1,  // HTTPS (TLS 1.2/1.3)
    DAP_STREAM_MIMICRY_HTTP2     = 2,  // HTTP/2
    DAP_STREAM_MIMICRY_WEBSOCKET = 3,  // WebSocket
    DAP_STREAM_MIMICRY_QUIC      = 4   // QUIC (UDP-based)
} dap_stream_mimicry_protocol_t;
```

### HTTPS мимикрия

Пакеты оборачиваются в TLS Application Data records:

```
[0x17] [0x03 0x03] [length_be16] [payload]
 │        │            │           │
 │        │            │           └── Реальные данные DAP stream
 │        │            └── Длина payload (big-endian)
 │        └── Версия TLS 1.2 на wire
 └── Content Type: Application Data (0x17)
```

Генерация TLS ClientHello/ServerHello для эмуляции handshake:

```c
// Клиент генерирует ClientHello
dap_stream_mimicry_generate_client_hello(mimicry, &client_hello, &hello_size);

// Сервер отвечает ServerHello
dap_stream_mimicry_generate_server_hello(mimicry, client_hello, client_hello_size,
                                          &server_hello, &hello_size);
```

### WebSocket мимикрия

Пакеты оборачиваются в WebSocket frames:

```
[FIN=1, opcode=binary] [MASK + payload_len] [masking_key(4)] [masked_payload]
```

Поддерживается маскировка клиентских фреймов (как в реальном WebSocket).

---

## TLS Mimicry (dap_tls_mimicry.c)

### Назначение

Модуль `dap_tls_mimicry` реализует полноценную TLS 1.3 мимикрию на wire-уровне. DPI видит настоящий TLS handshake и Application Data records. При этом никакой реальной TLS-криптографии не выполняется — DAP stream со своим DSHP handshake и `dap_enc` работает поверх, treating mimicry layer как прозрачный byte pipe.

### Wire layout

```
Client → Server:  TLS Record(Handshake/ClientHello)
Server → Client:  TLS Record(Handshake/ServerHello)
                + TLS Record(ChangeCipherSpec)
                + TLS Record(ApplicationData) [fake EncryptedExtensions, 1500-2500 байт]
Client → Server:  TLS Record(ChangeCipherSpec)
                + TLS Record(ApplicationData) [fake Finished, 48-64 байта]
```

После handshake все данные оборачиваются в Application Data records:
```
[0x17][0x03 0x03][length_be16][payload]
```

### TLS Fingerprint Profiles

Для реалистичности мимикрии поддерживаются профили отпечатков браузеров:

```c
typedef enum dap_stream_browser_type {
    DAP_STREAM_BROWSER_CHROME  = 0,  // Google Chrome
    DAP_STREAM_BROWSER_FIREFOX = 1,  // Mozilla Firefox
    DAP_STREAM_BROWSER_SAFARI  = 2,  // Apple Safari
    DAP_STREAM_BROWSER_EDGE    = 3   // Microsoft Edge
} dap_stream_browser_type_t;
```

Профили определяют точный порядок и набор TLS extensions, cipher suites и другие параметры ClientHello, соответствующие реальным браузерам. При установленном профиле используется template-based генерация через `dap_tls_fp_build_clienthello()`.

### Wire-совместимость

Мимикрия генерирует стандартные TLS 1.3 extensions:

| Extension | ID | Назначение |
|-----------|-----|-----------|
| server_name (SNI) | 0x0000 | Указание имени хоста |
| supported_groups | 0x000A | X25519, secp256r1, secp384r1 |
| signature_algorithms | 0x000D | RSA-PSS, ECDSA, Ed25519 |
| supported_versions | 0x002B | TLS 1.3 (0x0304) |
| key_share | 0x0033 | X25519 (32-byte fake pubkey) |
| session_ticket | 0x0023 | Empty (no ticket) |
| encrypt_then_mac | 0x0016 | Supported |
| extended_master_secret | 0x0017 | Supported |
| psk_key_exchange_modes | 0x002D | psk_dhe_ke |

### Record Layer

Функции wrap/unwrap для работы с TLS record framing:

```c
// Оборачивание payload в TLS Application Data records
int dap_tls_mimicry_wrap(dap_tls_mimicry_t *a_m,
                         const void *a_data, size_t a_size,
                         void **a_out, size_t *a_out_size);

// Извлечение payload из TLS records
int dap_tls_mimicry_unwrap(dap_tls_mimicry_t *a_m,
                           const void *a_data, size_t a_size,
                           void **a_out, size_t *a_out_size,
                           size_t *a_consumed);
```

Максимальный размер payload в одном record: 16384 байт (`DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD`). Большие данные разбиваются на несколько records.

---

## Traffic Padding и Protocol Rotation

### Профили обфускации по регионам

Система поддерживает региональные профили обфускации, адаптированные под уровень DPI в разных странах:

| Профиль | Уровень | Описание |
|---------|---------|----------|
| `DEFAULT` | NONE | Без обфускации (для регионов без DPI) |
| `RU` | MEDIUM | Средняя обфускация (padding + timing + mixing) |
| `CN` / `IR` | HIGH | Сильная обфускация (все техники + мимикрия) |

### Traffic Padding

Историческое название `fake_traffic` переименовано в `traffic_padding`. Параметры:

- **min_padding / max_padding** — диапазон размера padding
- **padding_probability** — вероятность добавления (0.0-1.0)
- При 100% вероятности (PARANOID) каждый пакет получает padding

### Protocol Rotation

Историческое название `protocol_rotation` переименовано в `transport_rotation`. Позволяет динамически переключать транспорт и профиль обфускации в процессе сессии для затруднения анализа.

---

## Полная цепочка обработки пакета

```
Исходящий пакет:
  Application Data
    ↓
  Stream Encryption (session_key)
    ↓
  Stream Obfuscation (padding, timing, mixing)
    ↓
  TLS Mimicry wrap (TLS Application Data record)
    ↓
  Transport Obfuscation (handshake packets only)
    ↓
  Network (выглядит как обычный HTTPS трафик)

Входящий пакет:
  Network
    ↓
  Transport Deobfuscation (handshake packets only)
    ↓
  TLS Mimicry unwrap
    ↓
  Stream Deobfuscation (удаление padding)
    ↓
  Stream Decryption (session_key)
    ↓
  Application Data
```

## Связанные документы

- [02 — Transport Abstraction Layer](02-transport-abstraction_ru.md) — vtable, регистрация транспортов, общий интерфейс
- [03 — Конкретные транспорты](03-transports_ru.md) — реализации TLS, UDP, HTTP, DNS, WS
- [05 — Client Transport](05-client-transport_ru.md) — клиентская сторона подключения
- Заголовочные файлы: `net/trans/include/dap_transport_obfuscation.h`, `net/stream/stream/include/dap_stream_obfuscation.h`, `net/stream/stream/include/dap_stream_obfuscation_mimicry.h`, `net/trans/tls/include/dap_tls_mimicry.h`
