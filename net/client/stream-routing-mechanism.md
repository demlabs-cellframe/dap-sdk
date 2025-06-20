# Механизм выбора стрима и роутинг данных

## 🔍 Проблема роутинга в мультиплексированной сессии

### 📊 **Исходная задача:**
```c
// Одна сессия, множество стримов
Session: api.example.com:443
├── Stream ID=1: HTTP GET /users
├── Stream ID=3: WebSocket /chat  
├── Stream ID=5: SSE /events
└── Stream ID=7: HTTP POST /data

// Вопрос: как определить, какому стриму принадлежат входящие данные?
```

### 🌊 **Поток данных в TCP буфере:**
```c
TCP Socket buf_in:
[HTTP/1.1 200 OK\r\nContent-Length: 123\r\n\r\n{"users":[...]}]  // Stream 1
[data: {"event":"message","data":"hello"}\n\n]                    // Stream 5 (SSE)
[\x81\x05hello]                                                   // Stream 3 (WebSocket)
[HTTP/1.1 201 Created\r\nContent-Length: 45\r\n\r\n{"id":123}]   // Stream 7
```

**Проблема:** Как понять, где заканчивается один стрим и начинается другой?

## 🎯 Стратегии идентификации стримов

### 📡 **1. HTTP/2 подход (идеальный):**
```c
// HTTP/2 Frame Header (9 bytes):
// +-----------------------------------------------+
// |                 Length (24)                   |
// +---------------+---------------+---------------+
// |   Type (8)    |   Flags (8)   |
// +-+-------------+---------------+-------------------------------+
// |R|                 Stream Identifier (31)                     |
// +=+=============================================================+
// |                   Frame Payload (0...)                     ...
// +---------------------------------------------------------------+

typedef struct {
    uint32_t length:24;     // Длина данных
    uint8_t type;           // Тип фрейма (HEADERS, DATA, etc)
    uint8_t flags;          // Флаги
    uint32_t stream_id:31;  // ID стрима (ключевое поле!)
    uint8_t reserved:1;     // Зарезервированный бит
} dap_http2_frame_header_t;

// Роутинг тривиален:
uint32_t stream_id = frame->stream_id;
dap_http2_stream_t *target_stream = session_find_stream(session, stream_id);
```

### 🔄 **2. HTTP/1.1 подход (эмуляция):**
```c
// HTTP/1.1 не имеет Stream ID, поэтому эмулируем:

typedef enum {
    DAP_STREAM_ROUTING_SEQUENTIAL,    // По очереди (HTTP/1.1 style)
    DAP_STREAM_ROUTING_PROTOCOL,      // По типу протокола
    DAP_STREAM_ROUTING_EXPLICIT_ID    // Явный ID (HTTP/2 style)
} dap_stream_routing_mode_t;

// Для HTTP/1.1: sequential routing
// Первый активный HTTP стрим получает данные
```

### 🔌 **3. WebSocket подход:**
```c
// WebSocket Frame Header:
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-------+-+-------------+-------------------------------+
// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
// | |1|2|3|       |K|             |                               |
// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +

// Роутинг по состоянию соединения:
// WebSocket connection = один стрим
// Все WebSocket фреймы идут в этот стрим
```

## 🏗️ Архитектура роутинга в нашей системе

### 📊 **Структура сессии с роутингом:**
```c
typedef struct dap_http2_session {
    // Сетевое соединение
    dap_events_socket_t *es;
    dap_worker_t *worker;
    
    // Управление стримами
    dap_http2_stream_t **streams;        // Массив стримов
    size_t streams_count;
    uint32_t next_stream_id;             // Следующий ID
    
    // РОУТИНГ (только роутинг, без парсинга!)
    dap_stream_routing_mode_t routing_mode;  // Режим роутинга
    dap_http2_stream_t *current_stream;     // Текущий активный стрим (для HTTP/1.1)
    
    // Callback'и
    dap_http2_session_callbacks_t callbacks;
} dap_http2_session_t;

// ПАРСИНГ НАХОДИТСЯ В СТРИМАХ!
typedef struct dap_http2_stream {
    uint32_t stream_id;                  // ID для роутинга
    
    // Парсинг данных (здесь, а не в сессии!)
    uint8_t *receive_buffer;             // Буфер для парсинга
    size_t receive_buffer_size;          // Размер данных в буфере
    size_t receive_buffer_capacity;      // Емкость буфера
    
    // Состояние парсинга
    dap_http_parser_state_t parser_state;
    // ... остальные поля стрима
} dap_http2_stream_t;
```

### 🔄 **Функция роутинга данных:**
```c
/**
 * @brief Обработка входящих данных с роутингом по стримам
 * @param a_session Сессия
 * @param a_data Входящие данные
 * @param a_data_size Размер данных
 */
static void s_session_route_data(dap_http2_session_t *a_session, 
                                const void *a_data, 
                                size_t a_data_size) {
    switch (a_session->routing_mode) {
        case DAP_STREAM_ROUTING_EXPLICIT_ID:
            s_route_by_explicit_id(a_session, a_data, a_data_size);
            break;
            
        case DAP_STREAM_ROUTING_PROTOCOL:
            s_route_by_protocol(a_session, a_data, a_data_size);
            break;
            
        case DAP_STREAM_ROUTING_SEQUENTIAL:
        default:
            s_route_sequential(a_session, a_data, a_data_size);
            break;
    }
}
```

## 🎯 Реализация различных стратегий роутинга

### 🚀 **1. Explicit ID Routing (HTTP/2 style):**
```c
static void s_route_by_explicit_id(dap_http2_session_t *a_session, 
                                   const void *a_data, 
                                   size_t a_data_size) {
    const uint8_t *l_data = (const uint8_t *)a_data;
    
    // СЕССИЯ только извлекает Stream ID, НЕ парсит содержимое!
    if (a_data_size < 9) {
        // Недостаточно данных для HTTP/2 frame header
        // TODO: Буферизация должна быть в стриме, не в сессии!
        log_it(L_WARNING, "Incomplete HTTP/2 frame received");
        return;
    }
    
    // Извлекаем Stream ID из HTTP/2 frame header
    uint32_t l_stream_id = s_extract_http2_stream_id(l_data);
    
    // Находим соответствующий стрим
    dap_http2_stream_t *l_stream = s_session_find_stream(a_session, l_stream_id);
    if (!l_stream) {
        log_it(L_WARNING, "Received data for unknown stream ID %u", l_stream_id);
        return;
    }
    
    // СЕССИЯ только передает данные стриму
    // СТРИМ сам парсит содержимое в своем буфере!
    dap_http2_stream_process_data(l_stream, a_data, a_data_size);
}

static uint32_t s_extract_http2_stream_id(const uint8_t *a_frame_header) {
    // HTTP/2 Stream ID в байтах 5-8 (big-endian, 31 бит)
    uint32_t l_stream_id = 0;
    l_stream_id |= (a_frame_header[5] & 0x7F) << 24;  // Убираем reserved bit
    l_stream_id |= a_frame_header[6] << 16;
    l_stream_id |= a_frame_header[7] << 8;
    l_stream_id |= a_frame_header[8];
    return l_stream_id;
}
```

### 🔄 **2. Protocol-based Routing:**
```c
static void s_route_by_protocol(dap_http2_session_t *a_session, 
                               const void *a_data, 
                               size_t a_data_size) {
    const uint8_t *l_data = (const uint8_t *)a_data;
    
    // Определяем тип протокола по первым байтам
    dap_protocol_type_t l_protocol = s_detect_protocol(l_data, a_data_size);
    
    switch (l_protocol) {
        case DAP_PROTOCOL_HTTP:
            s_route_to_http_stream(a_session, a_data, a_data_size);
            break;
            
        case DAP_PROTOCOL_WEBSOCKET:
            s_route_to_websocket_stream(a_session, a_data, a_data_size);
            break;
            
        case DAP_PROTOCOL_SSE:
            s_route_to_sse_stream(a_session, a_data, a_data_size);
            break;
            
        default:
            log_it(L_WARNING, "Unknown protocol in data stream");
            break;
    }
}

static dap_protocol_type_t s_detect_protocol(const uint8_t *a_data, size_t a_size) {
    if (a_size < 4) return DAP_PROTOCOL_UNKNOWN;
    
    // HTTP response
    if (!dap_strncmp((char*)a_data, "HTTP/", 5)) {
        return DAP_PROTOCOL_HTTP;
    }
    
    // SSE event
    if (!dap_strncmp((char*)a_data, "data:", 5) || 
        !dap_strncmp((char*)a_data, "event:", 6)) {
        return DAP_PROTOCOL_SSE;
    }
    
    // WebSocket frame (первый бит = FIN, биты 1-3 = RSV, биты 4-7 = opcode)
    uint8_t l_first_byte = a_data[0];
    uint8_t l_opcode = l_first_byte & 0x0F;
    if (l_opcode >= 0x0 && l_opcode <= 0xA) {  // Валидные WebSocket opcodes
        return DAP_PROTOCOL_WEBSOCKET;
    }
    
    return DAP_PROTOCOL_UNKNOWN;
}
```

### 📝 **3. Sequential Routing (HTTP/1.1 compatibility):**
```c
static void s_route_sequential(dap_http2_session_t *a_session, 
                              const void *a_data, 
                              size_t a_data_size) {
    // Для HTTP/1.1: данные идут в текущий активный стрим
    dap_http2_stream_t *l_current = a_session->current_stream;
    
    if (!l_current) {
        // Нет активного стрима, ищем первый HTTP стрим в состоянии ожидания ответа
        l_current = s_session_find_waiting_http_stream(a_session);
        if (l_current) {
            a_session->current_stream = l_current;
        }
    }
    
    if (l_current) {
        // Передаем данные текущему стриму
        dap_http2_stream_process_data(l_current, a_data, a_data_size);
        
        // Проверяем, завершился ли стрим
        if (dap_http2_stream_is_complete(l_current)) {
            a_session->current_stream = NULL;  // Освобождаем для следующего
        }
    } else {
        log_it(L_WARNING, "No active stream to route data to");
    }
}
```

## 🔧 Практическая реализация callback роутинга

### 📞 **Callback-based routing:**
```c
typedef struct dap_http2_stream {
    uint32_t stream_id;
    dap_http2_stream_type_t type;
    dap_http2_stream_state_t state;
    
    // CALLBACK'И для обработки данных
    dap_http2_stream_callbacks_t callbacks;
    void *callback_arg;
    
    // Связанный клиент (может быть NULL для автономных стримов)
    dap_http2_client_t *attached_client;
    
    // Парсинг состояние
    dap_http_parser_state_t parser_state;
    
} dap_http2_stream_t;

typedef struct dap_http2_stream_callbacks {
    // Обработка входящих данных
    void (*data_received)(dap_http2_stream_t *stream, const void *data, size_t size);
    
    // HTTP-специфичные callback'и
    void (*headers_parsed)(dap_http2_stream_t *stream, http_status_code_t code);
    void (*body_chunk)(dap_http2_stream_t *stream, const void *data, size_t size);
    void (*request_complete)(dap_http2_stream_t *stream);
    
    // WebSocket-специфичные callback'и  
    void (*websocket_frame)(dap_http2_stream_t *stream, const void *frame, size_t size);
    void (*websocket_message)(dap_http2_stream_t *stream, const void *msg, size_t size);
    
    // SSE-специфичные callback'и
    void (*sse_event)(dap_http2_stream_t *stream, const char *event, const char *data);
    
    // Общие callback'и
    void (*error)(dap_http2_stream_t *stream, int error_code);
    void (*closed)(dap_http2_stream_t *stream);
} dap_http2_stream_callbacks_t;
```

### 🎯 **Роутинг к правильному callback'у:**
```c
void dap_http2_stream_process_data(dap_http2_stream_t *a_stream, 
                                  const void *a_data, 
                                  size_t a_data_size) {
    // Общий callback для всех данных
    if (a_stream->callbacks.data_received) {
        a_stream->callbacks.data_received(a_stream, a_data, a_data_size);
    }
    
    // Специфичная обработка по типу стрима
    switch (a_stream->type) {
        case DAP_HTTP2_STREAM_HTTP:
            s_process_http_data(a_stream, a_data, a_data_size);
            break;
            
        case DAP_HTTP2_STREAM_WEBSOCKET:
            s_process_websocket_data(a_stream, a_data, a_data_size);
            break;
            
        case DAP_HTTP2_STREAM_SSE:
            s_process_sse_data(a_stream, a_data, a_data_size);
            break;
    }
}

static void s_process_http_data(dap_http2_stream_t *a_stream, 
                               const void *a_data, 
                               size_t a_data_size) {
    // Парсим HTTP response
    if (a_stream->parser_state == DAP_HTTP_PARSING_HEADERS) {
        if (s_parse_http_headers(a_stream, a_data, a_data_size)) {
            // Заголовки распарсены
            if (a_stream->callbacks.headers_parsed) {
                http_status_code_t code = s_extract_status_code(a_stream);
                a_stream->callbacks.headers_parsed(a_stream, code);
            }
            a_stream->parser_state = DAP_HTTP_PARSING_BODY;
        }
    }
    
    if (a_stream->parser_state == DAP_HTTP_PARSING_BODY) {
        // Обрабатываем тело ответа
        if (a_stream->callbacks.body_chunk) {
            a_stream->callbacks.body_chunk(a_stream, a_data, a_data_size);
        }
        
        // Проверяем завершение
        if (s_is_http_complete(a_stream)) {
            if (a_stream->callbacks.request_complete) {
                a_stream->callbacks.request_complete(a_stream);
            }
        }
    }
}
```

## 🎪 Практические примеры использования

### 🌐 **Пример 1: Смешанная сессия с роутингом:**
```c
// Создаем сессию с protocol-based routing
dap_http2_session_t *session = dap_http2_session_create("api.example.com", 443, true);
session->routing_mode = DAP_STREAM_ROUTING_PROTOCOL;

// HTTP запрос
dap_http2_stream_t *http_stream = dap_http2_stream_create(session, 1);
http_stream->type = DAP_HTTP2_STREAM_HTTP;
http_stream->callbacks.headers_parsed = http_headers_callback;
http_stream->callbacks.body_chunk = http_body_callback;
http_stream->callbacks.request_complete = http_complete_callback;

// WebSocket соединение
dap_http2_stream_t *ws_stream = dap_http2_stream_create(session, 3);
ws_stream->type = DAP_HTTP2_STREAM_WEBSOCKET;
ws_stream->callbacks.websocket_message = websocket_message_callback;

// SSE поток
dap_http2_stream_t *sse_stream = dap_http2_stream_create(session, 5);
sse_stream->type = DAP_HTTP2_STREAM_SSE;
sse_stream->callbacks.sse_event = sse_event_callback;

// Теперь сессия автоматически роутит данные:
// HTTP responses → http_stream
// WebSocket frames → ws_stream  
// SSE events → sse_stream
```

### 🚀 **Пример 2: HTTP/2 стиль с explicit ID:**
```c
// HTTP/2 сессия с explicit stream ID
dap_http2_session_t *session = dap_http2_session_create("http2.example.com", 443, true);
session->routing_mode = DAP_STREAM_ROUTING_EXPLICIT_ID;

// Множественные HTTP/2 стримы
for (int i = 1; i <= 10; i += 2) {  // Нечетные ID для клиентских стримов
    dap_http2_stream_t *stream = dap_http2_stream_create(session, i);
    stream->callbacks.data_received = http2_data_callback;
    stream->callback_arg = &request_contexts[i];
}

// HTTP/2 фреймы автоматически роутятся по Stream ID
```

## 🏗️ Правильное разделение ответственности

### ❌ **НЕПРАВИЛЬНО: Парсинг в сессии**
```c
// ПЛОХО - сессия не должна парсить данные!
typedef struct dap_http2_session {
    uint8_t *parse_buffer;      // ❌ Буфер парсинга в сессии
    http_parser_t parser;       // ❌ Парсер в сессии
    // ...
};

// ПЛОХО - сессия парсит HTTP заголовки
static void s_session_parse_http(dap_http2_session_t *session, const void *data) {
    // ❌ Сессия не должна знать про HTTP заголовки!
    if (strstr(data, "Content-Length:")) { ... }
}
```

### ✅ **ПРАВИЛЬНО: Парсинг в стримах**
```c
// ХОРОШО - каждый стрим парсит свои данные
typedef struct dap_http2_stream {
    uint32_t stream_id;                  // Только ID для роутинга
    uint8_t *receive_buffer;             // ✅ Буфер парсинга в стриме
    dap_http_parser_state_t parser_state; // ✅ Состояние парсинга в стриме
    // ...
};

// ХОРОШО - сессия только роутит
static void s_session_route_data(dap_http2_session_t *session, const void *data, size_t size) {
    uint32_t stream_id = s_extract_stream_id(data);  // ✅ Только извлекаем ID
    dap_http2_stream_t *stream = s_find_stream(session, stream_id);
    dap_http2_stream_process_data(stream, data, size); // ✅ Стрим сам парсит
}

// ХОРОШО - стрим парсит свои данные
int dap_http2_stream_process_data(dap_http2_stream_t *stream, const void *data, size_t size) {
    // ✅ Стрим добавляет данные в свой буфер
    s_stream_append_to_buffer(stream, data, size);
    
    // ✅ Стрим парсит в зависимости от своего типа
    switch (stream->mode) {
        case DAP_HTTP2_STREAM_MODE_HTTP:
            return s_parse_http_data(stream);      // HTTP парсинг
        case DAP_HTTP2_STREAM_MODE_WEBSOCKET:
            return s_parse_websocket_frame(stream); // WebSocket парсинг
        case DAP_HTTP2_STREAM_MODE_SSE:
            return s_parse_sse_event(stream);      // SSE парсинг
    }
}
```

### 🎯 **Почему это важно:**

1. **Разделение ответственности:**
   - **Сессия:** Только роутинг по Stream ID/протоколу
   - **Стрим:** Парсинг и обработка данных своего протокола

2. **Масштабируемость:**
   - Каждый стрим имеет свой буфер → нет конкуренции за ресурсы
   - Разные стримы могут парсить разные протоколы параллельно

3. **Изоляция ошибок:**
   - Ошибка парсинга в одном стриме не влияет на другие
   - Сессия остается стабильной при проблемах со стримами

4. **Эффективность памяти:**
   - Буферы создаются только для активных стримов
   - Нет глобального буфера сессии, который может расти бесконечно

## ✅ Ответ на исходный вопрос

### ❓ **"Можно ли ориентироваться по ID в заголовке пакета?"**
**✅ ДА!** Это основной механизм:

1. **HTTP/2:** Stream ID в frame header (байты 5-8)
2. **Protocol detection:** По первым байтам данных
3. **Sequential routing:** Для HTTP/1.1 совместимости

### ❓ **"Выбирать нужный callback для обработки?"**
**✅ ИМЕННО ТАК!** Каждый стрим имеет свои callback'и:
- `headers_parsed` для HTTP заголовков
- `websocket_message` для WebSocket сообщений  
- `sse_event` для SSE событий

Роутинг происходит в два этапа:
1. **Session** определяет, какому **Stream** принадлежат данные
2. **Stream** вызывает соответствующий **callback** для обработки

Это обеспечивает четкое разделение ответственности и эффективную обработку мультиплексированных данных. 