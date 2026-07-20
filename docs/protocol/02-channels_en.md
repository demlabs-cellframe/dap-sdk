# DAP Stream Channels -- Multiplexing Layer

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/stream/ch/`

## Overview

Channels constitute the fourth layer of the DAP SDK network stack. They provide multiplexing of multiple logical data streams within a single binary stream (`dap_stream_t`). Each channel has a type (VPN, GlobalDB, Chain, Gossip, etc.) defined through a vtable processor with four callback functions.

## Architecture

```
+----------------------------------------------------------+
| L4: Channels (dap_stream_ch_t) <-- THIS DOCUMENT         |
|     VPN ('S'), GlobalDB ('D'), Chain Net ('N'), Gossip ('G') |
+----------------------------------------------------------+
| L3: DAP Stream Protocol                                  |
|     dap_stream_t -> dap_stream_pkt_t                     |
+----------------------------------------------------------+
| L2: Transport Abstraction (dap_net_trans_t)              |
+----------------------------------------------------------+
| L1: TLS / UDP / HTTP / DNS / WebSocket                   |
+----------------------------------------------------------+
| L0: IO (dap_events_socket_t)                             |
+----------------------------------------------------------+
```

## Channel Structure: dap_stream_ch_t

Each `dap_stream_ch_t` instance represents a single logical channel within a stream:

```c
typedef struct dap_stream_ch {
    pthread_mutex_t         mutex;               // Mutex for thread safety
    bool                    ready_to_write;       // Write readiness flag
    bool                    ready_to_read;        // Read readiness flag
    bool                    closing;              // Channel closing flag
    dap_stream_t            *stream;              // Parent stream
    dap_stream_ch_uuid_t    uuid;                 // Unique channel ID
    dap_stream_worker_t     *stream_worker;       // Worker owning this channel
    struct {
        uint64_t            bytes_write;          // Bytes written
        uint64_t            bytes_read;           // Bytes read
    } stat;
    dap_list_t              *packet_in_notifiers; // Incoming packet notifier list
    dap_list_t              *packet_out_notifiers;// Outgoing packet notifier list
    dap_dap_stream_ch_proc_t *proc;               // Processor vtable (channel type)
    void                    *internal;            // Channel-specific private data
    struct dap_stream_ch    *me;                  // Self-pointer
    UT_hash_handle          hh_worker;            // Hash handle for per-worker lookup
} dap_stream_ch_t;
```

### mutex Field

The `mutex` field (`pthread_mutex_t`) protects channel state when modifying the `closing` flag during deletion. It guarantees that the delete callback (`delete_callback`) is invoked before channel resources are freed.

### State Flags

| Flag | Type | Description |
|------|------|-------------|
| `ready_to_write` | `bool` | Channel is ready to accept write operations. Managed via `dap_stream_ch_set_ready_to_write_unsafe()` |
| `ready_to_read` | `bool` | Channel is ready to receive data. Set to `true` on creation |
| `closing` | `bool` | Channel is in the process of being closed. Set in `dap_stream_ch_delete()`, blocks further operations |

### proc Field

Pointer to `dap_stream_ch_proc_t` -- a vtable defining the channel type and its handlers. Set during creation via lookup by ID.

### internal Field

`void*` pointer for storing channel-type-specific data. For example, a VPN channel stores VPN connection context here, while a GlobalDB channel stores replication state. Must be NULLed by `delete_callback`.

### Statistics

The anonymous nested `stat` block contains counters:
- `bytes_write` -- total bytes written (excluding headers)
- `bytes_read` -- total bytes read

### hh_worker Hash Handle

Each channel is added to the worker's hash table (`stream_worker->channels`) keyed by `uuid`. This enables fast channel lookup by UUID within the worker context. The hash table is protected by `channels_rwlock` (see "Thread Safety" section).

## Channel UUID

```c
typedef unsigned int dap_stream_ch_uuid_t;
```

A channel UUID is a 32-bit unsigned integer identifier. Generated atomically on channel creation:

```c
unsigned int dap_new_stream_ch_id() {
    static _Atomic unsigned int stream_ch_id = 0;
    return stream_ch_id++;
}
```

Atomic increment guarantees uniqueness even when channels are created concurrently from multiple threads.

## Channel Packet: dap_stream_ch_pkt_t

### Packet Header

```c
typedef struct dap_stream_ch_pkt_hdr {
    uint8_t     id;           // Channel ID (character, e.g. 'S', 'D', 'N')
    uint8_t     enc_type;     // Encryption type (0 = unencrypted)
    uint8_t     type;         // Packet type (defined by channel)
    uint8_t     padding;      // Alignment padding
    uint64_t    seq_id;       // Sequence ID (ordinal in stream)
    uint32_t    data_size;    // Payload data size
} DAP_ALIGN_PACKED dap_stream_ch_pkt_hdr_t;  // 16 bytes
```

### Wire Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   id (1)      |  enc_type (1) |   type (1)    |  padding (1)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       seq_id (8 bytes)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   seq_id cont.                | data_size (4) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   data_size cont.             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Total header size:** 16 bytes (`1 + 1 + 1 + 1 + 8 + 4`).

### Full Packet

```c
typedef struct dap_stream_ch_pkt {
    dap_stream_ch_pkt_hdr_t hdr;
    uint8_t                 data[];    // Flexible array member
} DAP_ALIGN_PACKED dap_stream_ch_pkt_t;
```

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| `STREAM_CH_PKT_TYPE_REQUEST` | `0x0` | Request (default) |

Specific packet types are defined by each channel individually. For example, a VPN channel may use types for tunnel management, while a GlobalDB channel uses them for replication commands.

## Channel Processor: dap_stream_ch_proc_t

A vtable defining the behavior of a specific channel type:

```c
typedef struct dap_stream_ch_proc {
    uint8_t                         id;                 // Character ID of channel type
    dap_stream_ch_callback_t        new_callback;       // Called on channel creation
    dap_stream_ch_callback_t        delete_callback;    // Called on channel deletion
    dap_stream_ch_read_callback_t   packet_in_callback; // Incoming packet handler (returns bool)
    dap_stream_ch_write_callback_t  packet_out_callback;// Outgoing packet handler (returns bool)
    void                            *internal;          // Processor private data
} dap_stream_ch_proc_t;
```

### Callback Functions

| Callback | Signature | Description |
|----------|-----------|-------------|
| `new_callback` | `void (*)(dap_stream_ch_t*, void*)` | Channel initialization. Creates `internal` data |
| `delete_callback` | `void (*)(dap_stream_ch_t*, void*)` | Channel destruction. Frees `internal` data |
| `packet_in_callback` | `bool (*)(dap_stream_ch_t*, void*)` | Incoming packet handler. Returns `true` on success |
| `packet_out_callback` | `bool (*)(dap_stream_ch_t*, void*)` | Outgoing packet handler. Returns `true` on success |

## Channel Registration

### Module Initialization

```c
int stream_ch_proc_init();     // Initialize processor registry
void stream_ch_proc_deinit();  // Deinitialize
```

### Global Registry

Channel processors are stored in a static array of 256 elements, indexed by ID (character):

```c
dap_stream_ch_proc_t s_proc[256] = {{0}};
```

### Registering a New Channel Type

```c
void dap_stream_ch_proc_add(
    uint8_t                         id,                 // Type ID (character, e.g. 'S')
    dap_stream_ch_callback_t        new_callback,       // Creation callback
    dap_stream_ch_callback_t        delete_callback,    // Deletion callback
    dap_stream_ch_read_callback_t   packet_in_callback, // Incoming packet handler
    dap_stream_ch_write_callback_t  packet_out_callback // Outgoing packet handler
);
```

Example of registering a VPN channel:
```c
dap_stream_ch_proc_add('S', vpn_ch_new, vpn_ch_delete, vpn_ch_pkt_in, vpn_ch_pkt_out);
```

### Processor Lookup

```c
dap_stream_ch_proc_t *dap_stream_ch_proc_find(uint8_t id);
```

Returns a pointer to `s_proc[id]`. If the processor was not registered, returns a pointer to a zero-initialized entry (all callbacks = NULL).

### Technical Channel

```c
#define TECHICAL_CHANNEL_ID 't'
```

Reserved ID for the technical (service) channel.

## Channel Lifecycle

### Creation: dap_stream_ch_new()

```
1. Call dap_stream_ch_proc_find(a_id) -- look up processor by ID
2. If processor not found -> return NULL
3. DAP_REALLOC_COUNT -- grow the stream's channel array
4. dap_stream_ch_alloc() -- allocate memory for dap_stream_ch_t
5. Initialize fields:
   - me = self
   - stream = parent stream
   - proc = found processor
   - ready_to_read = true
   - closing = false
   - uuid = dap_new_stream_ch_id() (atomic increment)
   - pthread_mutex_init(&mutex)
6. Resolve stream_worker from the stream
7. pthread_rwlock_wrlock(&channels_rwlock)
8. HASH_ADD_BYHASHVALUE -- add to worker's hash table
9. pthread_rwlock_unlock(&channels_rwlock)
10. Call proc->new_callback(ch, NULL)
11. Append to stream's channel[] array, increment channel_count
12. Return pointer to the new channel
```

### Deletion: dap_stream_ch_delete()

```
1. pthread_rwlock_wrlock(&channels_rwlock)
2. HASH_DELETE(hh_worker) -- remove from worker's hash table
3. pthread_rwlock_unlock(&channels_rwlock)
4. pthread_mutex_lock(&ch->mutex)
5. closing = true
6. Call proc->delete_callback(ch, NULL)
7. assert(!ch->internal) -- internal must be NULLed by delete_callback
8. Find channel index in stream->channel[] array
9. Shift array to fill the gap
10. If no channels remain -> free the array
11. pthread_mutex_unlock(&ch->mutex)
12. Deferred free via dap_worker_exec_callback_on(worker, s_stream_ch_free_callback, ch)
    -- prevents use-after-free during notifier iteration
13. If worker unavailable -> direct free via dap_stm_ch_free()
```

### Memory Release: dap_stm_ch_free()

```
1. (DAP_SYS_DEBUG) Look up tracking record in s_stm_chs hash table
2. HASH_DEL from s_stm_chs
3. dap_list_free_full(packet_in_notifiers)
4. dap_list_free_full(packet_out_notifiers)
5. DAP_DELETE(ch) -- free channel structure
```

## Thread Safety

### channels_rwlock

The worker's channel hash table (`stream_worker->channels`) is protected by `pthread_rwlock_t channels_rwlock`:

- **Read** (`pthread_rwlock_rdlock`): channel lookup by UUID, iteration
- **Write** (`pthread_rwlock_wrlock`): channel insertion/removal

```c
// Lookup (read-only)
pthread_rwlock_rdlock(&a_worker->channels_rwlock);
HASH_FIND_BYHASHVALUE(hh_worker, a_worker->channels, &a_uuid, sizeof(a_uuid), a_uuid, l_ch);
pthread_rwlock_unlock(&a_worker->channels_rwlock);
```

### Per-Channel Mutex

The `ch->mutex` protects the `closing` state and ensures atomicity of the deletion operation. Guarantees that `delete_callback` is invoked exactly once.

### Deferred Free

A channel is not freed immediately in `dap_stream_ch_delete()`. Instead, the free is scheduled through `dap_worker_exec_callback_on()`:

```c
// Defer actual free to worker's queue to avoid freeing while iterating notifiers
if (l_stream_worker && l_stream_worker->worker)
    dap_worker_exec_callback_on(l_stream_worker->worker, s_stream_ch_free_callback, a_ch);
else
    dap_stm_ch_free(a_ch);
```

This prevents use-after-free when `dap_stream_ch_delete()` is called from a notifier callback during iteration over `packet_in_notifiers` or `packet_out_notifiers`.

## Write Functions

### Single-Thread Write: dap_stream_ch_pkt_write_unsafe()

```c
size_t dap_stream_ch_pkt_write_unsafe(
    dap_stream_ch_t *a_ch,      // Channel (UNSAFE: worker context only)
    uint8_t         a_type,     // Packet type
    const void      *a_data,    // Data
    size_t          a_data_size // Data size
);
```

Direct write to the channel from the worker context. Builds the `dap_stream_ch_pkt_hdr_t` header, performs fragmentation if needed, passes data to `dap_stream_pkt_write_unsafe()`.

### Multi-Thread Write: dap_stream_ch_pkt_write_mt()

```c
size_t dap_stream_ch_pkt_write_mt(
    dap_stream_worker_t  *a_worker,   // Target worker
    dap_stream_ch_uuid_t a_ch_uuid,   // Channel UUID
    uint8_t              a_type,      // Packet type
    const void           *a_data,     // Data
    size_t               a_data_size  // Data size
);
```

Cross-thread write. Copies data and enqueues via `queue_ch_io`. When called from a different worker, uses the inter-worker queue `queue_ch_io_input[src][dst]`.

### Formatted Write (printf-style)

```c
// Single-thread
ssize_t dap_stream_ch_pkt_write_f_unsafe(dap_stream_ch_t *a_ch, uint8_t a_type, const char *a_format, ...);

// Multi-thread
size_t dap_stream_ch_pkt_write_f_mt(dap_stream_worker_t *a_worker, dap_stream_ch_uuid_t a_ch_uuid,
                                     uint8_t a_type, const char *a_format, ...);
```

Perform `vsnprintf()` to format the string, then pass the result to the corresponding write function.

### Send by esocket UUID

```c
int dap_stream_ch_pkt_send_mt(
    dap_stream_worker_t     *a_worker,   // Worker
    dap_events_socket_uuid_t a_uuid,     // Events socket UUID
    char                     a_ch_id,    // Channel ID (character)
    uint8_t                  a_type,     // Packet type
    const void               *a_data,    // Data
    size_t                   a_data_size // Data size
);
```

Sends data to a channel by events socket UUID. Data is copied and enqueued via `queue_ch_send`.

### Send by Node Address

```c
int dap_stream_ch_pkt_send_by_addr(
    dap_stream_node_addr_t *a_addr,      // Target node address
    char                    a_ch_id,     // Channel ID
    uint8_t                 a_type,      // Packet type
    const void              *a_data,     // Data
    size_t                  a_data_size  // Data size
);
```

Performs a stream lookup by node address via `dap_stream_find_by_addr()`, then calls `dap_stream_ch_pkt_send_mt()`.

### Inter-Worker Write: dap_stream_ch_pkt_write_inter()

```c
size_t dap_stream_ch_pkt_write_inter(
    dap_context_queue_t     *a_queue_input,  // Target queue
    dap_stream_ch_uuid_t    a_ch_uuid,       // Channel UUID
    uint8_t                 a_type,          // Packet type
    const void              *a_data,         // Data
    size_t                  a_data_size      // Data size
);
```

Direct enqueue to a specified context queue. Used for inter-worker routing.

## Channel Packet Fragmentation

When channel data exceeds the transport MTU, automatic fragmentation is performed.

### Algorithm

```
1. Determine target size (l_target_size):
   - If transport supports get_max_packet_size() -> use its value
   - Otherwise -> DAP_STREAM_PKT_FRAGMENT_SIZE
2. Compute maximum fragment size:
   l_max_fragm_size = l_target_size
                    - DAP_STREAM_PKT_ENCRYPTION_OVERHEAD
                    - sizeof(dap_stream_fragment_pkt_t)
3. If l_data_size <= l_max_fragm_size:
   - Packet fits in one piece -> STREAM_PKT_TYPE_DATA_PACKET
4. If l_data_size > l_max_fragm_size:
   - Slice into fragments with size, mem_shift, full_size fields
   - First fragment includes channel header (dap_stream_ch_pkt_hdr_t)
   - Subsequent fragments contain raw data slices
   - Each fragment sent as STREAM_PKT_TYPE_FRAGMENT_PACKET
```

### Statistics

After sending, `ch->stat.bytes_write += a_data_size` is updated (excluding headers).

## Notifier System

Notifiers allow external modules to receive notifications about incoming/outgoing packets on specific channels.

### Notifier Structure

```c
typedef struct dap_stream_ch_notifier {
    dap_stream_ch_notify_callback_t callback;  // Callback function
    void                            *arg;      // User data
} dap_stream_ch_notifier_t;
```

### Callback Signature

```c
typedef void (*dap_stream_ch_notify_callback_t)(
    dap_stream_ch_t *a_ch,        // Channel
    uint8_t          a_type,      // Packet type
    const void       *a_data,     // Packet data
    size_t           a_data_size, // Data size
    void             *a_arg       // User data
);
```

### Registration

```c
int dap_stream_ch_add_notifier(
    dap_stream_node_addr_t           *a_stream_addr,  // Node address
    uint8_t                          a_ch_id,         // Channel ID (character)
    dap_stream_packet_direction_t    a_direction,     // DAP_STREAM_PKT_DIR_IN / DAP_STREAM_PKT_DIR_OUT
    dap_stream_ch_notify_callback_t  a_callback,      // Callback
    void                             *a_callback_arg  // Callback argument
);
```

### Removal

```c
int dap_stream_ch_del_notifier(
    dap_stream_node_addr_t           *a_stream_addr,
    uint8_t                          a_ch_id,
    dap_stream_packet_direction_t    a_direction,
    dap_stream_ch_notify_callback_t  a_callback,
    void                             *a_callback_arg
);
```

### Directions

```c
typedef enum dap_stream_packet_direction {
    DAP_STREAM_PKT_DIR_IN,   // Incoming packets
    DAP_STREAM_PKT_DIR_OUT   // Outgoing packets
} dap_stream_packet_direction_t;
```

### How It Works

1. Notifiers are stored in two linked lists (`dap_list_t`) per channel: `packet_in_notifiers` and `packet_out_notifiers`
2. Registration/removal is performed asynchronously via `dap_worker_exec_callback_on()` on the target worker
3. Duplicates are prevented by `dap_list_find()` with the `s_notifiers_compare()` comparison function (compares by `callback` + `arg`)
4. On packet send (`dap_stream_ch_pkt_write_unsafe`), `packet_out_notifiers` is iterated; on receive -- `packet_in_notifiers`

### Use-After-Free Protection

Notifier iteration checks the `closing` flag:

```c
for (dap_list_t *it = a_ch->packet_out_notifiers; !a_ch->closing && it; it = it->next) {
    dap_stream_ch_notifier_t *l_notifier = it->data;
    l_notifier->callback(a_ch, a_type, a_data, a_data_size, l_notifier->arg);
}
```

If the channel is marked as `closing`, iteration is aborted. Deferred freeing via `dap_worker_exec_callback_on()` guarantees that channel memory is not released until the current iteration completes.

## Utility Functions

### Find Channel by UUID

```c
dap_stream_ch_t *dap_stream_ch_find_by_uuid_unsafe(
    dap_stream_worker_t  *a_worker, // Worker (UNSAFE: worker context only)
    dap_stream_ch_uuid_t a_uuid     // Channel UUID
);
```

Searches the worker's hash table with a read lock on `channels_rwlock`.

### Find Channel by ID

```c
dap_stream_ch_t *dap_stream_ch_by_id_unsafe(
    dap_stream_t *a_stream, // Stream
    const char    a_ch_id   // Character ID (e.g. 'S')
);
```

Linear search through the stream's channel array. Compares `channel[i]->proc->id`.

### UUID Check (MT-safe)

```c
DAP_STATIC_INLINE bool dap_stream_ch_check_uuid_mt(
    dap_stream_worker_t  *a_worker,  // Worker
    dap_stream_ch_uuid_t a_ch_uuid   // Channel UUID
);
```

Returns `true` if a channel with the given UUID exists in the specified worker.

### Get Worker by Channel UUID

```c
dap_worker_t *dap_stream_ch_get_worker_mt(dap_stream_ch_uuid_t a_ch_uuid);
```

Finds the worker owning the channel with the given UUID. Iterates through all workers with proper `channels_rwlock` locking. Fully thread-safe (MT-safe).

### Readiness Control

```c
void dap_stream_ch_set_ready_to_read_unsafe(dap_stream_ch_t *a_ch, bool a_is_ready);
void dap_stream_ch_set_ready_to_write_unsafe(dap_stream_ch_t *a_ch, bool a_is_ready);
```

Set the `ready_to_read`/`ready_to_write` flags and synchronize state with the esocket via `dap_events_socket_set_readable_unsafe()` / `dap_events_socket_set_writable_unsafe()`.

## Channel Cachet

```c
typedef struct dap_stream_ch_cachet {
    dap_stream_worker_t  *stream_worker; // Worker
    dap_stream_ch_uuid_t uuid;          // Channel UUID
} dap_stream_ch_cachet_t;
```

A lightweight structure for caching a channel reference (worker + UUID) without holding a full pointer to `dap_stream_ch_t`.

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `TECHICAL_CHANNEL_ID` | `'t'` | Technical (service) channel ID |
| `STREAM_CH_PKT_TYPE_REQUEST` | `0x0` | Default packet type "request" |
| `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD` | 200 bytes | Encryption overhead (from stream_pkt) |
| `DAP_STREAM_PKT_FRAGMENT_SIZE` | (from stream_pkt) | Maximum packet size |

## Cross-References

- [01 -- Stream Protocol](01-stream-protocol_en.md) -- stream protocol core
- [03 -- DSHP Handshake](03-handshake_en.md) -- connection establishment
- [01 -- Transport Abstraction](../transport/02-transport-abstraction_en.md) -- transport layer
- Header files: `ch/include/dap_stream_ch.h`, `dap_stream_ch_proc.h`, `dap_stream_ch_pkt.h`
