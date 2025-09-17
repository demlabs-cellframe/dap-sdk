# DAP SDK Net Module - Networking

## Overview

The `dap-sdk/net` module provides a complete networking infrastructure for DAP SDK. It includes server and client components, multiple protocols, and high‑performance stream processing.

## Key capabilities

### 🌐 **Protocols and servers**
- **HTTP/HTTPS servers** with REST API
- **JSON‑RPC servers** for RPC
- **WebSocket servers** for real‑time comms
- **CLI servers** for remote control
- **Encryption servers** for secure connections

### 📡 **Client components**
- **HTTP clients** with proxy and SSL support
- **Link Manager** for connection management
- **Connection pooling** for resource efficiency

### ⚡ **Stream processing**
- **Stream processing** for high‑performance data handling
- **Session management** for connections
- **Channel processing** for multi‑channel comms
- **Cluster support** for scalability

## Module structure

```
dap-sdk/net/
├── server/                    # Server components
│   ├── http_server/         # HTTP server
│   ├── json_rpc/            # JSON‑RPC server
│   ├── cli_server/          # CLI server
│   ├── enc_server/          # Encrypted server
│   ├── notify_server/       # Notification server
│   └── test/                # Server tests
├── client/                   # Client components
│   ├── http/               # HTTP client
│   └── link_manager/       # Connection management
├── stream/                   # Stream processing
│   ├── stream/             # Core stream logic
│   ├── session/            # Session management
│   ├── ch/                 # Channel processing
│   └── test/               # Stream tests
├── common/                   # Common components
│   └── http/               # HTTP utilities
└── app-cli/                 # CLI apps
```

## Main components

### 1. **HTTP Server (http_server/)**

#### Architecture
```c
typedef struct dap_http_server {
    dap_server_t *server;           // Base server
    dap_http_cache_t *cache;         // HTTP cache
    dap_config_t *config;            // Configuration
    uint16_t port;                   // Server port
    bool use_ssl;                    // SSL support
    // ... additional fields
} dap_http_server_t;
```

#### Core functions

```c
// Create HTTP server
dap_http_server_t* dap_http_server_create(const char* addr, uint16_t port);

// Add URL handler
int dap_http_server_add_proc(dap_http_server_t* server,
                             const char* url_path,
                             http_proc_func_t proc_func,
                             void* arg);

// Start server
int dap_http_server_start(dap_http_server_t* server);

// Stop server
void dap_http_server_stop(dap_http_server_t* server);
```

### 2. **JSON-RPC Server (json_rpc/)**

#### Request/response structure
```c
typedef struct dap_json_rpc_request {
    int64_t id;                    // Request ID
    char* method;                  // Method name
    json_object* params;           // Parameters
    char* jsonrpc;                 // Protocol version ("2.0")
} dap_json_rpc_request_t;

typedef struct dap_json_rpc_response {
    int64_t id;                    // Response ID
    json_object* result;           // Result
    json_object* error;            // Error (if any)
    char* jsonrpc;                 // Protocol version
} dap_json_rpc_response_t;
```

#### API methods
```c
// Register RPC method
int dap_json_rpc_register_method(const char* method_name,
                                json_rpc_handler_func_t handler,
                                void* arg);

// Process incoming request
char* dap_json_rpc_process_request(const char* request_json);

// Create response
char* dap_json_rpc_create_response(int64_t id,
                                  json_object* result,
                                  json_object* error);
```

### 3. **Stream Processing (stream/)**

#### Stream architecture
```c
typedef struct dap_stream {
    dap_stream_session_t* session;    // Stream session
    dap_stream_worker_t* worker;      // Worker thread
    dap_stream_cluster_t* cluster;    // Cluster
    uint32_t id;                      // Stream ID
    void* internal;                   // Internal data
} dap_stream_t;

typedef struct dap_stream_session {
    uint64_t id;                      // Session ID
    time_t created;                   // Creation time
    time_t last_active;               // Last activity
    dap_stream_t* stream;             // Related stream
} dap_stream_session_t;
```

#### Packet processing
```c
// Create stream
dap_stream_t* dap_stream_create(uint32_t id);

// Handle incoming packet
int dap_stream_packet_in(dap_stream_t* stream,
                        dap_stream_pkt_t* packet);

// Send packet
int dap_stream_packet_out(dap_stream_t* stream,
                         dap_stream_pkt_t* packet);
```

## API Reference

### Server functions

#### dap_server_create()
```c
dap_server_t* dap_server_create(const char* addr, uint16_t port);
```
**Description**: Creates a new network server.

**Parameters**:
- `addr` - bind address (NULL for all interfaces)
- `port` - server port

**Returns**: Pointer to created server or NULL on error

#### dap_server_start()
```c
int dap_server_start(dap_server_t* server);
```
**Description**: Starts the server and begins accepting connections.

**Returns**:
- `0` - success
- `-1` - start error

### Client functions

#### dap_client_connect()
```c
dap_client_t* dap_client_connect(const char* addr, uint16_t port);
```
**Description**: Connects to a server.

**Parameters**:
- `addr` - server address
- `port` - server port

**Returns**: Pointer to client connection

#### dap_client_send()
```c
int dap_client_send(dap_client_t* client, const void* data, size_t size);
```
**Description**: Sends data to the server.

**Parameters**:
- `client` - client connection
- `data` - data to send
- `size` - data size

**Returns**: Number of bytes sent or -1 on error

## Usage examples

### Example 1: Simple HTTP server

```c
#include "dap_http_server.h"
#include "dap_http_simple.h"

int main() {
    // Initialization
    if (dap_enc_init() != 0) return -1;
    if (dap_http_init() != 0) return -1;

    // Create HTTP server
    dap_http_server_t* server = dap_http_server_create("0.0.0.0", 8080);
    if (!server) {
        printf("Failed to create HTTP server\n");
        return -1;
    }

    // Add simple handler
    dap_http_simple_proc_add(server, "/hello", hello_handler, NULL);

    // Start server
    if (dap_http_server_start(server) != 0) {
        printf("Failed to start HTTP server\n");
        return -1;
    }

    // Main loop
    while (1) {
        sleep(1);
    }

    return 0;
}

static void hello_handler(dap_http_simple_request_t* request,
                         dap_http_simple_response_t* response) {
    dap_http_simple_response_set_content(response,
                                       "Hello, World!",
                                       strlen("Hello, World!"),
                                       "text/plain");
}
```

### Example 2: JSON-RPC client

```c
#include "dap_json_rpc.h"
#include "dap_client.h"

int json_rpc_example() {
    // Connect to server
    dap_client_t* client = dap_client_connect("127.0.0.1", 8080);
    if (!client) {
        printf("Failed to connect to server\n");
        return -1;
    }

    // Create RPC request
    json_object* params = json_object_new_object();
    json_object_object_add(params, "name", json_object_new_string("world"));

    dap_json_rpc_request_t* request = dap_json_rpc_request_create(
        1,                          // Request ID
        "hello",                    // Method name
        params                      // Parameters
    );

    // Serialize to JSON
    char* request_json = dap_json_rpc_request_serialize(request);

    // Send request
    if (dap_client_send(client, request_json, strlen(request_json)) < 0) {
        printf("Failed to send request\n");
        free(request_json);
        dap_json_rpc_request_free(request);
        return -1;
    }

    free(request_json);
    dap_json_rpc_request_free(request);

    // Wait for response...
    // (response reading logic goes here)

    return 0;
}
```

### Example 3: Stream processing

```c
#include "dap_stream.h"
#include "dap_stream_session.h"

int stream_example() {
    // Create session
    dap_stream_session_t* session = dap_stream_session_create();
    if (!session) {
        printf("Failed to create session\n");
        return -1;
    }

    // Create stream
    dap_stream_t* stream = dap_stream_create(session, 1);
    if (!stream) {
        printf("Failed to create stream\n");
        dap_stream_session_delete(session);
        return -1;
    }

    // Set handlers
    stream->packet_in_callback = my_packet_handler;
    stream->error_callback = my_error_handler;

    // Start processing
    if (dap_stream_start(stream) != 0) {
        printf("Failed to start stream\n");
        dap_stream_delete(stream);
        dap_stream_session_delete(session);
        return -1;
    }

    // Main processing loop
    while (running) {
        // Process incoming packets
        dap_stream_process_packets(stream);

        // Small delay
        usleep(1000);
    }

    // Cleanup
    dap_stream_delete(stream);
    dap_stream_session_delete(session);

    return 0;
}
```

## Performance

### Networking benchmarks

| Component | Operation | Performance | Notes |
|-----------|----------|-------------|-------|
| **HTTP Server** | Requests/sec | ~10,000 | Intel Core i7 |
| **JSON-RPC** | Calls/sec | ~5,000 | Complex requests |
| **Stream Processing** | Packets/sec | ~100,000 | Small packets |
| **TCP Connections** | Establishment | ~1,000/sec | No SSL |
| **SSL Handshake** | Full | ~500/sec | AES‑256 |

### Optimizations

#### Connection Pooling
```c
// Create connection pool
dap_client_pool_t* pool = dap_client_pool_create("example.com", 443, 10);

// Get connection from pool
dap_client_t* client = dap_client_pool_get(pool);

// Use connection
dap_client_send(client, data, size);

// Return connection to pool
dap_client_pool_put(pool, client);
```

#### Zero-copy operations
```c
// Use zero-copy buffers
dap_buffer_t* buffer = dap_buffer_create_zero_copy(data, size);

// Send buffer without copying
dap_stream_send_buffer(stream, buffer);

// Free buffer (data not copied)
dap_buffer_free(buffer);
```

## Security

### Encrypted connections
```c
// Enable SSL/TLS
dap_server_config_t config = {
    .use_ssl = true,
    .cert_file = "/path/to/cert.pem",
    .key_file = "/path/to/key.pem",
    .ca_file = "/path/to/ca.pem"
};

dap_server_t* server = dap_server_create_ssl(&config);
```

### Authentication
```c
// Configure authentication
dap_auth_config_t auth = {
    .type = DAP_AUTH_TYPE_TOKEN,
    .token_secret = "your-secret-key",
    .token_expiry = 3600  // 1 hour
};

dap_server_set_auth(server, &auth);
```

## Configuration

### Core parameters
```ini
[net]
# HTTP server
http_port = 8080
http_max_connections = 1000
http_timeout = 30

# JSON-RPC
rpc_max_batch_size = 100
rpc_timeout = 60

# Streams
stream_buffer_size = 65536
stream_max_sessions = 10000
stream_timeout = 300
```

## Debugging and monitoring

### Logging
```c
#include "dap_log.h"

// Log networking events
dap_log(L_INFO, "Server started on port %d", port);
dap_log(L_DEBUG, "New connection from %s:%d", addr, port);
dap_log(L_ERROR, "Connection failed: %s", strerror(errno));
```

### Performance metrics
```c
// Get server statistics
dap_server_stats_t stats;
dap_server_get_stats(server, &stats);

printf("Active connections: %d\n", stats.active_connections);
printf("Total requests: %lld\n", stats.total_requests);
printf("Average response time: %.2f ms\n", stats.avg_response_time);
```

## Conclusion

The `dap-sdk/net` module provides a powerful and flexible networking stack:

### Key benefits:
- **High performance**: optimized protocol implementations
- **Scalability**: thousands of concurrent connections
- **Security**: built‑in SSL/TLS and authentication
- **Flexibility**: various protocols and formats

### Usage recommendations:
1. **REST APIs**: use HTTP Server with JSON‑RPC
2. **Real‑time**: use WebSocket or Stream processing
3. **Microservices**: use JSON‑RPC for inter‑service comms
4. **High‑perf systems**: use Stream processing with zero‑copy

For more details see:
- `dap_http_server.h` - HTTP server API
- `dap_json_rpc.h` - JSON‑RPC API
- `dap_stream.h` - Stream processing API
- `dap_client.h` - Client API
- Examples under `examples/net/`
- Tests under `test/net/`
