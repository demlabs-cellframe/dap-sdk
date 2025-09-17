# DAP Net Server Module (dap_http_server.h)

## Overview

The `dap_http_server` module provides a high‑performance HTTP/HTTPS server for DAP SDK. It includes:

- **Multi‑protocol support** - HTTP/1.1, HTTPS with TLS
- **Virtual hosts** - multiple domains support
- **URL processors** - flexible routing system
- **Caching** - in‑memory and disk caching
- **WebSocket support** - for real‑time communication
- **SSL/TLS encryption** - with SNI support

## Architectural role

The HTTP Server is a key component of the DAP networking stack:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP Net       │───▶│  HTTP Server    │
│   Module        │    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Transport│             │Application│
    │layer    │             │layer      │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │TCP/UDP  │◄────────────►│REST API │
    │sockets  │             │WebSocket│
    └─────────┘             └─────────┘
```

## Core data structures

### `dap_http_server`
```c
typedef struct dap_http_server {
    char *server_name;                    // Server name
    struct dap_server *internal_server; // Internal DAP server
    dap_http_url_proc_t *url_proc;       // URL processors
    void *_inheritor;                    // For inheritance
} dap_http_server_t;
```

### `dap_http_url_proc`
```c
typedef struct dap_http_url_proc {
    char url[512];                       // URL pattern
    struct dap_http_server *http;       // HTTP server

    dap_http_cache_t *cache;             // Cache
    pthread_rwlock_t cache_rwlock;       // Cache lock

    // Callback functions
    dap_http_client_callback_t new_callback;
    dap_http_client_callback_t delete_callback;
    dap_http_client_callback_t headers_read_callback;
    dap_http_client_callback_write_t headers_write_callback;
    dap_http_client_callback_t data_read_callback;

    void *internal;                      // Internal data
    UT_hash_handle hh;                   // Hash table handle
} dap_http_url_proc_t;
```

## Core functions

### Server creation and management

#### `dap_http_server_create()`
```c
dap_http_server_t *dap_http_server_create(const char *a_server_name);
```

**Parameters:**
- `a_server_name` - server name

**Return value:**
- Pointer to created HTTP server or NULL on error

#### `dap_http_server_delete()`
```c
void dap_http_server_delete(dap_http_server_t *a_http_server);
```

**Parameters:**
- `a_http_server` - HTTP server to delete

### Adding URL processors

#### `dap_http_server_add_proc()`
```c
bool dap_http_server_add_proc(dap_http_server_t *a_http_server,
                              const char *a_url_path,
                              dap_http_client_callback_t a_new_callback,
                              dap_http_client_callback_t a_delete_callback,
                              dap_http_client_callback_t a_headers_read_callback,
                              dap_http_client_callback_write_t a_headers_write_callback,
                              dap_http_client_callback_t a_data_read_callback);
```

**Parameters:**
- `a_http_server` - HTTP server
- `a_url_path` - URL path to process
- `a_new_callback` - new connection callback
- `a_delete_callback` - connection close callback
- `a_headers_read_callback` - headers read callback
- `a_headers_write_callback` - headers write callback
- `a_data_read_callback` - data read callback

**Return value:**
- `true` - added successfully
- `false` - add failed

### Cache management

#### `dap_http_server_cache_ctl()`
```c
int dap_http_server_cache_ctl(dap_http_server_t *a_http_server,
                              const char *a_url_path,
                              dap_http_cache_ctl_command_t a_command,
                              void *a_arg);
```

**Cache control commands:**
```c
typedef enum {
    DAP_HTTP_CACHE_CTL_SET_MAX_SIZE,     // Set max size
    DAP_HTTP_CACHE_CTL_GET_MAX_SIZE,     // Get max size
    DAP_HTTP_CACHE_CTL_CLEAR,            // Clear cache
    DAP_HTTP_CACHE_CTL_STATS             // Get statistics
} dap_http_cache_ctl_command_t;
```

## Callback types

### `dap_http_client_callback_t`
```c
typedef void (*dap_http_client_callback_t)(struct dap_http_client *a_client, void *a_arg);
```

**Parameters:**
- `a_client` - HTTP client
- `a_arg` - user argument

### `dap_http_client_callback_write_t`
```c
typedef size_t (*dap_http_client_callback_write_t)(struct dap_http_client *a_client,
                                                  void *a_arg, uint8_t *a_buf,
                                                  size_t a_buf_size);
```

**Parameters:**
- `a_client` - HTTP client
- `a_arg` - user argument
- `a_buf` - output buffer
- `a_buf_size` - buffer size

**Return value:**
- Number of bytes written

## HTTP Client

### `dap_http_client` structure
```c
typedef struct dap_http_client {
    struct dap_http_url_proc *proc;       // URL processor
    dap_http_t *http;                     // HTTP parser
    void *internal;                       // Internal data

    // State
    bool is_alive;                        // Client is alive
    bool is_closed;                       // Client is closed

    // Request data
    char *in_query_string;                // Query string
    char *in_path;                        // Request path
    char *in_method;                      // HTTP method

    // Headers
    dap_http_header_t *in_headers;        // Incoming headers
    dap_http_header_t *out_headers;       // Outgoing headers

    // Request/response body
    uint8_t *in_body;                     // Request body
    size_t in_body_size;                  // Request body size

    uint8_t *out_body;                    // Response body
    size_t out_body_size;                 // Response body size
} dap_http_client_t;
```

## HTTP Headers

### `dap_http_header_t`
```c
typedef struct dap_http_header {
    char *name;                           // Header name
    char *value;                          // Header value
    UT_hash_handle hh;                    // Hash table handle
} dap_http_header_t;
```

### Header helpers

#### `dap_http_header_parse()`
```c
dap_http_header_t *dap_http_header_parse(const char *a_header_line);
```

**Parameters:**
- `a_header_line` - HTTP header line

**Return value:**
- Parsed header or NULL on error

#### `dap_http_header_add()`
```c
void dap_http_header_add(dap_http_header_t **a_headers, const char *a_name,
                        const char *a_value);
```

**Parameters:**
- `a_headers` - headers table pointer
- `a_name` - header name
- `a_value` - header value

## Caching

### Cache structure
```c
typedef struct dap_http_cache {
    size_t max_size;                      // Max size
    size_t current_size;                  // Current size
    dap_http_cache_item_t *items;         // Cache items
    pthread_rwlock_t rwlock;              // RW lock
} dap_http_cache_t;
```

### Cache control

#### `dap_http_cache_init()`
```c
dap_http_cache_t *dap_http_cache_init(size_t a_max_size);
```

**Parameters:**
- `a_max_size` - maximum cache size in bytes

**Return value:**
- Initialized cache or NULL on error

#### `dap_http_cache_get()`
```c
void *dap_http_cache_get(dap_http_cache_t *a_cache, const char *a_key,
                        size_t *a_data_size);
```

**Parameters:**
- `a_cache` - cache
- `a_key` - lookup key
- `a_data_size` - data size output

**Return value:**
- Cached data or NULL if not found

#### `dap_http_cache_set()`
```c
bool dap_http_cache_set(dap_http_cache_t *a_cache, const char *a_key,
                       const void *a_data, size_t a_data_size,
                       time_t a_ttl);
```

**Parameters:**
- `a_cache` - cache
- `a_key` - key
- `a_data` - data to cache
- `a_data_size` - data size
- `a_ttl` - TTL in seconds

**Return value:**
- `true` - cached successfully
- `false` - error

## HTTP Methods and Statuses

### Supported HTTP methods
```c
#define DAP_HTTP_METHOD_GET     "GET"
#define DAP_HTTP_METHOD_POST    "POST"
#define DAP_HTTP_METHOD_PUT     "PUT"
#define DAP_HTTP_METHOD_DELETE  "DELETE"
#define DAP_HTTP_METHOD_HEAD    "HEAD"
#define DAP_HTTP_METHOD_OPTIONS "OPTIONS"
```

### HTTP status codes
```c
#define DAP_HTTP_STATUS_200     200  // OK
#define DAP_HTTP_STATUS_201     201  // Created
#define DAP_HTTP_STATUS_204     204  // No Content
#define DAP_HTTP_STATUS_400     400  // Bad Request
#define DAP_HTTP_STATUS_401     401  // Unauthorized
#define DAP_HTTP_STATUS_403     403  // Forbidden
#define DAP_HTTP_STATUS_404     404  // Not Found
#define DAP_HTTP_STATUS_500     500  // Internal Server Error
```

## Usage

### Creating a simple HTTP server

```c
#include "dap_http_server.h"

// Request handler callback
void request_handler(dap_http_client_t *client, void *arg) {
    // Read request data
    printf("Method: %s\n", client->in_method);
    printf("Path: %s\n", client->in_path);

    // Build response
    const char *response = "<html><body>Hello DAP!</body></html>";

    // Set response headers
    dap_http_header_add(&client->out_headers, "Content-Type",
                       "text/html; charset=utf-8");
    dap_http_header_add(&client->out_headers, "Content-Length",
                       "37");

    // Set response body
    client->out_body = (uint8_t *)strdup(response);
    client->out_body_size = strlen(response);
}

int main() {
    // Create HTTP server
    dap_http_server_t *server = dap_http_server_create("my_server");
    if (!server) {
        fprintf(stderr, "Failed to create HTTP server\n");
        return -1;
    }

    // Add processor for all URLs
    if (!dap_http_server_add_proc(server, "/*",
                                  request_handler, NULL, NULL, NULL, NULL)) {
        fprintf(stderr, "Failed to add URL processor\n");
        return -1;
    }

    // Server runs via DAP server
    // server->internal_server is already configured to handle HTTP

    // Wait
    pause();

    // Cleanup
    dap_http_server_delete(server);

    return 0;
}
```

### Working with headers

```c
void headers_handler(dap_http_client_t *client, void *arg) {
    // Read incoming headers
    dap_http_header_t *header = NULL;
    dap_http_header_t *headers = client->in_headers;

    HASH_ITER(hh, headers, header, tmp) {
        printf("Header: %s = %s\n", header->name, header->value);
    }

    // Add response headers
    dap_http_header_add(&client->out_headers, "Server", "DAP HTTP Server");
    dap_http_header_add(&client->out_headers, "X-Powered-By", "DAP SDK");
}

// Register processors
dap_http_server_add_proc(server, "/api/*",
                         NULL, NULL, headers_handler, NULL, request_handler);
```

### Using cache

```c
// Cache callback
void cache_handler(dap_http_client_t *client, void *arg) {
    const char *cache_key = client->in_path;

    // Try to get data from cache
    size_t data_size;
    void *cached_data = dap_http_cache_get(client->proc->cache,
                                          cache_key, &data_size);

    if (cached_data) {
        // Cache hit
        client->out_body = cached_data;
        client->out_body_size = data_size;
        dap_http_header_add(&client->out_headers, "X-Cache", "HIT");
    } else {
        // Cache miss, generate response
        const char *response = generate_response(client);

        // Store in cache
        dap_http_cache_set(client->proc->cache, cache_key,
                          response, strlen(response), 300); // 5 минут

        client->out_body = (uint8_t *)strdup(response);
        client->out_body_size = strlen(response);
        dap_http_header_add(&client->out_headers, "X-Cache", "MISS");
    }
}

// Configure cache for URL processor
dap_http_server_cache_ctl(server, "/api/data/*",
                          DAP_HTTP_CACHE_CTL_SET_MAX_SIZE,
                          (void *)1024 * 1024); // 1MB cache
```

## Performance and optimizations

### Server optimizations
- **Asynchronous processing** - non‑blocking operations
- **Connection pooling** - connection reuse
- **Memory pooling** - memory management
- **Zero‑copy operations** - minimize data copying

### Caching
- **In‑memory cache** - for frequent requests
- **LRU eviction** - evict rarely used data
- **TTL support** - time‑to‑live for cached data
- **Thread‑safe access** - safe concurrent access

## Security

### Attack protection
- **Request size limits**
- **Rate limiting**
- **Input validation**
- **CORS support**

### HTTPS support
```c
// SSL/TLS configuration
dap_ssl_config_t ssl_config = {
    .certificate_file = "/etc/ssl/server.crt",
    .private_key_file = "/etc/ssl/server.key",
    .ca_certificate_file = "/etc/ssl/ca.crt"
};

dap_http_server_enable_ssl(server, &ssl_config);
```

## Integration with other modules

### DAP Server
- Base network server
- Connection management
- Protocol handling

### DAP Events
- Asynchronous event processing
- Thread management
- Timers and callbacks

### DAP Config
- Load server configuration
- Parameter tuning
- Settings validation

## Typical use cases

### 1. REST API server
```c
// REST API handler
void api_handler(dap_http_client_t *client, void *arg) {
    if (strcmp(client->in_method, "GET") == 0) {
        // Handle GET requests
        handle_get_request(client);
    } else if (strcmp(client->in_method, "POST") == 0) {
        // Handle POST requests
        handle_post_request(client);
    } else {
        // Method not allowed
        client->out_status = 405; // Method Not Allowed
    }
}

// Register API handler
dap_http_server_add_proc(server, "/api/v1/*",
                         api_handler, NULL, NULL, NULL, NULL);
```

### 2. Static file server
```c
void file_handler(dap_http_client_t *client, void *arg) {
    const char *filepath = get_filepath_from_url(client->in_path);

    if (access(filepath, F_OK) == 0) {
        // File exists
        serve_file(client, filepath);
        client->out_status = 200;
    } else {
        // File not found
        client->out_status = 404;
        client->out_body = (uint8_t *)strdup("File not found");
        client->out_body_size = 13;
    }
}

dap_http_server_add_proc(server, "/static/*",
                         file_handler, NULL, NULL, NULL, NULL);
```

### 3. WebSocket server
```c
void websocket_upgrade_handler(dap_http_client_t *client, void *arg) {
// Check Upgrade header
    const char *upgrade = dap_http_header_find(client->in_headers, "Upgrade");

    if (upgrade && strcmp(upgrade, "websocket") == 0) {
        // Perform WebSocket handshake
        perform_websocket_handshake(client);
    } else {
        client->out_status = 400; // Bad Request
    }
}

dap_http_server_add_proc(server, "/ws",
                         websocket_upgrade_handler, NULL, NULL, NULL, NULL);
```

## Conclusion

The `dap_http_server` module provides a full‑featured HTTP/HTTPS server with modern web standards support. Its integration with the rest of the DAP SDK ecosystem ensures high performance and scalability for web applications.

