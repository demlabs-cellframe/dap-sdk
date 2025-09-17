# DAP IO Module (dap_io.h)

## Overview

The `dap_io.h` module is a foundational component of DAP SDK, providing a high‑performance asynchronous I/O and event management system. It is responsible for:

- **Multithreaded event processing** - efficient load distribution across threads
- **Cross‑platform support** - Windows, Linux, macOS, BSD
- **Scalable architecture** - thousands of concurrent connections
- **Optimized pollers** - epoll, kqueue, IOCP per platform

## Architecture

### Core components

#### 1. **dap_events** - Event system
Main interface to initialize and manage the event system:

```c
// Initialize event system
int dap_events_init(uint32_t a_threads_count, size_t a_conn_timeout);

// Start event processing
int32_t dap_events_start();

// Stop all threads
void dap_events_stop_all();
```

#### 2. **dap_worker** - Worker thread
Represents a dedicated event processing thread:

```c
typedef struct dap_worker {
    uint32_t id;                           // Unique identifier
    dap_proc_thread_t *proc_queue_input;   // Processing queue
    dap_context_t *context;               // Execution context
    // ... additional queue fields
} dap_worker_t;
```

#### 3. **dap_context** - Execution context
Abstraction for managing threads and their resources:

```c
typedef struct dap_context {
    uint32_t id;              // Context ID
    pthread_t thread_id;      // Thread ID
    int type;                 // Context type
    bool is_running;          // Running status
    // ... platform‑dependent fields
} dap_context_t;
```

## Key capabilities

### Multithreading and load balancing

```c
// Get CPU count
uint32_t dap_get_cpu_count();

// Pin thread to a specific CPU
void dap_cpu_assign_thread_on(uint32_t a_cpu_id);

// Automatic load distribution
dap_worker_t *dap_events_worker_get_auto();
```

### Event socket management

```c
// Add socket to worker
void dap_worker_add_events_socket(dap_worker_t *a_worker,
                                  dap_events_socket_t *a_events_socket);

// Auto‑assign socket
dap_worker_t *dap_worker_add_events_socket_auto(dap_events_socket_t *a_es);
```

### Timers and callbacks

```c
// Execute callback on a worker
void dap_worker_exec_callback_on(dap_worker_t *a_worker,
                                 dap_worker_callback_t a_callback,
                                 void *a_arg);
```

## Platform support

### Linux (epoll)
- Uses epoll for efficient event polling
- Supports edge‑triggered and level‑triggered modes
- Optimized for large descriptor counts

### macOS/FreeBSD (kqueue)
- Uses kqueue for event management
- Filters for various event types
- High performance for network apps

### Windows (IOCP)
- Uses I/O Completion Ports
- Asynchronous I/O
- Optimized for overlapped operations

## Performance and scalability

### Optimizations
- **Lock‑free data structures** for queues
- **Scalable hash tables** for socket lookup
- **Platform‑optimized** pollers
- **Automatic load balancing** across threads

### Limits and configuration
```c
#define DAP_MAX_EVENTS_COUNT 8192  // Max number of events
#define DAP_EVENTS_SOCKET_MAX 1024 // Max sockets per context
```

## Usage

### Basic initialization

```c
#include "dap_events.h"
#include "dap_worker.h"

// Initialize system
if (dap_events_init(4, 30000) != 0) {  // 4 threads, 30s timeout
    fprintf(stderr, "Failed to initialize events system\n");
    return -1;
}

// Start processing
if (dap_events_start() != 0) {
    fprintf(stderr, "Failed to start events processing\n");
    return -1;
}

// Main application loop
dap_events_wait();

// Deinitialize
dap_events_deinit();
```

### Working with workers

```c
// Get current worker
dap_worker_t *current_worker = dap_worker_get_current();

// Get auto‑assigned worker
dap_worker_t *auto_worker = dap_events_worker_get_auto();

// Execute task on a specific worker
dap_worker_exec_callback_on(auto_worker, my_callback, my_data);
```

## Integration with other modules

### DAP Net
The IO module underpins the networking module, providing:
- Async handling of network connections
- Load balancing across threads
- Optimized pollers

### DAP Server
Works with the server module to:
- Handle incoming connections
- Manage connection timeouts
- Distribute load

### DAP Client
Enables async client connections:
- Non‑blocking read/write
- Multiple connection management
- Timeout handling

## Debugging and monitoring

### Debug features
```c
extern bool g_debug_reactor;  // Enable reactor debug

// Print info about all workers
void dap_worker_print_all();
```

### Stats and metrics
- Active connections count
- Worker load
- Queue statistics
- Event response time

## Best practices

### 1. Thread configuration
- Set thread count equal to CPU cores
- For I/O‑bound apps use 1.5–2x cores
- Monitor worker load for tuning

### 2. Resource management
- Always call `dap_events_deinit()` on shutdown
- Use automatic assignment for new sockets
- Monitor queue memory usage

### 3. Error handling
- Check return codes of all functions
- Handle connection timeouts
- Log critical event‑system errors

## Conclusion

The DAP SDK IO module provides a high‑performance, cross‑platform async I/O system that underpins all networking operations in the DAP ecosystem. Its architecture ensures the scalability and efficiency required for high‑load applications.
