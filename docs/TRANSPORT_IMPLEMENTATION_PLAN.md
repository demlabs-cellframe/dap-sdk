# План реализации серверов для всех транспортных протоколов

## 📊 Анализ текущего состояния

### ✅ Реализовано (клиент + сервер):
1. **HTTP/HTTPS Transport**
   - Клиент: `dap-sdk/net/stream/stream/dap_stream_transport_http.c`
   - Сервер: `dap-sdk/net/server/http_server/`
   - Статус: ✅ Полностью реализовано и протестировано

2. **UDP Basic Transport**
   - Клиент: `dap-sdk/net/stream/stream/dap_stream_transport_udp.c`
   - Сервер: `dap_stream_add_proc_udp()` в `dap_stream.c`
   - Статус: ✅ Базовая реализация есть

### ⚠️ Частично реализовано (только клиент):
3. **WebSocket Transport**
   - Клиент: `dap-sdk/net/stream/stream/include/dap_stream_transport_websocket.h`
   - Сервер: ❌ Нет
   - Статус: ⚠️ Только заголовок

### ❌ Не реализовано:
4. **UDP Reliable** (`DAP_STREAM_TRANSPORT_UDP_RELIABLE = 0x03`)
   - Клиент: ❌
   - Сервер: ❌
   
5. **UDP QUIC-like** (`DAP_STREAM_TRANSPORT_UDP_QUIC_LIKE = 0x04`)
   - Клиент: ❌
   - Сервер: ❌

6. **TLS Direct** (`DAP_STREAM_TRANSPORT_TLS_DIRECT = 0x06`)
   - Клиент: ❌
   - Сервер: ❌

7. **DNS Tunnel** (`DAP_STREAM_TRANSPORT_DNS_TUNNEL = 0x07`)
   - Клиент: ❌
   - Сервер: ❌

8. **OBFS4** (`DAP_STREAM_TRANSPORT_OBFS4 = 0x08`)
   - Клиент: ❌
   - Сервер: ❌

---

## 🎯 План реализации (поэтапный)

### Phase 1: WebSocket Support (высокий приоритет)
**Цель**: WebSocket широко используется, важен для web-интеграции

#### Задачи:
1. **Завершить клиентскую реализацию**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_websocket.c`
   - Реализовать функции из `.h` файла:
     - `dap_stream_transport_websocket_init()`
     - `dap_stream_transport_websocket_connect()`
     - `dap_stream_transport_websocket_read()`
     - `dap_stream_transport_websocket_write()`
     - `dap_stream_transport_websocket_close()`

2. **Создать серверный модуль**
   - Новый модуль: `dap-sdk/net/server/websocket_server/`
   - Структура:
     ```
     dap-sdk/net/server/websocket_server/
     ├── CMakeLists.txt
     ├── dap_websocket_server.c
     └── include/
         └── dap_websocket_server.h
     ```
   - Функции:
     - `dap_websocket_server_new()`
     - `dap_websocket_server_add_proc()`
     - `dap_websocket_server_delete()`

3. **Интеграция с HTTP сервером**
   - WebSocket начинается с HTTP Upgrade
   - Добавить обработчик Upgrade в `dap_http_server.c`

4. **Тесты**
   - Unit: `dap-sdk/tests/unit/net/websocket/`
   - Integration: `dap-sdk/tests/integration/net/websocket/`

**Оценка**: 3-5 дней

---

### Phase 2: UDP Reliable Transport (средний приоритет)
**Цель**: Надежная доставка поверх UDP для низкой латентности

#### Задачи:
1. **Клиентская реализация**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_udp_reliable.c`
   - ARQ (Automatic Repeat Request) механизм:
     - Нумерация пакетов
     - ACK/NACK система
     - Таймауты и повторная отправка
     - Буфер переупорядочивания

2. **Серверная реализация**
   - Расширить `dap_stream_add_proc_udp()` для режима reliable
   - Добавить флаги в `dap_server_t` для выбора режима UDP

3. **Протокольные расширения**
   - Расширить UDP header из `dap_stream_transport_udp.h`:
     ```c
     typedef struct dap_udp_reliable_header {
         uint32_t seq_num;        // Sequence number
         uint32_t ack_num;        // Acknowledgement
         uint16_t window_size;    // Flow control
         uint16_t flags;          // ACK, SYN, FIN, etc.
         uint32_t timestamp;      // For RTT calculation
     } DAP_ALIGN_PACKED dap_udp_reliable_header_t;
     ```

4. **Тесты**
   - Тесты потери пакетов
   - Тесты переупорядочивания
   - Тесты перегрузки

**Оценка**: 5-7 дней

---

### Phase 3: TLS Direct Transport (средний приоритет)
**Цель**: Прямое TLS соединение без HTTP overhead

#### Задачи:
1. **Клиентская реализация**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_tls.c`
   - Использовать существующий TLS из `dap_enc`:
     - `dap_enc_tls_context_new()`
     - `dap_enc_tls_connect()`

2. **Серверная реализация**
   - Новый модуль: `dap-sdk/net/server/tls_server/`
   - TLS acceptor для входящих соединений
   - Интеграция с `dap_events_socket_t`

3. **Конфигурация**
   - Поддержка TLS 1.3
   - Настройка cipher suites
   - Поддержка mutual TLS (mTLS)

4. **Тесты**
   - Тесты handshake
   - Тесты сертификатов
   - Тесты mTLS

**Оценка**: 4-6 дней

---

### Phase 4: UDP QUIC-like Transport (низкий приоритет)
**Цель**: Multiplexed UDP с минимальной латентностью

#### Задачи:
1. **Клиентская реализация**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_udp_quic.c`
   - Упрощенный QUIC:
     - Stream multiplexing
     - 0-RTT connection
     - Connection migration

2. **Серверная реализация**
   - Расширить UDP сервер для QUIC-like режима
   - Connection ID management
   - Stream state machines

3. **Протокол**
   - Определить packet format
   - Stream frame format
   - Connection setup

**Оценка**: 7-10 дней (сложно)

---

### Phase 5: DNS Tunnel Transport (низкий приоритет)
**Цель**: Туннелирование через DNS для обхода файрволлов

#### Задачи:
1. **Клиентская реализация**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_dns.c`
   - DNS query encoding
   - Response parsing
   - Chunking для больших данных

2. **Серверная реализация**
   - Новый модуль: `dap-sdk/net/server/dns_tunnel_server/`
   - Интеграция с существующим `dap-sdk/net/server/dns_server/`
   - DNS authoritative server для туннелирования

3. **Протокол**
   - TXT record encoding
   - Base32/Base64 encoding
   - Compression

**Оценка**: 5-8 дней

---

### Phase 6: OBFS4 Transport (низкий приоритет)
**Цель**: Obfuscation для DPI bypass (как Tor)

#### Задачи:
1. **Интеграция obfs4**
   - Либо: Интегрировать obfs4proxy
   - Либо: Реализовать собственную версию

2. **Клиентская реализация**
   - Файл: `dap-sdk/net/stream/stream/dap_stream_transport_obfs4.c`
   - Handshake obfuscation
   - Traffic padding
   - Protocol mimicry

3. **Серверная реализация**
   - Новый модуль: `dap-sdk/net/server/obfs4_server/`
   - De-obfuscation layer
   - Connection fingerprinting resistance

4. **Тесты DPI**
   - Тесты против известных DPI систем
   - Traffic analysis resistance

**Оценка**: 10-14 дней (очень сложно)

---

## 📝 Текущий тест transport_integration.c

### Проблемы:
1. ❌ Пытается использовать несуществующие API (`dap_http_server_new` с неверными параметрами)
2. ❌ Слишком сложен для текущего состояния (нет серверов для большинства транспортов)
3. ❌ Не может протестировать то, что не реализовано

### Решение:
**Упростить тест, но НЕ упрощать логику - делать то что реально тестируется:**

1. **test_transport_integration.c должен тестировать:**
   - ✅ `dap_client_set_transport_type()` / `dap_client_get_transport_type()` (API)
   - ✅ Регистрацию транспортов в системе
   - ✅ Enumerate транспортов
   - ⚠️ Подключение к серверам **только для реализованных транспортов (HTTP, UDP)**

2. **Для нереализованных транспортов:**
   - Тестировать только API (set/get)
   - Проверять регистрацию
   - **НЕ** пытаться подключиться к несуществующим серверам

3. **Создать отдельные интеграционные тесты для каждого транспорта:**
   - `tests/integration/net/transport/test_http_transport.c` (уже есть в `tests/integration/net/http/`)
   - `tests/integration/net/transport/test_udp_transport.c` (создать)
   - `tests/integration/net/transport/test_websocket_transport.c` (создать после Phase 1)
   - и т.д. по мере реализации

---

## 🎬 Действия СЕЙЧАС

### Что сделать прямо сейчас:

1. **Обновить `test_transport_integration.c`** ✅ (уже сделано - упрощен до API тестов)

2. **Собрать и протестировать**
   ```bash
   cd dap-sdk/build
   make test_transport_integration -j$(nproc)
   ./tests/integration/net/transport/test_transport_integration
   ```

3. **Создать issue/задачу для каждой фазы**:
   - Issue #1: WebSocket Support Implementation
   - Issue #2: UDP Reliable Transport
   - Issue #3: TLS Direct Transport
   - Issue #4: UDP QUIC-like Transport
   - Issue #5: DNS Tunnel Transport
   - Issue #6: OBFS4 Transport

4. **Документировать план**:
   - Добавить в `dap-sdk/docs/TRANSPORT_ROADMAP.md`

---

## ⏱️ Общая оценка времени

| Phase | Transport | Приоритет | Оценка (дни) | Статус |
|-------|-----------|-----------|--------------|--------|
| 0 | HTTP/HTTPS | Высокий | - | ✅ Готово |
| 0 | UDP Basic | Высокий | - | ✅ Готово |
| 1 | WebSocket | Высокий | 3-5 | ⏳ Заголовок готов |
| 2 | UDP Reliable | Средний | 5-7 | ❌ Не начато |
| 3 | TLS Direct | Средний | 4-6 | ❌ Не начато |
| 4 | UDP QUIC-like | Низкий | 7-10 | ❌ Не начато |
| 5 | DNS Tunnel | Низкий | 5-8 | ❌ Не начато |
| 6 | OBFS4 | Низкий | 10-14 | ❌ Не начато |

**Итого**: 34-50 рабочих дней для полной реализации всех транспортов

---

## 🚀 Рекомендация

**Начать с Phase 1 (WebSocket)** после того как текущий тест заработает, так как:
1. WebSocket важен для web-приложений
2. Относительно простая реализация (Upgrade от HTTP)
3. Широко используется в индустрии
4. Хорошая основа для понимания других транспортов

**НЕ начинать сразу все фазы** - делать последовательно с полными тестами для каждого транспорта.

