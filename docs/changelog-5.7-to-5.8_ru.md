# Изменения DAP SDK v5.7 → v5.8

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Охват:** io/, net/, crypto/

## Обзор

Переход с v5.7 на v5.8 — **крупнейшая архитектурная переработка** в истории DAP SDK. Затронуто 675 файлов, 108 071 строка добавлено, 24 767 удалено. Три ключевых направления:

1. **Transport Abstraction Layer** — полное отвязывание стека от HTTP
2. **Lock-free reactor** — замена очереди proc-thread и межконтекстных очередей на lock-free ring buffers
3. **Client FSM rewrite** — разделение монолита на FSM + trans_ctx

## 1. Transport Abstraction Layer (новая подсистема)

### Новая директория: `net/trans/`

Полностью новая абстракция транспортного уровня. DAP Stream больше не знает, какой транспорт используется — он работает с vtable из 17 операций.

**Ключевые файлы (все новые):**

| Файл | Назначение |
|------|-----------|
| `dap_net_trans.h` | Центральная абстракция: `dap_net_trans_type_t` (7 типов), `dap_net_trans_ops` vtable, реестр транспортов |
| `dap_net_trans_ctx.h` | Per-connection контекст: владеет `dap_stream_t`, хранит ключи шифрования |
| `dap_net_trans_server.h` | Унифицированный серверный API с vtable |
| `dap_net_trans_qos.h` | QoS probe/echo протокол для измерения latency |
| `dap_transport_obfuscation.h` | Пакетная обфускация: KDF-SHAKE256 + SALSA2012 |

**Реализации транспортов (все новые):**

| Транспорт | Директория | Строк | Описание |
|-----------|-----------|-------|----------|
| HTTP | `net/trans/http/` | ~2 737 | Обратно-совместимая обёртка над legacy HTTP |
| UDP | `net/trans/udp/` | ~6 081 | Дейтаграммный с Flow Control, маршрутизация по (addr, port) |
| WebSocket | `net/trans/websocket/` | ~3 316 | RFC 6455 для обхода DPI, HTTP upgrade |
| TLS Direct | `net/trans/tls/` | ~1 077 | Прямое TLS с mimicry поддержкой |
| DNS Tunnel | `net/trans/dns/` | ~1 690 | Туннелирование через DNS-запросы (порт 53) |

**Алгоритм обфускации handshake:**
1. Случайный padding до размера в [850, 1350] байт
2. KDF-SHAKE256(seed, packet_size) → 40 байт [nonce(8) + key(32)]
3. Шифрование SALSA2012
4. Результат: blob переменного размера, неотличимый от случайных данных

## 2. Lock-free Reactor

### Замена очередей

Все межконтекстные очереди воркеров заменены с pipe-based на lock-free ring buffers:

| Было (5.7) | Стало (5.8) |
|-----------|------------|
| `dap_events_socket_t*` очереди | `dap_context_queue_t*` (lock-free ring buffer) |
| `DESCRIPTOR_TYPE_QUEUE` | Удалён полностью |
| `epoll_wait(-1)` (блокировка навсегда) | `epoll_wait(10000)` (heartbeat каждые 10 сек) |
| Mutex + condvar в proc thread | Полностью lock-free |

### Новые компоненты IO

| Файл | Строк | Назначение |
|------|-------|-----------|
| `dap_context_queue.c` | 230 | Lock-free ring buffer для межконтекстной коммуникации |
| `dap_io_flow.c` | 1 414 | Универсальный IO Flow API для дейтаграмных протоколов |
| `dap_io_flow_ctrl.c` | 1 103 | Flow Control: sequence numbers, ACK, RTT, retransmission |
| `dap_io_flow_datagram.c` | 499 | Дейтаграммный flow с remote address resolution |
| `dap_io_flow_socket.c` | 976 | Socket-level flow management |
| `dap_thread.c` | 224 | Новая абстракция потоков |
| `dap_thread_pool.c` | 410 | Пул потоков |

### Платформенные расширения IO Flow

| Платформа | Файл | Технология |
|-----------|------|-----------|
| Linux | `dap_io_flow_cbpf.c` | Classic BPF фильтрация |
| Linux | `dap_io_flow_ebpf.c` | eBPF фильтрация и маршрутизация |
| BSD | `dap_io_flow_bsd_lb.c` | Балансировка нагрузки |
| macOS | `dap_io_flow_darwin_gcd.c` | Grand Central Dispatch |
| Windows | `dap_io_flow_win_rio.c` | Registered I/O |

### Datagram Packet Queue

Новый `dap_events_socket_packet_queue_t` — ring buffer для дейтаграмм:
- Начальная ёмкость: 16 пакетов
- Максимум: 4 096 пакетов
- Автоматический рост (удвоение)
- `dap_events_socket_sendto_unsafe()` для UDP/SCTP sendto

## 3. Client FSM Rewrite

### Удалено

| Файл | Строк | Причина |
|------|-------|---------|
| `dap_client_pvt.c` | 1 343 | Монолитный state machine на произвольных потоках |
| `dap_client_pvt.h` | 109 | Приватный заголовок старого дизайна |

### Добавлено

| Файл | Строк | Назначение |
|------|-------|-----------|
| `dap_client_fsm.c` | 1 606 | FSM на выделенном пуле потоков (sticky binding: uuid % pool_size) |
| `dap_client_fsm.h` | 182 | Заголовок FSM |
| `dap_client_trans_ctx.c` | 781 | Минимальный IO контекст (uuid, client pointer, fsm_uuid) |
| `dap_client_trans_ctx.h` | 70 | Заголовок trans_ctx |
| `dap_client_helpers.c` | 221 | Вспомогательные утилиты |

### Архитектурное разделение

```
Было (5.7):                          Стало (5.8):
┌──────────────────────┐      ┌──────────────────────────────┐
│ dap_client_t         │      │ dap_client_t (публичный API) │
│   └─ dap_client_pvt  │      │   └─ dap_client_fsm_t        │
│      (монолит,       │      │      (FSM + крипто,           │
│       любой поток)   │      │       выделенный FSM thread)  │
│                      │      │   └─ dap_net_trans_ctx_t      │
│                      │      │      (ключи, stream ownership)│
│                      │      │   └─ dap_client_trans_ctx_t   │
│                      │      │      (IO identity)            │
└──────────────────────┘      └──────────────────────────────┘
```

**Ключевые улучшения:**
- Тяжёлая криптография на FSM потоке, а не на IO worker
- Transport fallback: `tried_transports[]` отслеживает попытки
- Session resume mode: горячее переподключение без полного handshake
- Legacy enc_init fallback для P2P соединений
- Atomic cross-thread readable stage/status copies

## 4. Stream Protocol Changes

### Модификации `dap_stream_t`

| Поле | Статус | Описание |
|------|--------|----------|
| `trans` | **Добавлено** | Указатель на транспорт (абстракция) |
| `trans_ctx` | **Добавлено** | Back-reference на transport context |
| `flow` | **Добавлено** | Datagram flow для UDP/SCTP |
| `_server_session` | **Добавлено** | Серверная сессия (NULL на клиенте) |
| `client_stream_ref` | **Добавлено** | Защита от dangling pointers |
| `esocket_worker` | **Добавлено** | Воркер esocket'а |
| `conn_http` | **Удалено** | Заменено на `trans` |

### Новые компоненты Stream

| Файл | Строк | Назначение |
|------|-------|-----------|
| `dap_stream_handshake.c` | 1 017 | DSHP v1.0 — бинарный TLV handshake (заменяет HTTP-based) |
| `dap_stream_obfuscation.c` | 631 | Движок обфускации: padding, timing, mimicry, polymorphism |
| `dap_stream_obfuscation_mimicry.c` | 809 | Мимикрия: HTTPS (TLS record), HTTP/2 (binary framing), WebSocket |

### DSHP v1.0 (DAP Stream Handshake Protocol)

Новый бинарный протокол заменяет HTTP-based handshake:

| Параметр | Legacy HTTP | DSHP v1.0 |
|----------|------------|-----------|
| Формат | HTTP POST + JSON | Бинарный TLV |
| Размер | ~100+ bytes | ~64-68 bytes (примерно -34%, оценка) |
| Транспорт | Только HTTP | Любой бинарный |
| Совместимость | Нативно для HTTP | Требует парсер |

**6 типов сообщений:** REQUEST → RESPONSE → SESSION_CREATE → SESSION_CREATE_RESPONSE → STREAM_READY → STREAM_START

## 5. Encryption Server Refactor

| Было (5.7) | Стало (5.8) |
|-----------|------------|
| `dap_enc_http.c` — монолитный HTTP handler | Тонкий HTTP adapter → `dap_enc_server` |
| Логика привязана к HTTP | `dap_enc_server_process_request()` — транспортно-независимый |

## 6. Crypto Layer Changes

### Новые компоненты

| Файл | Назначение |
|------|-----------|
| `dap_enc_kdf.h/.c` | Universal KDF через SHAKE256 с domain separation и ratcheting |
| `crypto/tls/` (5 файлов) | Кроссплатформенный TLS: OpenSSL, Apple Security, SChannel |
| `dap_uuid.h/.c` | UUID утилиты |
| `sha2-256/` | Реализация SHA2-256 |

### Оптимизации производительности

| Операция | Было (5.7) | Стало (5.8) | Ускорение |
|----------|-----------|------------|-----------|
| Создание ключа из raw bytes | ~50 мкс (через Keccak) | ~500 нс (`new_from_raw_bytes`) | **100x** |
| Обновление ключа | Аллокация + копирование | Zero-allocation (`update_from_raw_bytes`) | **~200 нс** |
| Salsa2012 ключ | Случайный nonce | Детерминированный nonce из raw bytes | Deterministic |

### Новые KEM API

```c
dap_enc_kem_result_t* dap_enc_kem_alice_generate_keypair(dap_enc_key_type_t a_kem_type);
dap_enc_kem_result_t* dap_enc_kem_bob_encapsulate(dap_enc_key_type_t a_kem_type,
                                                   const uint8_t *a_alice_pub, size_t a_alice_pub_size);
int                  dap_enc_kem_alice_decapsulate(dap_enc_kem_result_t *a_result,
                                                   const uint8_t *a_bob_pub, size_t a_bob_pub_size);
```

## 7. Сводка архитектурного воздействия

```
v5.7:                                v5.8:
┌────────────────────┐      ┌────────────────────────────┐
│ DAP Stream         │      │ DAP Stream                 │
│ (привязан к HTTP)  │      │ (транспортно-независимый)  │
├────────────────────┤      ├────────────────────────────┤
│ HTTP клиент/сервер │      │ Transport Abstraction      │
│                    │      │ ┌────┐┌────┐┌────┐┌───┐┌──┐│
│                    │      │ │HTTP││UDP ││TLS ││DNS││WS││
├────────────────────┤      ├────────────────────────────┤
│ Reactor (pipe queue)│     │ Reactor (lock-free)        │
│ Mutex + condvar    │      │ Очередь proc-thread lock-free (мьютексы остаются в других путях IO) │
├────────────────────┤      ├────────────────────────────┤
│ Client Pvt (монолит)│     │ Client FSM + TransCtx      │
│ Любой поток        │      │ Выделенный FSM thread      │
└────────────────────┘      └────────────────────────────┘
```

## 8. Ключевые коммиты

| Hash | Сообщение |
|------|----------|
| `55ce6523` | perf: fully lock-free reactor — remove mutex+condvar from proc thread |
| `21b9b3c2` | cleanup: remove dead pipe-based queue code |
| `12c487cd` | perf: remove mutex from flow control hot path |
| `f8238174` | perf: lock-free seq increment in flow control send path |
| `157e61e6` | feat: TLS mimicry transport for DPI resistance |
| `e96b486f` | feat(qos): STAGE_QOS_PROBE FSM branch + transport probe/echo protocol |
| `a4cc5808` | feat: transport fallback in client FSM, TLS auto-registration |
| `ca78707b` | refactor: replace dap_client_esocket with dap_client_trans_ctx |
| `e8bf79c8` | feat(crypto): add IAES2 type with strengthened IV derivation (Примечание: тип IAES2 позже удалён из release-5.8 HEAD) |
| `16c1167b` | fix: event_exit corruption guard + server refcount + HTTP timer fix |
| `51265d84` | fix: thread safety — remove double-free in TLS close, add ASan cmake support |
| `959cf0a4` | fix: EPOLLOUT busy loop, epoll fd mismatch, CLI timeout, worker queue diagnostics |

## Связанные документы

- [Transport Abstraction Layer](transport/02-transport-abstraction_ru.md)
- [Конкретные транспорты](transport/03-transports_ru.md)
- [Обфускация](transport/04-obfuscation_ru.md)
- [Клиентский транспорт](transport/05-client-transport_ru.md)
- [DSHP Handshake](protocol/03-handshake_ru.md)
- [IO Layer](transport/01-io-layer_ru.md)
