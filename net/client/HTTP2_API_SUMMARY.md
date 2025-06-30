# HTTP2 Client API Summary

## Архитектурные решения ✅

### 1. **Single Stream + Dynamic Channels**
- Один stream на session  
- Динамические каналы через array lookup
- Начальная ёмкость = 1 канал
- Расширение точно под нужное количество каналов

### 2. **Callback-based Architecture**
- Main `read_callback` выполняет роль диспетчера
- Channel callbacks для обработки конкретных каналов
- SDK разработчики могут писать свои парсеры протоколов

### 3. **Universal Structures**
- Одни и те же структуры для client/server
- Роль определяется через callbacks, не структурные поля

### 4. **Client-Channel Isolation**
- Client layer НЕ знает о каналах
- Channels - внутренняя кухня Stream'ов
- Простой API для пользователей, гибкость для SDK разработчиков

### 5. **Lifecycle Dependencies & Ownership**
- **Session → Stream Dependency**: Stream CANNOT exist without Session
- **Worker Ownership**: Worker thread owns all Session/Stream objects  
- **UID-Only External Access**: No direct pointers outside Worker thread
- **Creation Location**: Stream creation ONLY in Session.assigned_to_worker callback

### 6. **Factory Pattern Implementation**
- **Session as Factory**: Session creates Stream objects in worker thread context
- **Thread-Safe Creation**: Factory pattern ensures objects created in correct thread
- **Resource Access**: Factory has access to all necessary session resources
- **Lifecycle Control**: Factory controls complete Stream initialization sequence

### 7. **Composite UID Architecture**
- **64-bit UID**: [8 bits Worker ID][56 bits Stream ID] 
- **Atomic Operations**: Cross-thread access via atomic UID operations
- **UID-Based Routing**: All operations route via UID extraction
- **Consistency Guarantee**: UID valid ⟺ object exists in Worker

## Основные структуры

### Client Structure (FINALIZED with UID)
```c
typedef struct dap_http2_client {
    // UID Management (NEW!)
    _Atomic uint64_t stream_uid;  // Composite UID: worker_id + stream_id
    dap_worker_t *target_worker;  // Pre-selected worker
    
    // Client State  
    dap_http2_client_state_t state;
    dap_http2_client_config_t config;
    dap_http2_client_request_t *current_request;
    
    // Callbacks (including UID assignment)
    dap_http2_client_callbacks_t callbacks;
    void *callbacks_arg;
    
    // NEW: Stream assignment callback
    stream_assigned_cb_t stream_assigned_cb;
    void *callback_context;
} dap_http2_client_t;
```

**Major changes:**
- ✅ **UID-based architecture**: Client stores composite UID instead of direct pointers
- ✅ **Atomic UID field**: Thread-safe cross-thread access
- ✅ **Worker pre-selection**: Known before stream creation
- ✅ **Callback-based assignment**: Consistent lifecycle management
- ❌ **Removed direct pointers**: No session/stream pointers in client

### Request Structure (CLEANED)
```c
typedef struct dap_http2_client_request {
    char *method, *url, *host;
    uint16_t port;
    bool use_ssl;
    
    char *content_type, *custom_headers;
    void *body_data;
    size_t body_size;
} dap_http2_client_request_t;
```

**Removed redundant fields:**
- `path, cookie, user_agent` → Part of custom_headers
- `follow_redirects` → Configuration level setting

### Stream Structure (UNCHANGED)
```c
typedef struct dap_http2_stream {
    // Basic info
    uint32_t stream_id;
    dap_http2_stream_state_t state;
    dap_http2_session_t *session;
    dap_http2_protocol_type_t current_protocol;
    
    // Unified buffer
    uint8_t *receive_buffer;
    size_t receive_buffer_size, receive_buffer_capacity;
    
    // Main callback system
    dap_stream_read_callback_t read_callback;
    void *read_callback_context;
    
    // Dynamic channels
    dap_stream_channel_t *channels;
    size_t channels_capacity, channels_count;
    dap_stream_channel_t *last_used_channel;  // Cache
    
    // Events
    dap_stream_event_callback_t event_callback;
    dap_stream_channel_event_callback_t channel_event_callback;
    
    // HTTP parser state
    dap_http_parser_state_t parser_state;
    size_t content_length, content_received;
    bool is_chunked;
    
    bool is_autonomous;
} dap_http2_stream_t;
```

## Потоки данных

### CLIENT MODE (Updated with UID)
```
User Code → Client (UID) → Worker[Stream → Session] → Channel Callbacks
          create       queue command to worker by UID
```

### SERVER MODE  
```
Socket Accept → Session → Stream → Channel Callbacks
```

**Key Insights:** 
- **Client structure используется ТОЛЬКО в client mode**
- **Client не содержит прямых указателей** - только UID для routing
- **Worker полностью владеет** Stream и Session objects

### ARCHITECTURAL FLOW PATTERN
```
1. Client API Call → UID-based routing to Worker
2. Worker Thread → Direct object access (Session/Stream)
3. Stream Layer → Protocol parsing + Channel dispatching  
4. Channel Layer → User callbacks processing data

ВАЖНО: Stream создается ТОЛЬКО когда Session назначена на Worker!
```

### LIFECYCLE DEPENDENCY CHAIN
```
Client.stream_uid → routing → Worker.lookup_stream(uid) → Stream.session → Session.es
        ↑                                                                         ↓
   atomic access                                                            socket operations
```

## API Highlights

### Channel Management
```c
// Single channel
int dap_http2_stream_set_channel_callback(stream, channel_id, callback, context);

// Multiple channels
int dap_http2_stream_add_channels_array(stream, configs, count);

// Cleanup
int dap_http2_stream_remove_channel_callback(stream, channel_id);
void dap_http2_stream_clear_all_channels(stream);
```

### Protocol Switching
```c
// Built-in modes
void dap_http2_stream_set_http_client_mode(stream);
void dap_http2_stream_set_websocket_mode(stream);
void dap_http2_stream_set_binary_mode(stream);

// Generic switch
int dap_http2_stream_switch_protocol(stream, new_protocol);
```

### Channel Helpers
```c
// For SDK developers
size_t dap_http2_stream_dispatch_to_channel(stream, channel_id, data, size);
bool dap_http2_stream_is_single_stream_mode(stream);
```

## Performance Features

- **O(1) channel lookup** with caching
- **Dynamic memory allocation** - start with 1, expand exactly as needed
- **Single receive buffer** per stream
- **Callback switching** instead of conditionals
- **No thread synchronization** - single worker per socket

## Implementation Status

✅ **Architecture complete** - all structures cleaned and optimized
✅ **API design complete** - comprehensive function coverage  
✅ **Data flows analyzed** - client and server scenarios validated
✅ **Performance optimized** - caching and memory efficiency
✅ **Server compatibility** - universal structures confirmed
✅ **UID management finalized** - composite UID + callback assignment
✅ **Thread safety designed** - atomic cross-thread, simple within worker

**Ready for implementation phase** 🚀

## Key API Functions

### Channel Management
```c
// Individual channel operations
int dap_http2_stream_set_channel_callback(stream, channel_id, callback, context);
int dap_http2_stream_remove_channel_callback(stream, channel_id);

// Bulk operations (preferred)
int dap_http2_stream_add_channels_array(stream, configs, count);
void dap_http2_stream_clear_all_channels(stream);

// Queries
bool dap_http2_stream_has_channels(stream);
size_t dap_http2_stream_get_active_channels_count(stream);
bool dap_http2_stream_is_channel_active(stream, channel_id);
```

### Helper Functions (for SDK developers)
```c
// Main dispatcher helper
size_t dap_http2_stream_dispatch_to_channel(stream, channel_id, data, size);

// Mode checking
bool dap_http2_stream_is_single_stream_mode(stream);
```

### Built-in Protocol Parsers
```c
// Ready-to-use parsers
size_t dap_http2_stream_read_callback_http_client(stream, data, size);
size_t dap_http2_stream_read_callback_http_server(stream, data, size);
size_t dap_http2_stream_read_callback_websocket(stream, data, size);
size_t dap_http2_stream_read_callback_binary(stream, data, size);    // Main channel dispatcher
size_t dap_http2_stream_read_callback_sse(stream, data, size);
```

## Usage Patterns

### 1. Simple HTTP Client (UID-based)
```c
// Client creates with callback-based initialization
dap_http2_client_t *client = dap_http2_client_create();
client->stream_assigned_cb = on_stream_ready;
dap_http2_client_init_stream(client, DAP_HTTP2_PROTOCOL_HTTP);

void on_stream_ready(dap_http2_client_t *client, uint64_t stream_uid) {
    // Stream is ready for HTTP requests
    dap_http2_client_get_async(client, "http://example.com");
}
```

### 2. WebSocket with Logical Channels (UID-based)
```c
dap_http2_client_t *client = dap_http2_client_create();
client->stream_assigned_cb = on_websocket_stream_ready;
dap_http2_client_init_stream(client, DAP_HTTP2_PROTOCOL_HTTP);

void on_websocket_stream_ready(dap_http2_client_t *client, uint64_t stream_uid) {
    // Start WebSocket upgrade process
    dap_http2_client_upgrade_to_websocket(client);
    
    // Add logical channels (sent as commands to worker)
    dap_stream_channel_config_t channels[] = {
        {0, chat_handler, &chat_ctx},
        {1, file_handler, &file_ctx},
        {2, video_handler, &video_ctx}
    };
    dap_http2_client_add_channels_array(client, channels, 3);
}
```

### 3. Custom Binary Protocol
```c
dap_http2_stream_t *stream = dap_http2_stream_create(session, DAP_HTTP2_PROTOCOL_BINARY);

// Set custom parser
dap_http2_stream_set_read_callback(stream, my_custom_protocol_parser, my_ctx);

// Add channels dynamically
dap_http2_stream_set_channel_callback(stream, 0x01, control_handler, NULL);
dap_http2_stream_set_channel_callback(stream, 0x02, data_handler, NULL);
dap_http2_stream_set_channel_callback(stream, 0xFF, heartbeat_handler, NULL);
```

### 4. Custom Parser Example
```c
size_t my_custom_protocol_parser(dap_http2_stream_t *stream, const void *data, size_t size) {
    // Parse custom protocol header
    my_packet_t *packet = parse_my_protocol(data, size);
    
    if (dap_http2_stream_has_channels(stream)) {
        // Multi-channel mode - dispatch by packet type/channel
        return dap_http2_stream_dispatch_to_channel(stream, packet->channel_id, 
                                                   packet->payload, packet->payload_size);
    } else {
        // Single-stream mode - process directly
        return process_packet_directly(stream, packet);
    }
}
```

## Channel Events
```c
void my_channel_event_handler(dap_http2_stream_t *stream, 
                             dap_http2_stream_channel_event_t event,
                             uint8_t channel_id, size_t total_channels) {
    switch (event) {
        case DAP_HTTP2_STREAM_CHANNEL_EVENT_ADDED:
            log_it(L_INFO, "Channel %d added, total: %zu", channel_id, total_channels);
            break;
        case DAP_HTTP2_STREAM_CHANNEL_EVENT_REMOVED:
            log_it(L_INFO, "Channel %d removed, total: %zu", channel_id, total_channels);
            break;
        case DAP_HTTP2_STREAM_CHANNEL_EVENT_CLEARED:
            log_it(L_INFO, "All channels cleared");
            break;
    }
}

dap_http2_stream_set_channel_event_callback(stream, my_channel_event_handler, NULL);
```

## Performance Characteristics

- ✅ **Single stream**: Zero overhead, direct processing
- ✅ **Multi-channel**: O(1) array lookup + cache optimization
- ✅ **Memory**: Exact allocation, no waste
- ✅ **Flexibility**: Custom parsers, any protocol
- ✅ **SDK-friendly**: Clear extension points

## Next Implementation Steps

1. **Stream lifecycle** - create/delete with dynamic channels
2. **Channel management** - add/remove/query operations  
3. **Built-in parsers** - HTTP, WebSocket, Binary protocol
4. **Dispatcher helpers** - channel lookup with caching
5. **Integration** - Session and Client integration
6. **Testing** - Unit tests for all components 

## 📊 Константы и типы

## 🆔 UID Management (NEW!)

### Composite UID Format
```c
// 64-bit UID: [8 bits Worker ID][56 bits Stream ID]
#define INVALID_STREAM_UID     0x0000000000000000
#define MIN_VALID_STREAM_UID   0x0000000000000001
#define MAX_WORKERS            256

// Utility functions
uint8_t extract_worker_id(uint64_t stream_uid);
uint64_t extract_stream_id(uint64_t stream_uid);
```

### Callback-based Assignment
```c
// Stream assignment callback
typedef void (*stream_assigned_cb_t)(dap_http2_client_t *client, uint64_t assigned_uid);

// Client setup with callback
client->stream_assigned_cb = on_stream_assigned;
dap_http2_client_init_stream(client, DAP_HTTP2_PROTOCOL_HTTP);

// Callback implementation
void on_stream_assigned(dap_http2_client_t *client, uint64_t stream_uid) {
    printf("Stream ready with UID: %016lx\n", stream_uid);
    // Client is now ready for operations
    client->state = DAP_HTTP2_CLIENT_STATE_READY;
}
```

### Thread-Safe Operations
```c
// Safe UID access
bool dap_http2_client_has_valid_stream(dap_http2_client_t *client) {
    return atomic_load(&client->stream_uid) != INVALID_STREAM_UID;
}

// Safe command sending
int dap_http2_client_send_data(dap_http2_client_t *client, 
                              const void *data, size_t size) {
    uint64_t uid = atomic_load(&client->stream_uid);
    if (uid == INVALID_STREAM_UID) {
        return -EAGAIN;  // Stream not ready yet
    }
    return send_stream_command(uid, create_send_command(data, size));
}
```

## Performance Characteristics

// ... existing code ... 