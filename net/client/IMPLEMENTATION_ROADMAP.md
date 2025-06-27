# HTTP2 CLIENT IMPLEMENTATION ROADMAP

## ✅ **COMPLETED: Phase 1 - Client Layer Foundation**

### What's Done:
- ✅ HTTP Client API structures (`dap_http2_client_t`, `dap_http2_client_request_t`)
- ✅ Client lifecycle functions (`create`, `delete`, `config`)
- ✅ Request management (`create`, `set_url`, `set_method`, `set_headers`, `set_body`)
- ✅ URL parsing logic (extracts host, port, path, SSL flag)
- ✅ State management with atomic operations
- ✅ Proper memory management and cleanup

### Architecture Compliance:
- ✅ **NO** custom `create_and_connect` functions
- ✅ **NO** duplication of Session API
- ✅ **PREPARED** to use existing Session/Stream API
- ✅ **CORRECT** separation of responsibilities

---

## 🎯 **NEXT: Phase 2 - Stream HTTP Mode (HTTP Protocol Handler)**

### Goal: 
Create HTTP-specific Stream read callback that handles HTTP protocol parsing and formatting.

### Tasks:

#### 2.1. HTTP Stream Context Structure
```c
typedef struct dap_http_stream_context {
    // HTTP Request Info (from Client Layer)
    char *method;                    
    char *path;                      
    char *headers;                   
    void *body_data;                 
    size_t body_size;                
    
    // HTTP Response Parsing State
    int status_code;                 
    struct dap_http_header *response_headers;
    size_t content_length;
    bool is_chunked;
    
    // Streaming Decision Logic (from old module)
    bool streaming_enabled;          
    size_t streaming_threshold;      
    
    // Chunked Processing (from old module)
    bool reading_chunk_size;
    size_t current_chunk_size;
    size_t current_chunk_read;
    
    // Client Callbacks
    dap_http2_client_response_cb_t response_callback;
    dap_http2_client_progress_cb_t progress_callback;
    dap_http2_client_error_cb_t error_callback;
    void *callback_arg;
} dap_http_stream_context_t;
```

#### 2.2. HTTP Stream Constants
```c
// HTTP-specific Stream states
#define HTTP_STREAM_STATE_IDLE           0
#define HTTP_STREAM_STATE_REQUEST_SENT   1
#define HTTP_STREAM_STATE_HEADERS        2
#define HTTP_STREAM_STATE_BODY           3
#define HTTP_STREAM_STATE_COMPLETE       4
#define HTTP_STREAM_STATE_ERROR          5
```

#### 2.3. HTTP Stream Functions
- `dap_http2_stream_set_http_client_mode()` - Configure stream for HTTP client
- `dap_http2_stream_send_http_request()` - Format and send HTTP request
- `dap_http2_stream_read_callback_http_client()` - Main HTTP protocol handler

#### 2.4. HTTP Request Formatting Logic
Adapt from old module:
- HTTP method + path + version line
- Host header generation
- Content-Length for POST
- Custom headers integration
- Body attachment for POST/PUT

#### 2.5. HTTP Response Parsing Logic  
Adapt from old module:
- Status line parsing
- Headers parsing
- Content-Length extraction
- Transfer-Encoding: chunked detection
- Streaming mode decision

#### 2.6. Streaming Logic Integration
Adapt from old module:
- MIME type analysis for streaming decision
- Size threshold checking
- Zero-copy data forwarding
- Progress callbacks

#### 2.7. Chunked Transfer Encoding
Adapt from old module:
- Chunk size parsing
- Chunk data processing
- Error recovery logic
- Completion detection

---

## 🎯 **Phase 3 - Request Execution Integration**

### Goal:
Connect Client Layer with Stream Layer using existing Session API.

### Tasks:

#### 3.1. Request Execution Functions
- `dap_http2_client_request_sync()` - Synchronous request execution
- `dap_http2_client_request_async()` - Asynchronous request execution

#### 3.2. Integration Flow Implementation
```c
// Правильный flow с использованием СУЩЕСТВУЮЩИХ API
int dap_http2_client_request_sync(client, request, response_data, response_size, status_code) {
    // 1. Parse URL components (already done in request)
    
    // 2. Create Session через СУЩЕСТВУЮЩИЙ API
    session = dap_http2_session_create(worker, client->config.connect_timeout_ms);
    
    // 3. Connect через СУЩЕСТВУЮЩИЙ API  
    dap_http2_session_connect(session, request->host, request->port, request->use_ssl);
    
    // 4. Create Stream через СУЩЕСТВУЮЩИЙ API
    stream = dap_http2_session_create_stream(session);
    
    // 5. Configure Stream for HTTP
    dap_http2_stream_set_http_client_mode(stream);
    
    // 6. Send request through Stream
    dap_http2_stream_send_http_request(stream, request);
    
    // 7. Wait for response via callbacks
}
```

#### 3.3. Callback Integration
- Client callbacks → Stream context callbacks
- State synchronization between layers
- Error propagation from Stream to Client
- Progress reporting integration

#### 3.4. UID Management
- Assign Stream UID to Client when session created
- Use UID for cancel/close operations
- Thread-safe UID-based routing

---

## 🎯 **Phase 4 - Convenience Functions**

### Goal:
Implement simple one-line HTTP functions.

### Tasks:

#### 4.1. Synchronous Convenience Functions
- `dap_http2_client_get_sync()` - Simple GET
- `dap_http2_client_post_sync()` - Simple POST

#### 4.2. Asynchronous Convenience Functions  
- `dap_http2_client_get_async()` - Simple async GET

#### 4.3. Profile-based Client
- `dap_http2_client_create_with_profile()` - For embedded transitions

---

## 🎯 **Phase 5 - Testing & Integration**

### Goal:
Test the complete implementation and integrate with existing codebase.

### Tasks:

#### 5.1. Unit Tests
- Client lifecycle tests
- Request management tests
- URL parsing tests
- Error handling tests

#### 5.2. Integration Tests
- Real HTTP requests
- HTTPS requests
- Streaming tests
- Chunked transfer tests
- Redirect handling tests

#### 5.3. Performance Tests
- Memory usage validation
- Zero-copy streaming verification
- Concurrent requests testing

#### 5.4. Legacy Compatibility
- Compare with old `dap_client_http` behavior
- Ensure feature parity
- Migration guide creation

---

## 📋 **Implementation Strategy**

### Incremental Approach:
1. **Build each phase completely** before moving to next
2. **Test each phase independently** 
3. **Use pseudocode first**, then real implementation
4. **Always use existing Session/Stream API** - never create new ones
5. **Adapt logic from old module** - don't rewrite from scratch

### Key Principles:
- ✅ **Universal Architecture**: Session/Stream work for ALL protocols
- ✅ **Proper Layering**: Client → Stream → Session
- ✅ **No Duplication**: Use existing APIs, don't create new ones
- ✅ **Adaptation**: Take old logic and fit it to new architecture

### Next Action:
Ready to start **Phase 2: Stream HTTP Mode**? 