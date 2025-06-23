# Data Flow Analysis - HTTP2 Client/Server

## 📊 CLIENT MODE SCENARIOS

### Scenario 1: Simple HTTP GET Request

**User Code:**
```c
dap_http2_client_get_sync(worker, "https://api.example.com/data", &response, &size, &status)
```

**Data Flow:**
```
User → Client → Session → Stream → HTTP Parser → User Callback
     create    connect   create   set_http_
     client             stream   client_mode()
```

**Detailed Trace:**
1. `dap_http2_client_get_sync()` creates temporary client
2. Client creates session: `dap_http2_session_create(worker)`
3. Client creates stream: `dap_http2_stream_create(session, HTTP)`  
4. Stream sets parser: `stream->read_callback = dap_http2_stream_read_callback_http_client`
5. Session connects: `dap_http2_session_connect(host, port, ssl)`
6. Client sends request through session
7. Response data flows: `Session → Stream → HTTP Parser → Client callback`
8. Client aggregates response and returns to user

**Key Insights:**
- ✅ **Client isolates user from channels** - single response callback
- ✅ **Stream does protocol work** - HTTP parsing happens in stream
- ✅ **Session handles I/O** - socket management

### Scenario 2: WebSocket Connection

**User Code:**
```c
client = dap_http2_client_create(worker);
// ... setup WebSocket upgrade request
dap_http2_client_request_async(client, request);
// Later: protocol switches to WebSocket
```

**Data Flow:**
```
User → Client → Session → Stream → HTTP Parser → WebSocket Parser → Channel Dispatcher
                                 (initial)      (after upgrade)      (for frames)
```

**Protocol Switch:**
1. Initial: `stream->read_callback = dap_http2_stream_read_callback_http_client`
2. After upgrade: `dap_http2_stream_switch_protocol(stream, WEBSOCKET)`
3. New callback: `stream->read_callback = dap_http2_stream_read_callback_websocket`

**Channel Usage:**
```c
// WebSocket callback can dispatch to channels
dap_http2_stream_set_channel_callback(stream, 1, websocket_control_handler, ctx);
dap_http2_stream_set_channel_callback(stream, 2, websocket_data_handler, ctx);
```

**Key Insights:**
- ✅ **Client still unaware of channels** - gets aggregated WebSocket events
- ✅ **Stream handles protocol switching** - callback switching
- ✅ **Channels used internally** - WebSocket frame types dispatch to different handlers

### Scenario 3: Binary Protocol with Multiple Channels

**User Code:**
```c
// SDK developer creates custom protocol
stream = dap_http2_stream_create(session, BINARY);
stream->read_callback = my_custom_binary_parser;

// Custom parser uses channel system
dap_http2_stream_set_channel_callback(stream, 0x01, control_channel_handler, ctx);
dap_http2_stream_set_channel_callback(stream, 0x02, data_channel_handler, ctx);
dap_http2_stream_set_channel_callback(stream, 0x03, auth_channel_handler, ctx);
```

**Data Flow:**
```
Session → Stream → Custom Parser → Channel Dispatcher → Channel Callbacks
              |
              → parse packet header 
              → extract channel_id
              → dap_http2_stream_dispatch_to_channel(stream, channel_id, data, size)
```

**Key Insights:**
- ✅ **SDK developers have full control** - custom parsers can use channel system
- ✅ **Channel dispatching is O(1)** - array lookup with caching
- ✅ **Flexible protocol support** - any binary protocol can be implemented

## 🖥️ SERVER MODE SCENARIOS

### Scenario 1: HTTP Server

**Server Setup:**
```c
// No dap_http2_client structure used in server mode!
session = dap_http2_session_create_from_socket(worker, client_fd);
stream = dap_http2_stream_create(session, HTTP);
dap_http2_stream_set_http_server_mode(stream);

// Set callbacks
session->callbacks.data_received = session_data_handler;
stream->read_callback = dap_http2_stream_read_callback_http_server;
```

**Data Flow:**
```
Client Socket → Session → Stream → HTTP Parser → Request Handler
                     ↓
               session_data_handler calls stream->process_data()
                     ↓
               HTTP parser extracts method, path, headers
                     ↓
               Calls application request handler
```

**Key Insights:**
- ✅ **No client structure** - server uses Session+Stream directly
- ✅ **Same structures different role** - callbacks define behavior
- ✅ **HTTP server parsing** - built-in `dap_http2_stream_read_callback_http_server`

### Scenario 2: WebSocket Server

**Server Setup:**
```c
session = dap_http2_session_create_from_socket(worker, client_fd);
stream = dap_http2_stream_create(session, HTTP);  // Start as HTTP
dap_http2_stream_set_http_server_mode(stream);

// After WebSocket upgrade:
dap_http2_stream_switch_protocol(stream, WEBSOCKET);
```

**Data Flow:**
```
HTTP Upgrade Request → HTTP Parser → WebSocket Upgrade Response
                                         ↓
WebSocket Frames → WebSocket Parser → Channel Dispatcher → Frame Handlers
```

**Key Insights:**
- ✅ **Protocol switching works both ways** - client and server
- ✅ **Channels useful for server** - different WebSocket frame types
- ✅ **Unified architecture** - same code for client/server WebSocket handling

### Scenario 3: Custom Binary Server Protocol

**Server Setup:**
```c
session = dap_http2_session_create_from_socket(worker, client_fd);
stream = dap_http2_stream_create(session, BINARY);
stream->read_callback = my_server_protocol_parser;

// Setup channels for different message types
dap_http2_stream_set_channel_callback(stream, MSG_LOGIN, handle_login, ctx);
dap_http2_stream_set_channel_callback(stream, MSG_DATA, handle_data, ctx);
dap_http2_stream_set_channel_callback(stream, MSG_KEEPALIVE, handle_keepalive, ctx);
```

**Data Flow:**
```
Binary Packets → Custom Parser → Channel Dispatcher → Message Handlers
                      ↓
                Parse message header
                      ↓
                Extract message_type
                      ↓
                dap_http2_stream_dispatch_to_channel(stream, message_type, payload, size)
```

## 🔍 ARCHITECTURAL VALIDATION

### ✅ Structure Completeness Check

**Session Structure:**
- ✅ **Universal for client/server** - role defined by callbacks
- ✅ **Minimal fields** - no redundant data
- ✅ **Single stream management** - `current_stream` pointer

**Stream Structure:**
- ✅ **Protocol agnostic** - callback switching handles different protocols  
- ✅ **Dynamic channels** - efficient memory usage
- ✅ **Caching optimization** - `last_used_channel` for performance
- ✅ **Universal buffer** - single receive buffer for all protocols

**Client Structure:**
- ✅ **High-level only** - no low-level networking details
- ✅ **Minimal fields** - delegates statistics to session
- ✅ **Clean separation** - doesn't know about channels

### ✅ Redundancy Elimination

**Removed Fields:**
- `ts_request_start, ts_first_byte, ts_complete` → Available from session timestamps
- `auto_cleanup` → Unclear purpose, removed
- `request_id` → Not needed for single-stream architecture  
- `path, cookie, user_agent` → Can be part of custom_headers
- `follow_redirects` → Configuration level setting

**Removed Functions:**
- `get_bytes_sent/received()` → Available from session
- `get_duration_ms()` → Calculate from session timestamps

## 🚀 Performance Analysis

### Memory Efficiency
- **Dynamic channels:** Start with 1, expand exactly as needed
- **No compression:** Mark inactive instead of reallocating
- **Single buffer:** Unified receive buffer per stream
- **Caching:** Last used channel cache for locality of reference

### CPU Efficiency  
- **O(1) channel lookup:** Array access by channel_id
- **Callback switching:** Function pointer calls, not conditionals
- **Single parser pass:** Parse once, dispatch to appropriate handler
- **No thread locks:** Single worker per socket architecture

### Network Efficiency
- **Single socket:** One connection handles multiple logical channels
- **Protocol multiplexing:** Different protocols on same connection
- **Efficient callbacks:** Direct function calls, no event queues

## 📋 Implementation Readiness

**Ready to implement:**
1. ✅ **All structures defined** and cleaned of redundancy
2. ✅ **All APIs designed** with proper separation of concerns
3. ✅ **Data flows analyzed** and validated
4. ✅ **Performance optimizations** identified and designed
5. ✅ **Server compatibility** confirmed through analysis

**Next Steps:**
1. Implement stream lifecycle (constructor/destructor)
2. Implement dynamic channel management
3. Implement built-in read callbacks (HTTP, WebSocket, Binary)
4. Implement session-stream integration
5. Implement client convenience functions 