# DAP Net Stream Module (dap_stream.h)

## Overview

The `dap_stream` module provides high‑performance data streaming for DAP SDK. It implements:

- **Bidirectional streaming** - async read/write
- **Multi‑channel architecture** - multiple channels per stream
- **Clustering** - distributed stream processing
- **Security** - cryptographic protection and authentication
- **Compression** - traffic optimization

## Architectural role

The Stream module is the backbone for high‑performance communication in DAP:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP Net       │───▶│   Stream        │
│   Module        │    │   Module        │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │TCP/UDP  │             │Channels │
    │sockets  │             │& sessions│
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Low‑level│◄────────────►│High‑level│
    │transport│             │protocols │
    └─────────┘             └─────────┘
```

## Core data structures

### `dap_stream`
```c
typedef struct dap_stream {
    dap_stream_node_addr_t node;          // Node address
    bool authorized;                      // Stream authorized
    bool primary;                         // Primary stream
    int id;                               // Stream identifier

    // Connection management
    dap_events_socket_t *esocket;         // Event socket
    dap_stream_session_t *session;        // Stream session

    // Channels
    dap_stream_ch_t **channels;           // Channel array
    size_t channels_count;                // Channels count

    // Timers
    dap_timerfd_t *keepalive_timer;       // Keepalive timer

    // State
    bool is_active;                       // Stream active
    pthread_mutex_t mutex;                // Synchronization mutex

    void *_inheritor;                     // For inheritance
} dap_stream_t;
```

### `dap_stream_session`
```c
typedef struct dap_stream_session {
    uint32_t id;                          // Session ID
    dap_stream_node_addr_t node_addr;     // Node address
    dap_stream_t *stream;                 // Related stream

    // State management
    bool is_active;                       // Session active
    time_t create_time;                   // Creation time
    time_t last_activity;                 // Last activity

    // Session channels
    dap_stream_ch_t *channels;            // Session channels
    size_t channels_count;                // Channels count

    // Clustering
    dap_cluster_t *cluster;               // Cluster
    uint32_t cluster_member_id;           // Cluster member ID

    UT_hash_handle hh;                    // Hash table handle
} dap_stream_session_t;
```

### `dap_stream_ch`
```c
typedef struct dap_stream_ch {
    uint8_t type;                         // Channel type
    uint32_t id;                          // Channel ID

    // Relations
    dap_stream_t *stream;                 // Parent stream
    dap_stream_session_t *session;        // Session

    // Buffers
    dap_stream_ch_buf_t *buf;             // Channel buffer
    size_t buf_size;                      // Buffer size

    // Callbacks
    dap_stream_ch_callback_t ready_to_read;
    dap_stream_ch_callback_t ready_to_write;
    dap_stream_ch_callback_packet_t packet_in;
    dap_stream_ch_callback_packet_t packet_out;

    // State
    bool is_active;                       // Channel active
    uint32_t seq_id;                      // Sequence ID

    void *_inheritor;                     // For inheritance
} dap_stream_ch_t;
```

## Channel types

### Standard channel types
```c
#define DAP_STREAM_CH_ID_CONTROL   0x00   // Control channel
#define DAP_STREAM_CH_ID_FILE      0x01   // File channel
#define DAP_STREAM_CH_ID_SERVICE   0x02   // Service channel
#define DAP_STREAM_CH_ID_SECURITY  0x03   // Security channel
#define DAP_STREAM_CH_ID_MEDIA     0x04   // Media channel
#define DAP_STREAM_CH_ID_CUSTOM    0x10   // Custom channel
```

## Core functions

### Initialization and stream management

#### `dap_stream_init()`
```c
int dap_stream_init();
```

Initializes the stream system.

**Return values:**
- `0` - initialized successfully
- `-1` - initialization error

#### `dap_stream_deinit()`
```c
void dap_stream_deinit();
```

Deinitializes the stream system.

### Session creation and management

#### `dap_stream_session_create()`
```c
dap_stream_session_t *dap_stream_session_create(dap_stream_node_addr_t a_node_addr);
```

**Parameters:**
- `a_node_addr` - node address for the session

**Return value:**
- Pointer to the created session or NULL on error

#### `dap_stream_session_delete()`
```c
void dap_stream_session_delete(dap_stream_session_t *a_session);
```

**Parameters:**
- `a_session` - session to delete

### Channel operations

#### `dap_stream_ch_new()`
```c
dap_stream_ch_t *dap_stream_ch_new(dap_stream_t *a_stream, uint8_t a_type);
```

**Parameters:**
- `a_stream` - stream owning the channel
- `a_type` - channel type

**Return value:**
- Pointer to the created channel or NULL on error

#### `dap_stream_ch_delete()`
```c
void dap_stream_ch_delete(dap_stream_ch_t *a_ch);
```

**Parameters:**
- `a_ch` - channel to delete

### Data transfer

#### `dap_stream_ch_packet_write()`
```c
int dap_stream_ch_packet_write(dap_stream_ch_t *a_ch, uint8_t a_type,
                              const void *a_data, size_t a_data_size);
```

**Parameters:**
- `a_ch` - channel to write to
- `a_type` - packet type
- `a_data` - data to send
- `a_data_size` - data size

**Return values:**
- `0` - sent successfully
- `-1` - send error

#### `dap_stream_ch_packet_read()`
```c
size_t dap_stream_ch_packet_read(dap_stream_ch_t *a_ch, uint8_t *a_type,
                                void *a_data, size_t a_data_max_size);
```

**Parameters:**
- `a_ch` - channel to read from
- `a_type` - output packet type
- `a_data` - data buffer
- `a_data_max_size` - max buffer size

**Return value:**
- Number of bytes read or 0 on error

## Callback functions

### `dap_stream_ch_callback_t`
```c
typedef void (*dap_stream_ch_callback_t)(dap_stream_ch_t *a_ch, void *a_arg);
```

**Parameters:**
- `a_ch` - channel
- `a_arg` - user argument

### `dap_stream_ch_callback_packet_t`
```c
typedef size_t (*dap_stream_ch_callback_packet_t)(dap_stream_ch_t *a_ch,
                                                 uint8_t a_type,
                                                 void *a_data, size_t a_data_size,
                                                 void *a_arg);
```

**Parameters:**
- `a_ch` - channel
- `a_type` - packet type
- `a_data` - packet data
- `a_data_size` - data size
- `a_arg` - user argument

**Return value:**
- Number of processed bytes

## Stream protocol

### Packet structure
```c
typedef struct dap_stream_packet_hdr {
    uint32_t size;                        // Data size
    uint8_t type;                         // Packet type
    uint32_t seq_id;                      // Sequence ID
    uint16_t ch_id;                       // Channel ID
    uint8_t flags;                        // Flags
} __attribute__((packed)) dap_stream_packet_hdr_t;
```

### Packet types
```c
#define DAP_STREAM_PKT_TYPE_DATA          0x01   // Data
#define DAP_STREAM_PKT_TYPE_CONTROL       0x02   // Control
#define DAP_STREAM_PKT_TYPE_KEEPALIVE     0x03   // Keepalive
#define DAP_STREAM_PKT_TYPE_CLOSE         0x04   // Close
#define DAP_STREAM_PKT_TYPE_ERROR         0x05   // Error
```

### Packet flags
```c
#define DAP_STREAM_PKT_FLAG_COMPRESSED    0x01   // Compressed data
#define DAP_STREAM_PKT_FLAG_ENCRYPTED     0x02   // Encrypted data
#define DAP_STREAM_PKT_FLAG_FRAGMENTED    0x04   // Fragmented packet
```

## Clustering

### Cluster structure
```c
typedef struct dap_cluster {
    uint32_t id;                          // Cluster ID
    char *name;                           // Cluster name

    // Members
    dap_list_t *members;                  // Member list
    size_t members_count;                 // Members count

    // Balancing
    dap_cluster_balancer_t balancer;      // Load balancer

    // Synchronization
    pthread_mutex_t mutex;                // Mutex
    pthread_cond_t cond;                  // Condition variable
} dap_cluster_t;
```

### Cluster management

#### `dap_cluster_add_member()`
```c
int dap_cluster_add_member(dap_cluster_t *a_cluster,
                          dap_stream_node_addr_t a_member_addr);
```

**Parameters:**
- `a_cluster` - cluster
- `a_member_addr` - new member address

**Return values:**
- `0` - added successfully
- `-1` - add failed

#### `dap_cluster_remove_member()`
```c
int dap_cluster_remove_member(dap_cluster_t *a_cluster,
                             dap_stream_node_addr_t a_member_addr);
```

**Parameters:**
- `a_cluster` - cluster
- `a_member_addr` - member address to remove

**Return values:**
- `0` - removed successfully
- `-1` - remove failed

## Security and authentication

### Stream authentication
```c
// Verify stream signature
bool dap_stream_verify_signature(dap_stream_t *a_stream,
                                dap_sign_t *a_sign);

// Authenticate node
bool dap_stream_authenticate_node(dap_stream_t *a_stream,
                                 dap_stream_node_addr_t a_node_addr);
```

### Data encryption
```c
// Encrypt packet
int dap_stream_encrypt_packet(dap_stream_packet_t *a_packet,
                             dap_enc_key_t *a_key);

// Decrypt packet
int dap_stream_decrypt_packet(dap_stream_packet_t *a_packet,
                             dap_enc_key_t *a_key);
```

## Performance and optimizations

### Optimizations
- **Zero-copy buffers** - minimize data copies
- **Async I/O** - asynchronous I/O
- **Connection pooling** - reuse connections
- **Adaptive compression** - adaptive compression

### Keepalive mechanism
```c
#define STREAM_KEEPALIVE_TIMEOUT 3        // Keepalive timeout in seconds

// Send keepalive packet
void dap_stream_send_keepalive(dap_stream_t *a_stream);

// Process keepalive packet
void dap_stream_process_keepalive(dap_stream_t *a_stream);
```

### Performance statistics
```c
typedef struct dap_stream_stats {
    uint64_t packets_sent;                // Packets sent
    uint64_t packets_received;            // Packets received
    uint64_t bytes_sent;                  // Bytes sent
    uint64_t bytes_received;              // Bytes received
    uint32_t active_channels;             // Active channels
    double avg_latency;                   // Average latency
} dap_stream_stats_t;

// Get statistics
dap_stream_stats_t dap_stream_get_stats(dap_stream_t *a_stream);
```

## Usage

### Basic stream setup

```c
#include "dap_stream.h"
#include "dap_stream_session.h"

// Initialize stream system
if (dap_stream_init() != 0) {
    fprintf(stderr, "Failed to initialize stream system\n");
    return -1;
}

// Create session
dap_stream_node_addr_t node_addr = {.addr = inet_addr("127.0.0.1"), .port = 8080};
dap_stream_session_t *session = dap_stream_session_create(node_addr);

if (!session) {
    fprintf(stderr, "Failed to create stream session\n");
    return -1;
}

// Create stream
dap_stream_t *stream = dap_stream_create(session);
if (!stream) {
    fprintf(stderr, "Failed to create stream\n");
    return -1;
}

// Main stream logic
// ...

// Cleanup
dap_stream_delete(stream);
dap_stream_session_delete(session);
dap_stream_deinit();
```

### Working with channels

```c
// Callback to handle incoming packets
size_t packet_handler(dap_stream_ch_t *ch, uint8_t type,
                     void *data, size_t data_size, void *arg) {
    printf("Received packet type: %d, size: %zu\n", type, data_size);

    // Process data depending on type
    switch (type) {
        case DAP_STREAM_PKT_TYPE_DATA:
            process_data_packet(data, data_size);
            break;
        case DAP_STREAM_PKT_TYPE_CONTROL:
            process_control_packet(data, data_size);
            break;
        default:
            fprintf(stderr, "Unknown packet type: %d\n", type);
            return 0;
    }

    return data_size; // Number of processed bytes
}

// Create channel
dap_stream_ch_t *channel = dap_stream_ch_new(stream, DAP_STREAM_CH_ID_CONTROL);
if (!channel) {
    fprintf(stderr, "Failed to create channel\n");
    return -1;
}

// Register callback for incoming packets
channel->packet_in = packet_handler;

// Send data
const char *message = "Hello, World!";
if (dap_stream_ch_packet_write(channel, DAP_STREAM_PKT_TYPE_DATA,
                              message, strlen(message)) != 0) {
    fprintf(stderr, "Failed to send packet\n");
}
```

### Asynchronous processing

```c
// Ready-to-read callback
void ready_to_read_callback(dap_stream_ch_t *ch, void *arg) {
    uint8_t packet_type;
    char buffer[1024];

    size_t bytes_read = dap_stream_ch_packet_read(ch, &packet_type,
                                                 buffer, sizeof(buffer));

    if (bytes_read > 0) {
        printf("Read %zu bytes of type %d\n", bytes_read, packet_type);
        // Handle received data
        process_received_data(buffer, bytes_read);
    }
}

// Register callbacks
channel->ready_to_read = ready_to_read_callback;
channel->ready_to_write = ready_to_write_callback;

// Start async processing
dap_stream_start_async_processing(stream);
```

### Working with clusters

```c
// Create cluster
dap_cluster_t *cluster = dap_cluster_create("my_cluster");
if (!cluster) {
    fprintf(stderr, "Failed to create cluster\n");
    return -1;
}

// Add members
dap_stream_node_addr_t member1 = {.addr = inet_addr("192.168.1.10"), .port = 8080};
dap_stream_node_addr_t member2 = {.addr = inet_addr("192.168.1.11"), .port = 8080};

dap_cluster_add_member(cluster, member1);
dap_cluster_add_member(cluster, member2);

// Attach session to cluster
dap_stream_session_join_cluster(session, cluster);

// Enable load balancing
dap_cluster_enable_load_balancing(cluster, true);
```

## Advanced capabilities

### Custom channels

```c
// Define a new channel type
#define DAP_STREAM_CH_ID_CUSTOM_ENCRYPTED 0x20

// Create custom channel
dap_stream_ch_t *encrypted_channel = dap_stream_ch_new(
    stream, DAP_STREAM_CH_ID_CUSTOM_ENCRYPTED);

// Configure channel encryption
dap_enc_key_t *channel_key = dap_enc_key_generate(DAP_ENC_KEY_TYPE_AES, 256);
dap_stream_ch_set_encryption(encrypted_channel, channel_key);

// Use encrypted channel
dap_stream_ch_packet_write(encrypted_channel,
                          DAP_STREAM_PKT_TYPE_DATA,
                          sensitive_data, data_size);
```

### Monitoring and debugging

```c
// Enable debugging
extern int g_dap_stream_debug_more;
g_dap_stream_debug_more = 1;

// Get statistics
dap_stream_stats_t stats = dap_stream_get_stats(stream);
printf("Packets sent: %llu\n", stats.packets_sent);
printf("Packets received: %llu\n", stats.packets_received);
printf("Active channels: %u\n", stats.active_channels);
printf("Average latency: %.2f ms\n", stats.avg_latency);

// Monitor channel state
for (size_t i = 0; i < stream->channels_count; i++) {
    dap_stream_ch_t *ch = stream->channels[i];
    if (ch->is_active) {
        printf("Channel %u: active, seq_id: %u\n", ch->id, ch->seq_id);
    }
}
```

## Integration with other modules

### DAP Events
- Asynchronous event processing
- Timer management
- I/O readiness callbacks

### DAP Crypto
- Channel data encryption
- Packet digital signatures
- Participant authentication

### DAP Net Server
- HTTP-over-Stream
- WebSocket support
- REST API integration

## Common issues

### 1. Packet loss
```
Symptom: Packets are lost on high‑load channels
Solution: Increase buffer sizes and tune flow control
```

### 2. High latency
```
Symptom: High data transfer latency
Solution: Optimize packet size and keepalive frequency
```

### 3. Channel overload
```
Symptom: Channel buffer overflow
Solution: Implement rate limiting and backpressure
```

## Conclusion

The `dap_stream` module provides a powerful and flexible data streaming system with multi‑channel support, clustering, and high performance. Its architecture is optimized for networked applications requiring reliable and efficient communication.

