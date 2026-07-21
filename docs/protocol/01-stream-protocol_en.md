# DAP Stream Protocol -- Stream Protocol Core

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/stream/stream/`, `session/`

## Overview

DAP Stream Protocol is the third level (L3) of the DAP SDK network stack. It provides a binary stream protocol with fragmentation, channel multiplexing, encryption, and keepalive. It operates over any transport (L2) through the Transport Abstraction Layer.

## Architecture

```
+--------------------------------------------------------------+
| L4: Channels (dap_stream_ch_t) -- VPN, GlobalDB, Chain       |
+--------------------------------------------------------------+
| L3: DAP Stream Protocol <- THIS DOCUMENT                     |
|     dap_stream_t -> dap_stream_pkt_t -> dap_stream_ch_pkt_t  |
+--------------------------------------------------------------+
| L2: Transport Abstraction (dap_net_trans_t)                  |
+--------------------------------------------------------------+
| L1: TLS / UDP / HTTP / DNS / WebSocket                       |
+--------------------------------------------------------------+
| L0: IO (dap_events_socket_t)                                 |
+--------------------------------------------------------------+
```

## Core Structure: dap_stream_t

`dap_stream_t` represents a single binary stream between two nodes:

```c
typedef struct dap_stream {
    dap_stream_node_addr_t  node;              // Node address
    bool                    authorized;        // Authorization flag
    bool                    primary;           // Primary stream flag
    int                     id;                // Stream ID

    dap_stream_session_t    *session;          // Session
    dap_stream_worker_t     *stream_worker;    // Worker

    // Channels
    dap_stream_ch_t         **channel;         // Channel array
    size_t                  channel_count;

    // Sequencing
    size_t                  seq_id;            // Current sequence ID
    size_t                  stream_size;       // Stream size
    size_t                  client_last_seq_id_packet;

    // Fragmentation
    uint8_t                 *buf_fragments;    // Reassembly buffer
    size_t                  buf_fragments_size_total;
    size_t                  buf_fragments_size_filled;
    uint8_t                 *pkt_cache;        // Packet cache

    // Keepalive
    dap_timerfd_t           *keepalive_timer;
    uint64_t                keepalive_timer_uuid;
    struct dap_worker       *keepalive_timer_worker; // Timer's worker

    // Transport
    struct dap_net_trans    *trans;            // Shared transport (not owned)
    dap_net_trans_ctx_t     *trans_ctx;        // Transport context
    void                    *flow;             // Datagram flow (for UDP/SCTP)

    // Bindings
    dap_events_socket_t     *esocket;          // UNSAFE: worker-context only
    dap_events_socket_uuid_t esocket_uuid;     // SAFE: cross-thread reference
    dap_worker_t            *esocket_worker;

    // State
    bool                    is_active;
    bool                    is_deleting;       // Double-free guard
    bool                    is_client_to_uplink;
    char                    *service_key;      // Authorization key

    // Server/client references
    void                    *_server_session;  // Server-side session (NULL on client)
    dap_stream_t            **client_stream_ref; // Client-side ref (NULL on server)

    UT_hash_handle          hh;               // Hash table by address
    struct dap_stream       *prev, *next;      // Linked list
} dap_stream_t;
```

## Stream Packet (dap_stream_pkt_t)

### Packet Header

```c
typedef struct dap_stream_pkt_hdr {
    uint8_t     sig[8];       // Signature for packet boundary detection
    uint32_t    size;         // Data size
    uint64_t    timestamp;    // Timestamp
    uint8_t     type;         // Packet type
    uint64_t    src_addr;     // Source address
    uint64_t    dst_addr;     // Destination address
} __attribute__((packed)) dap_stream_pkt_hdr_t;  // 37 bytes
```

### Packet Signature

An 8-byte signature is used for detecting packet boundaries within a data stream:

```c
static const uint8_t c_dap_stream_sig[8] = {
    0xa0, 0x95, 0x96, 0xa9, 0x9e, 0x5c, 0xfb, 0xfa
};
```

Detection algorithm:
1. Search for the first signature byte using `memchr`
2. Verify the full 8-byte signature
3. Validate the packet size against `DAP_STREAM_PKT_SIZE_MAX`

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| `STREAM_PKT_TYPE_DATA_PACKET` | 0x00 | Data packet (complete or last fragment) |
| `STREAM_PKT_TYPE_FRAGMENT_PACKET` | 0x01 | Fragment of a large packet |
| `STREAM_PKT_TYPE_SERVICE_PACKET` | 0xFF | Service packet (session check) |
| `STREAM_PKT_TYPE_KEEPALIVE` | 0x11 | Keepalive request |
| `STREAM_PKT_TYPE_ALIVE` | 0x12 | Keepalive response |

### Full Packet

```c
typedef struct dap_stream_pkt {
    dap_stream_pkt_hdr_t    hdr;
    uint8_t                 data[];    // Flexible array member
} __attribute__((packed)) dap_stream_pkt_t;
```

### Wire Format -- Packet Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        sig[0..7] (8 bytes)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           sig cont.           |         size (4 bytes)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       timestamp (8 bytes)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  timestamp cont.|  type (1)   |       src_addr (8 bytes)      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       src_addr cont.                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       dst_addr (8 bytes)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       dst_addr cont.                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       data[0..size-1]                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Total header: 37 bytes  |  Overhead: 37 + up to 200 bytes encryption
```

**Overhead:** 37-byte header + up to 200 bytes of encryption overhead (`DAP_STREAM_PKT_ENCRYPTION_OVERHEAD`).

## Fragmentation

For transmitting large packets over transports with limited MTU (UDP=1200, DNS=500) the protocol uses fragmentation:

```c
typedef struct dap_stream_fragment_pkt {
    uint32_t    size;         // Size of this fragment
    uint32_t    mem_shift;    // Offset within the original packet
    uint32_t    full_size;    // Full size of the original packet
    uint8_t     data[];       // Fragment payload
} __attribute__((packed)) dap_stream_fragment_pkt_t;  // 12-byte header
```

### Wire Format -- Fragment Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              size (4)         |         mem_shift (4)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            full_size (4)      |        data[0..size-1]        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Header: 12 bytes  |  data[] extends for 'size' bytes
```

### Fragmentation Algorithm

1. Query the transport MTU: `trans->ops->get_max_packet_size()` (UDP=1200, DNS=500, TCP=0)
2. Compute maximum fragment size: `mtu - DAP_STREAM_PKT_ENCRYPTION_OVERHEAD - sizeof(dap_stream_fragment_pkt_t)`
3. If the data fits in a single fragment -- send without fragmentation
4. Otherwise -- split into fragments, filling in `size`, `mem_shift`, and `full_size`
5. The first fragment includes the channel header (`dap_stream_ch_pkt_hdr_t`); subsequent fragments carry raw data only

### Reassembly Algorithm

1. On receiving `STREAM_PKT_TYPE_FRAGMENT_PACKET`:
2. Verify `buf_fragments_size_filled == mem_shift` (ordering check)
3. Copy the fragment payload into `buf_fragments` at the correct offset
4. If `buf_fragments_size_filled == full_size` -- all fragments received
5. Process the reassembled buffer as `STREAM_PKT_TYPE_DATA_PACKET`

## Stream Session (dap_stream_session_t)

A session represents an active streaming connection:

```c
typedef struct dap_stream_session {
    bool                    create_empty;      // Create empty session
    uint32_t                id;                // Session ID
    uint32_t                media_id;          // Media ID
    dap_enc_key_t           *key;              // Encryption key
    bool                    open_preview;      // Open preview
    pthread_mutex_t         mutex;
    int                     opened;
    dap_time_t              time_created;
    uint8_t                 enc_type;          // Encryption type
    int32_t                 protocol_version;  // Protocol version
    char                    *service_key;      // Authorization key
    char                    active_channels[16]; // Active channels (string, e.g. "C,F,N")
    stream_session_connection_type_t conn_type; // Connection type (HTTP/UDP)
    stream_session_type_t   type;              // Session type (MEDIA/VPN)
    uint8_t                 *acl;              // Access Control List
    dap_stream_node_addr_t  node;              // Node address
    UT_hash_handle          hh;               // Hash table by ID
    struct in_addr          tun_client_addr;   // Client TUN address
    void                    *_inheritor;       // Extension point
    dap_stream_session_callback_t callback_delete; // Delete callback
} dap_stream_session_t;
```

### Session Types

```c
typedef enum stream_session_type {
    STREAM_SESSION_TYPE_MEDIA = 0,  // Media stream
    STREAM_SESSION_TYPE_VPN,        // VPN connection
} stream_session_type_t;

typedef enum stream_session_connection_type {
    STEAM_SESSION_HTTP = 0,         // HTTP transport
    STREAM_SESSION_UDP,             // UDP transport
    STREAM_SESSION_END_TYPE,
} stream_session_connection_type_t;
```

## Keepalive

Connection maintenance through periodic keepalive messages:

- **Interval:** 3 seconds (`STREAM_KEEPALIVE_TIMEOUT`)
- **Mechanism:** Timer (`dap_timerfd_t`) with UUID for safe cross-thread access
- **Packets:** `STREAM_PKT_TYPE_KEEPALIVE` (0x11) is the request, `STREAM_PKT_TYPE_ALIVE` (0x12) is the response
- **Safety:** UUID-based timer prevents use-after-free when the stream is destroyed

## Service Packet

A service packet used for session verification:

```c
typedef struct dap_stream_srv_pkt {
    uint32_t    session_id;   // Session ID
    uint8_t     enc_type;     // Encryption type
    uint32_t    coockie;      // Cookie (authorization)
} __attribute__((packed)) dap_stream_srv_pkt_t;
```

## Data Path (Read)

```
1. Socket receives data -> buf_in
2. dap_stream_data_proc_read_ext() scans for sig[8] in the stream
3. On finding a complete packet -> s_stream_proc_pkt_in()
4. Packet type determines processing:
   a. FRAGMENT  -> reassemble into buf_fragments
   b. DATA      -> decrypt -> extract dap_stream_ch_pkt_t -> dispatch to channel
   c. SERVICE   -> session verification
5. Packet is passed to channel->proc->packet_in_callback()
6. Iterate packet_in_notifiers for notifications
```

## Data Path (Write)

```
1. Upper layer calls dap_stream_ch_pkt_write_*()
2. Channel header (dap_stream_ch_pkt_hdr_t) is formed
3. If data exceeds MTU -> fragmentation
4. Each fragment/packet is encrypted via dap_stream_pkt_write_unsafe()
5. Send path selection:
   a. trans->ops->write()             (if transport is present)
   b. stream->trans->ops->write()     (client-side path)
   c. Direct esocket write            (legacy)
6. For datagrams: s_stream_send_datagram_unsafe() via dap_io_flow
```

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `STREAM_KEEPALIVE_TIMEOUT` | 3 sec | Keepalive interval |
| `STREAM_PKT_SIG_SIZE` | 8 bytes | Signature size |
| `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD` | 200 bytes | Encryption overhead |
| `STREAM_PKT_TYPE_DATA_PACKET` | 0x00 | Data packet |
| `STREAM_PKT_TYPE_FRAGMENT_PACKET` | 0x01 | Fragment |
| `STREAM_PKT_TYPE_SERVICE_PACKET` | 0xFF | Service packet |
| `STREAM_PKT_TYPE_KEEPALIVE` | 0x11 | Keepalive request |
| `STREAM_PKT_TYPE_ALIVE` | 0x12 | Keepalive response |

## Related Documents

- [02 -- Channels](02-channels_en.md) -- channel multiplexing
- [03 -- DSHP Handshake](03-handshake_en.md) -- connection setup
- [04 -- Encryption](04-encryption_en.md) -- cryptographic model
- [01 -- IO Layer](../transport/01-io-layer_en.md) -- underlying transport layer
- Header files: `stream/include/dap_stream.h`, `dap_stream_pkt.h`, `session/include/dap_stream_session.h`
