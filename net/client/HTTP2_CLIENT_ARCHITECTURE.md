# HTTP2 Client Architecture Design

## Overview
Трехуровневая архитектура HTTP2 клиента с четким разделением ответственности и универсальностью для клиентского/серверного режимов.

## Architecture Layers

### 1. Client Layer (Top - `dap_http2_client`)
**Ответственность:** Публичный API, управление запросами, конфигурация
- Управление жизненным циклом запроса
- Конфигурация (таймауты, SSL, заголовки) 
- Статистика и метрики
- Callbacks для пользователя

### 2. Stream Layer (Middle - `dap_http2_stream`)
**Ответственность:** Обработка протоколов, парсинг данных, буферизация
- HTTP/WebSocket/Binary/SSE протоколы
- Callback-based переключение протоколов
- Channel dispatching (для binary режима)
- Парсинг входящих данных

### 3. Session Layer (Bottom - `dap_http2_session`)
**Ответственность:** Сетевое соединение, сокеты, таймеры
- Управление dap_events_socket_t
- Таймеры подключения/чтения
- SSL/TLS обработка
- Универсальность для клиента/сервера

## Key Design Decisions

### Single Stream Per Session
- Нет множественных параллельных потоков
- Временное разделение (смена callbacks) вместо пространственного
- Упрощает архитектуру и повышает производительность

### Callback-Based Role Definition
- Клиент vs Сервер определяется через callbacks, не структурные поля
- `dap_http2_stream_read_callback_http_client` - парсит HTTP responses
- `dap_http2_stream_read_callback_http_server` - парсит HTTP requests

### Universal Structures
- Все структуры работают и для клиента, и для сервера
- Различия только в конструкторах и callbacks:
  - `dap_http2_session_create()` - клиентская сессия
  - `dap_http2_session_create_from_socket()` - серверная сессия

### Protocol Switching
```c
// HTTP → WebSocket → Binary
stream->read_callback = dap_http2_stream_read_callback_http_client;
// ... HTTP 101 Upgrade ...
stream->read_callback = dap_http2_stream_read_callback_websocket;
// ... protocol negotiation ...
stream->read_callback = dap_http2_stream_read_callback_binary;
```

### Channel Architecture (Binary Mode)
```c
// Двухуровневая система:
// 1. Stream level: read_callback обрабатывает входящие данные
// 2. Channel level: channel_callbacks[id] обрабатывает конкретные каналы

// Для binary протоколов:
decrypt() → parse_packets() → dispatch_to_channel_callbacks[packet->channel_id]
```

## Removed Duplications

### Session Optimizations
- ❌ `remote_addr/port` → используем `es->addr_storage`
- ❌ `*_timeout_ms` → настраиваем таймеры напрямую
- ❌ `ts_last_activity` → используем `es->last_time_active`
- ❌ `is_closing/error` → определяем из `state`

### Stream Optimizations  
- ❌ `created_time/last_activity` → не критично или дублирует session

### Client Optimizations
- ❌ `worker` → используем `session->worker`
- ❌ `bytes_sent/received` → получаем из session статистики
- ❌ `is_async/cancelled` → определяем логически

## Usage Patterns

### Client Mode
```c
dap_http2_client_t *client = dap_http2_client_create(worker);
dap_http2_client_request_t *request = dap_http2_client_request_create();
dap_http2_client_request_set_url(request, "https://example.com");
dap_http2_client_request_async(client, request);
```

### Server Mode (Session/Stream only)
```c
// При accept()
dap_http2_session_t *session = dap_http2_session_create_from_socket(worker, client_socket);
dap_http2_stream_t *stream = dap_http2_stream_create(session, DAP_HTTP2_PROTOCOL_HTTP);
dap_http2_stream_set_http_server_mode(stream);
```

## Performance Characteristics
- **Composition approach:** 1 + N function calls
- **Excellent cache locality:** все данные в соседних структурах
- **Minimal overhead:** callback dispatch через array lookup
- **Single allocation per layer:** нет фрагментации памяти

## Lifecycle Management

### Client Request Lifecycle
```
Client.create() → Session.create() → Stream.create() → 
Request.execute() → Response.receive() → Cleanup
```

### Server Connection Lifecycle  
```
Accept() → Session.create_from_socket() → Stream.create() →
Process_requests() → Send_responses() → Connection.close()
```

### Cancellation Logic
```
Client.cancel() → Session.close() → Socket.close() → 
Session.cleanup() → Stream.cleanup()
```

## Next Steps
1. Реализация базовых конструкторов и деструкторов
2. HTTP парсер для клиентского и серверного режимов
3. Callback система и protocol switching
4. SSL/TLS интеграция
5. Тестирование производительности

## Files Modified
- `net/client/include/dap_http2_client.h` - Client API
- `net/client/include/dap_http2_stream.h` - Stream API  
- `net/client/include/dap_http2_session.h` - Session API
- `net/client/dap_http2_client.c` - Client stubs
- `net/client/dap_http2_stream.c` - Stream stubs
- `net/client/dap_http2_session.c` - Session stubs 