# HTTP2 Client Architecture: Universal Stream-Session Design

## Архитектурный обзор

Наша архитектура построена на принципе **универсальных компонентов**, где `Session` и `Stream` работают как для клиентских, так и для серверных сценариев через **union-based роли** и **контекстные колбеки**.

## Трехслойная архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                     CLIENT LAYER                           │
│  ┌─────────────────┐ ┌─────────────────┐ ┌───────────────┐  │
│  │ dap_http2_      │ │ dap_http2_      │ │ dap_http2_    │  │
│  │ client_t        │ │ client_t        │ │ server_t      │  │
│  │ (HTTP Client)   │ │ (WebSocket)     │ │ (HTTP Server) │  │
│  └─────────────────┘ └─────────────────┘ └───────────────┘  │
└─────────────────────────────────────────────────────────────┘
                               │
┌─────────────────────────────────────────────────────────────┐
│                     STREAM LAYER                           │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │              dap_http2_stream_t                         │  │
│  │          (Универсальный стрим)                          │  │
│  │                                                         │  │
│  │  • Единый data_buffer                                  │  │
│  │  • Контекстные колбеки                                 │  │
│  │  • Автономный режим                                    │  │
│  │  • Protocol upgrade support                            │  │
│  └─────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                               │
┌─────────────────────────────────────────────────────────────┐
│                     SESSION LAYER                          │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │              dap_http2_session_t                        │  │
│  │          (Универсальная сессия)                         │  │
│  │                                                         │  │
│  │  • TCP/SSL соединение                                  │  │
│  │  • Множественные стримы                                │  │
│  │  • Клиент/Сервер роли                                  │  │
│  │  • Connection pooling                                  │  │
│  └─────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Универсальная Session структура

```c
typedef struct dap_http2_session {
    // === Универсальные поля ===
    // Connection management
    dap_events_socket_t *es;
    dap_worker_t *worker;
    dap_http2_session_state_t state;
    
    // Network details
    char remote_addr[DAP_HOSTADDR_STRLEN];
    uint16_t remote_port;
    bool is_ssl;
    
    // Universal timers
    dap_timerfd_t *read_timer;
    uint64_t read_timeout_ms;
    
    // Session lifecycle
    time_t ts_created;
    time_t ts_last_activity;
    
    // Multiple streams support
    dap_http2_stream_t *stream;          // Основной стрим (обратная совместимость)
    dap_http2_stream_t **streams;        // Массив всех стримов
    size_t streams_count;                // Количество активных стримов
    size_t streams_capacity;             // Размер массива стримов
    
    // === Ролевые данные (union для экономии памяти) ===
    dap_http2_role_t role;
    union {
        // Клиентские поля (исходящие соединения)
        struct {
            dap_timerfd_t *connect_timer;
            uint64_t connect_timeout_ms;
            time_t ts_connection_attempt;
        } client;
        
        // Серверные поля (входящие соединения)
        struct {
            time_t ts_client_connected;
            char client_user_agent[256];  
            uint16_t client_port;
            size_t max_concurrent_streams;
        } server;
    } role_data;
    
    // Callbacks
    dap_session_callback_new_stream_t new_stream_callback;
    dap_session_callback_error_t error_callback;
    void *callback_arg;
    
} dap_http2_session_t;
```

## Универсальная Stream структура

```c
typedef struct dap_http2_stream {
    // === Универсальные поля ===
    dap_http2_session_t *session;
    
    // Единый буфер данных (заменяет receive_buffer)
    uint8_t *data_buffer;
    size_t data_size;
    size_t data_capacity;
    
    // HTTP парсинг
    dap_http_header_t *headers;
    size_t content_length;
    bool is_chunked;
    
    // Состояние и режим
    dap_http2_stream_state_t state;
    dap_processing_mode_t mode;
    
    // Контекстные колбеки (меняются в зависимости от режима)
    dap_http2_stream_callbacks_t callbacks;
    
    // WebSocket/SSE support
    dap_stream_protocol_t protocol;
    void *protocol_data;
    
    // Buffer management
    size_t max_buffer_size;
    
    // === Ролевые данные ===
    union {
        // Клиентские поля (когда стрим отправляет запросы)
        struct {
            char *method;
            char *uri;
            char *version;
            void *request_body;
            size_t request_body_size;
        } client;
        
        // Серверные поля (когда стрим получает запросы)
        struct {
            http_status_code_t response_code;
            char *response_reason;
            void *response_body;
            size_t response_body_size;
        } server;
    } role_data;
    
} dap_http2_stream_t;
```

## Ролевая дифференциация

### **Session роли**

#### **CLIENT роль**
```c
// Инициализация клиентской сессии
dap_http2_session_t *session = dap_http2_session_create();
session->role = DAP_HTTP2_ROLE_CLIENT;

// Настройка клиентских параметров
session->role_data.client.connect_timeout_ms = 30000;
session->role_data.client.connect_timer = dap_timerfd_create();

// Подключение к серверу
dap_http2_session_connect(session, "example.com", 443, true);
```

#### **SERVER роль**
```c
// Инициализация серверной сессии (при accept)
dap_http2_session_t *session = dap_http2_session_create_from_socket(accepted_socket);
session->role = DAP_HTTP2_ROLE_SERVER;

// Настройка серверных параметров
session->role_data.server.max_concurrent_streams = 100;
session->role_data.server.ts_client_connected = time(NULL);
```

### **Stream контекстные режимы**

#### **Автоматическое определение по данным**
```c
// В колбеке data_received автоматически определяем тип данных
static void s_stream_data_received(dap_http2_stream_t *stream, const void *data, size_t size) {
    const char *str_data = (const char*)data;
    
    // HTTP Response (клиентский режим)
    if (strncmp(str_data, "HTTP/", 5) == 0) {
        dap_http2_stream_set_processing_mode(stream, DAP_PROCESSING_MODE_HTTP_CLIENT);
        s_parse_http_response(stream, data, size);
    }
    
    // HTTP Request (серверный режим)  
    else if (strncmp(str_data, "GET ", 4) == 0 || strncmp(str_data, "POST", 4) == 0) {
        dap_http2_stream_set_processing_mode(stream, DAP_PROCESSING_MODE_HTTP_SERVER);
        s_parse_http_request(stream, data, size);
    }
    
    // WebSocket Frame
    else if (size >= 2 && (str_data[0] & 0x80)) {
        dap_http2_stream_set_processing_mode(stream, DAP_PROCESSING_MODE_WEBSOCKET);
        s_parse_websocket_frame(stream, data, size);
    }
}
```

## Множественные стримы на одной сессии

### **HTTP/2-style мультиплексирование**
```c
// Создание нескольких стримов на одной сессии
dap_http2_session_t *session = dap_http2_session_connect("api.example.com", 443);

// Параллельные запросы
dap_http2_stream_t *stream1 = dap_http2_session_create_stream(session);
dap_http2_stream_send_request(stream1, "GET", "/api/users");

dap_http2_stream_t *stream2 = dap_http2_session_create_stream(session);  
dap_http2_stream_send_request(stream2, "GET", "/api/posts");

dap_http2_stream_t *stream3 = dap_http2_session_create_stream(session);
dap_http2_stream_send_request(stream3, "POST", "/api/comments");

// Все три запроса идут по одному TCP соединению
```

### **Смешанные протоколы**
```c
// HTTP + WebSocket на одной сессии
dap_http2_session_t *session = dap_http2_session_connect("chat.example.com", 443);

// Обычный HTTP API запрос
dap_http2_stream_t *api_stream = dap_http2_session_create_stream(session);
dap_http2_stream_send_request(api_stream, "GET", "/api/user/profile");

// WebSocket для чата (после HTTP Upgrade)
dap_http2_stream_t *ws_stream = dap_http2_session_create_stream(session);
dap_http2_stream_send_upgrade_request(ws_stream, "websocket", "/chat");

// После upgrade WebSocket стрим становится автономным
dap_http2_stream_detach_from_client(ws_stream);
```

## Автономные стримы

### **Отстёгивание от клиента**
```c
// WebSocket стрим после успешного upgrade
if (response_code == 101) { // Switching Protocols
    // Стрим больше не нуждается в клиенте
    dap_http2_stream_detach_from_client(stream);
    
    // Регистрируем в глобальном менеджере
    dap_http2_stream_manager_register(stream);
    
    // Настраиваем WebSocket колбеки
    stream->callbacks.websocket_frame = my_ws_message_handler;
    stream->callbacks.websocket_ping = my_ws_ping_handler;
    
    // Клиент может быть удален или переиспользован
    dap_http2_client_delete(client);
}
```

### **Глобальный менеджер стримов**
```c
// Поиск автономного стрима по ID
dap_http2_stream_t *stream = dap_http2_stream_manager_find_by_id(stream_id);

// Отправка данных в автономный стрим
dap_http2_stream_write(stream, websocket_frame, frame_size);

// Перечисление всех автономных стримов
dap_http2_stream_manager_foreach(my_stream_iterator, user_data);
```

## Интеграция с существующей архитектурой

### **Использование dap_events_socket_t**
```c
// Session привязана к конкретному events socket
typedef struct dap_http2_session {
    dap_events_socket_t *es;        // Привязка к reactor
    dap_worker_t *worker;           // Worker thread
    // ...
} dap_http2_session_t;

// Стрим наследует worker от сессии
dap_http2_stream_t *stream = dap_http2_session_create_stream(session);
assert(stream->session->worker == session->worker);
```

### **Интеграция с dap_context_t**
```c
// Поиск стрима через context
dap_context_t *ctx = dap_worker_get_current()->context;
dap_http2_stream_t *stream = dap_context_find_stream(ctx, stream_uuid);

// Кросс-worker операции
dap_http2_stream_write_mt(target_worker, stream_uuid, data, size);
```

## Преимущества универсального подхода

### **1. Экономия памяти**
- **Union вместо дублирования**: 60% экономии в ролевых полях
- **Единый буфер**: 50% экономии памяти на стрим
- **Общий код**: меньше дублирования функций

### **2. Упрощение архитектуры**
- **Один тип стрима** для всех сценариев
- **Автоматическое определение** роли по данным
- **Переиспользование соединений** между запросами

### **3. Гибкость**
- **Protocol upgrade** без пересоздания стрима
- **Автономные стримы** для long-lived соединений
- **Мультиплексирование** нескольких протоколов

### **4. Производительность**
- **Connection pooling** автоматически
- **Zero-copy** операции где возможно
- **Минимальный overhead** переключения ролей

## Сценарии использования

### **1. HTTP Client**
```c
dap_http2_client_t *client = dap_http2_client_create();
dap_http2_client_request(client, "GET", "https://api.example.com/data", my_response_callback);
```

### **2. HTTP Server**  
```c
dap_http2_server_t *server = dap_http2_server_create();
dap_http2_server_set_request_handler(server, "/api/*", my_api_handler);
dap_http2_server_listen(server, "0.0.0.0", 8080);
```

### **3. WebSocket Client**
```c
dap_http2_client_t *client = dap_http2_client_create();
dap_http2_stream_t *ws = dap_http2_client_websocket_connect(client, "wss://chat.example.com/ws");
dap_http2_stream_set_websocket_handler(ws, my_ws_message_handler);
```

### **4. Proxy/Gateway**
```c
// Получаем запрос от клиента (серверная роль)
dap_http2_stream_t *client_stream = dap_http2_server_accept_stream(server);

// Пересылаем на backend (клиентская роль)  
dap_http2_stream_t *backend_stream = dap_http2_client_create_stream(backend_client);
dap_http2_stream_proxy_data(client_stream, backend_stream);
```

## Заключение

Универсальная архитектура Stream-Session обеспечивает:

- **Единообразие**: один подход для всех сценариев
- **Эффективность**: экономия памяти и переиспользование соединений  
- **Гибкость**: поддержка сложных протоколов и переходов
- **Простоту**: автоматическое определение ролей и режимов
- **Масштабируемость**: поддержка множественных стримов и автономных режимов

Этот подход позволяет создать мощную и гибкую сетевую библиотеку, способную работать в различных сценариях от простых HTTP клиентов до сложных прокси-серверов и WebSocket приложений. 