# DAP SDK IO Layer — Architecture and Implementation

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/io/`

## Overview

The IO layer is the foundation of the DAP SDK networking stack. It provides cross-platform event-driven I/O with a worker thread pool model where each worker handles its own subset of sockets. All higher layers (transport, stream, channels) are built on top of this abstract I/O.

**Position in the stack:**

```
┌─────────────────────────────────────────────────────┐
│ L4: Applications (VPN, Services, Channels)          │
├─────────────────────────────────────────────────────┤
│ L3: DAP Stream Protocol (dap_stream_t)              │
├─────────────────────────────────────────────────────┤
│ L2: Transport Abstraction (dap_net_trans_t)         │
├─────────────────────────────────────────────────────┤
│ L1: Concrete Transports (TLS, UDP, HTTP, DNS)       │
├─────────────────────────────────────────────────────┤
│ L0: IO Layer ← THIS DOCUMENT                        │
│     (dap_events, dap_events_socket, dap_worker)     │
└─────────────────────────────────────────────────────┘
```

## Event Loop Architecture

### Platform Abstraction

The IO layer automatically selects the event notification mechanism based on the platform:

| Platform | Mechanism | Define |
|----------|-----------|--------|
| Linux | epoll | `DAP_EVENTS_CAPS_EPOLL` |
| macOS / iOS | kqueue | `DAP_EVENTS_CAPS_KQUEUE` |
| BSD | kqueue | `DAP_EVENTS_CAPS_KQUEUE` |
| Windows | IOCP | `DAP_EVENTS_CAPS_IOCP` |
| Windows (alt.) | wepoll (epoll over IOCP) | `DAP_EVENTS_CAPS_WEPOLL` |
| Android / fallback | poll | `DAP_EVENTS_CAPS_POLL` |

### Thread Model

```
┌──────────────────────────────────────────────────┐
│                  dap_events_t                     │
│              (main event loop)                    │
│                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ Worker 0 │ │ Worker 1 │ │ Worker N │  ...     │
│  │ (thread) │ │ (thread) │ │ (thread) │         │
│  │          │ │          │ │          │         │
│  │ es[0..M] │ │ es[0..M] │ │ es[0..M] │         │
│  └──────────┘ └──────────┘ └──────────┘         │
│                                                  │
│  ┌──────────────────────────────────────┐        │
│  │         dap_proc_thread_t            │        │
│  │    (processing thread pool)          │        │
│  └──────────────────────────────────────┘        │
└──────────────────────────────────────────────────┘
```

- **Worker thread** (`dap_worker_t`) — each worker has its own event loop and handles a set of sockets. Sockets are bound to a worker at creation or reassigned by a balancer.
- **Proc thread** (`dap_proc_thread_t`) — thread pool for heavy computational tasks (not IO).
- Sticky binding: a socket is bound to one worker for its entire lifetime. Reassignment between workers is possible via `dap_events_socket_reassign_between_workers_mt()`.

## Core Structure: dap_events_socket_t

`dap_events_socket_t` is the central IO layer abstraction. It represents any descriptor: TCP socket, UDP socket, pipe, timer, or event notification.

### Key Fields

```c
typedef struct dap_events_socket {
    SOCKET              socket;          // Socket descriptor
    dap_events_desc_type_t type;         // Descriptor type
    dap_events_socket_uuid_t uuid;       // Unique ID (uint64_t)

    // Buffers
    byte_t              *buf_in;         // Input buffer
    byte_t              *buf_out;        // Output buffer
    size_t              buf_in_size, buf_in_size_max;
    size_t              buf_out_size, buf_out_size_max;

    // Datagram queue (for UDP/SCTP)
    dap_events_socket_packet_queue_t *packet_queue;

    // Addressing
    struct sockaddr_storage addr_storage;
    char                remote_addr_str[256];
    uint16_t            remote_port;

    // Bindings
    dap_context_t       *context;
    dap_worker_t        *worker;
    dap_server_t        *server;

    // Callbacks
    dap_events_socket_callbacks_t callbacks;

    // User data
    void                *_inheritor;     // Public data (inheritor)
    void                *_pvt;           // Private data

    // Flags
    uint32_t            flags;
    atomic_bool         is_initalized;
} dap_events_socket_t;
```

### Descriptor Types

```c
typedef enum dap_events_desc_type {
    DESCRIPTOR_TYPE_SOCKET_CLIENT,          // TCP client
    DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT,    // Unix domain socket client
    DESCRIPTOR_TYPE_SOCKET_LISTENING,       // TCP listening socket
    DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING, // Unix domain listening
    DESCRIPTOR_TYPE_SOCKET_UDP,             // UDP socket
    DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL,      // SSL/TLS client
    DESCRIPTOR_TYPE_SOCKET_RAW,             // Raw socket
    DESCRIPTOR_TYPE_FILE,                   // File descriptor
    DESCRIPTOR_TYPE_PIPE,                   // Pipe
    DESCRIPTOR_TYPE_TIMER,                  // Timer fd
    DESCRIPTOR_TYPE_EVENT                   // Event notification
} dap_events_desc_type_t;
```

### Socket Flags

| Flag | Bit | Description |
|------|-----|-------------|
| `DAP_SOCK_READY_TO_READ` | 0 | Socket is ready for reading |
| `DAP_SOCK_READY_TO_WRITE` | 1 | Socket is ready for writing |
| `DAP_SOCK_SIGNAL_CLOSE` | 2 | Close signal pending |
| `DAP_SOCK_CONNECTING` | 3 | Connection in progress |
| `DAP_SOCK_REASSIGN_ONCE` | 4 | One-time reassignment |
| `DAP_SOCK_FILE_MAPPED` | 7 | Memory-mapped file |
| `DAP_SOCK_MSG_ORIENTED` | 8 | Datagram socket (UDP/SCTP) |

## Data Flow

### Reading (stream socket)

```
1. Event loop (epoll/kqueue) detects readability
2. → worker calls esocket->callbacks.read_callback()
3. → data is read into buf_in
4. → higher layer (stream) parses packets from buf_in
5. → buffer is cleared after processing
```

### Writing (stream socket)

```
1. Higher layer calls dap_events_socket_write(esocket, data, size)
2. → data is written to buf_out
3. → esocket is marked DAP_SOCK_READY_TO_WRITE
4. → event loop detects writability
5. → worker calls write_callback() → sends buf_out to socket
6. → buf_out is cleared
```

### Datagrams (UDP)

```
1. Event loop detects readability on UDP socket
2. → worker calls read_callback()
3. → recvfrom() reads datagram + sender address
4. → data + address are packed into dap_events_socket_packet_t
5. → packet is enqueued into packet_queue (ring buffer)
6. → higher layer dequeues packets
```

For sending datagrams, `dap_events_socket_sendto_unsafe()` is used, which calls `sendto()` with the specified destination address.

## Datagram Queue

```c
typedef struct dap_events_socket_packet_queue {
    dap_events_socket_packet_t  *packets;    // Packet array
    size_t                      count;       // Current count
    size_t                      capacity;    // Maximum capacity
    size_t                      head;        // Head index (ring buffer)
} dap_events_socket_packet_queue_t;

typedef struct dap_events_socket_packet {
    uint8_t     *data;
    size_t      size;
    struct sockaddr_storage addr;
    socklen_t   addr_len;
} dap_events_socket_packet_t;
```

Ring buffer provides O(1) insertion and extraction. Default capacity: `DAP_QUEUE_MAX_MSGS = 1024`.

## IO Flow Abstraction

Beyond basic sockets, the IO layer provides a "flow" abstraction — a high-level data stream with platform-specific optimizations:

| Module | Description | Platform |
|--------|-------------|----------|
| `dap_io_flow.c` | Base flow abstraction | All |
| `dap_io_flow_ctrl.c` | Flow control | All |
| `dap_io_flow_datagram.c` | Datagram flow | All |
| `dap_io_flow_socket.c` | Socket-based flow | All |
| `dap_io_flow_cbpf.c` | Classic BPF filtering | Linux |
| `dap_io_flow_ebpf.c` | eBPF filtering and routing | Linux |
| `dap_io_flow_bsd_lb.c` | Load balancing | BSD |
| `dap_io_flow_darwin_gcd.c` | Grand Central Dispatch | macOS |
| `dap_io_flow_win_rio.c` | Registered I/O | Windows |

## Automatic Worker Selection

When creating a new socket, the system automatically selects the least loaded worker via `dap_events_worker_get_auto()`:

**Algorithm:**
1. Find `l_min_count` — the minimum `event_sockets_count` across all workers
2. Atomically increment `s_worker_rr_counter` (round-robin)
3. Scan workers starting from `(l_rr + i) % s_threads_count`, return the first with `event_sockets_count == l_min_count`
4. Fallback: `s_workers[l_rr % s_threads_count]`

This prevents thundering herd on worker 0 and ensures even distribution.

## Activity Check (Idle Connection Timeout)

Workers periodically check socket activity:

| Parameter | Value | Description |
|-----------|-------|-------------|
| `s_connection_timeout` | 60 sec | Inactivity timeout (default) |
| Check interval | 30 sec | Timer fires every `timeout / 2` |
| Close condition | `last_time_active + timeout < now` | Socket idle for > 60 sec |

When an idle socket is detected:
1. Call `error_callback(ETIMEDOUT)`
2. Remove socket from worker's hash table
3. Verify `event_sockets_count` matches actual hash table count

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DAP_EVENTS_SOCKET_MAX` | 8194 | Max sockets per worker |
| `DAP_STREAM_PKT_FRAGMENT_SIZE` | 16 KB | Packet fragment size |
| `DAP_STREAM_PKT_SIZE_MAX` | 4 MB | Maximum packet size |
| `DAP_EVENTS_SOCKET_BUF_SIZE` | 256 KB | Socket buffer size (16 × 16KB) |
| `DAP_EVENTS_SOCKET_BUF_LIMIT` | 4 MB | Buffer limit |
| `DAP_QUEUE_MAX_MSGS` | 1024 | Max messages in queue |
| `DAP_HOSTADDR_STRLEN` | 256 | Address string length |
| `DAP_UDP_MAX_DATAGRAM_SIZE` | 65507 | Maximum UDP datagram size |
| `DAP_PACKET_QUEUE_INITIAL_CAPACITY` | 16 | Initial datagram queue capacity |
| `DAP_PACKET_QUEUE_MAX_CAPACITY` | 4096 | Maximum datagram queue capacity |

## Callback Model

```c
typedef struct dap_events_socket_callbacks {
    union {
        dap_events_socket_callback_t connected_callback;  // TCP connected
        dap_events_socket_callback_t accept_callback;     // New connection
        dap_events_socket_callback_t event_callback;      // Event signal
        dap_events_socket_callback_t queue_callback;      // Queue message
    };
    dap_events_socket_callback_t timer_callback;          // Timer fired
    dap_events_socket_callback_t new_callback;            // Socket created
    dap_events_socket_callback_t delete_callback;         // Socket deleted
    dap_events_socket_callback_t read_callback;           // Data available
    dap_events_socket_callback_t write_callback;          // Ready to write
    dap_events_socket_callback_t write_finished_callback; // Write complete
    dap_events_socket_callback_t error_callback;          // Error occurred
    dap_events_socket_callback_t worker_assign_callback;  // Assigned to worker
    dap_events_socket_callback_t worker_unassign_callback;// Unassigned
    void *arg;                                            // User data
} dap_events_socket_callbacks_t;
```

## Thread Safety

- **Unsafe functions** (suffix `_unsafe`) — called only in the context of the owning worker. No locks, maximum performance.
- **MT-safe functions** (suffix `_mt`) — use inter-thread communication (event signal or queue) for safe invocation from any thread.
- **Inter-context** (suffix `_inter`) — cross-worker operations via context queue.

Typical pattern: higher-level code obtains the socket UUID and uses `_mt` functions for writing from another thread.

## Related Documents

- [02 — Transport Abstraction Layer](02-transport-abstraction_en.md)
- [01 — DAP Stream Protocol](../protocol/01-stream-protocol_en.md)
- Header files: `io/include/dap_events_socket.h`, `dap_events.h`, `dap_worker.h`
