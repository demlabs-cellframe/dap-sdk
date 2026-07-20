# DAP SDK -- Transport & Protocol Overview

> **Version:** 1.0 | **Date:** 2026-07-20 | **Scope:** DAP SDK Transport & Protocol

## 1. Layered Architecture

Full stack diagram of the DAP SDK networking stack from hardware level to applications:

```
┌──────────────────────────────────────────────────────────────────┐
│ L4: Channels & Applications                                      │
│     dap_stream_ch_t -- VPN service, GlobalDB, Chain, custom      │
├──────────────────────────────────────────────────────────────────┤
│ L3: DAP Stream Protocol                                          │
│     dap_stream_t, dap_stream_pkt_t, fragmentation, sessions      │
├──────────────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction                                        │
│     dap_net_trans_t, vtable (17 operations), obfuscation hook    │
├──────────────────────────────────────────────────────────────────┤
│ L1: Concrete Transports                                          │
│     TLS mimicry | UDP basic/reliable | HTTP | DNS tunnel | WS    │
├──────────────────────────────────────────────────────────────────┤
│ L0: IO Layer                                                     │
│     dap_events_socket_t, dap_worker_t, epoll/kqueue/IOCP         │
└──────────────────────────────────────────────────────────────────┘
```

### L0: IO Layer

The foundation of the stack. Provides cross-platform event-driven I/O based on a worker thread pool. Each socket is bound to one worker -- no contention between threads. The platform mechanism is selected automatically: epoll (Linux), kqueue (macOS/BSD), IOCP (Windows), poll (fallback).

**Details:** [transport/01-io-layer](transport/01-io-layer_en.md)

### L1: Concrete Transports

Six concrete transport implementations, each implementing the `dap_net_trans_ops_t` vtable:

| Transport | Purpose |
|-----------|---------|
| TLS Mimicry | DPI bypass via TLS 1.3 handshake simulation |
| UDP Basic | Basic UDP without delivery guarantees |
| UDP Reliable | UDP with ARQ for guaranteed delivery |
| HTTP | Tunneling over HTTP connections |
| DNS Tunnel | Tunneling through DNS queries |
| WebSocket | Transport over WebSocket |

**Details:** [transport/03-transports](transport/03-transports_en.md)

### L2: Transport Abstraction

A unified interface `dap_net_trans_t` with a vtable of 17 operations. DAP Stream Protocol does not know which transport is used -- it only works with the abstraction. Each transport declares capabilities via a `DAP_NET_TRANS_CAP_*` bitmask. Contains an obfuscation hook for packet processing before sending to the network.

**Details:** [transport/02-transport-abstraction](transport/02-transport-abstraction_en.md)

### L3: DAP Stream Protocol

A binary stream protocol with fragmentation, channel multiplexing, encryption, and keepalive. Each stream (`dap_stream_t`) is associated with a session and can contain up to 256 channels. Packets are split into fragments respecting the transport's MTU.

**Details:** [protocol/01-stream-protocol](protocol/01-stream-protocol_en.md)

### L4: Channels & Applications

Channels (`dap_stream_ch_t`) are logical streams within a single DAP Stream. Each channel has a type and its own handler. Examples: VPN service, GlobalDB synchronization, blockchain chain.

---

## 2. How It All Works Together

End-to-end data flow from client connection to data transfer:

```
Client                                          Server
  │                                                │
  │  1. Select transport type (e.g. TLS_DIRECT)    │
  │                                                │
  │  2. IO layer establishes TCP connection         │
  │  ─────────────────────────────────────────────→ │
  │                                                │
  │  3. Transport creates esocket and stream        │
  │                                                │
  │  4. DSHP handshake: key exchange                │
  │     (post-quantum KEM: Kyber / Falcon)          │
  │  ←───────────────────────────────────────────→  │
  │                                                │
  │  5. Session created with channel list           │
  │                                                │
  │  6. Data flow:                                  │
  │     app → channel_pkt → stream_pkt              │
  │         → fragment → transport → network        │
  │  ─────────────────────────────────────────────→ │
  │                                                │
  │  7. Receive path:                               │
  │     network → transport → defragment            │
  │         → decrypt → channel dispatch            │
  │  ←───────────────────────────────────────────── │
```

**Step by step:**

1. **Transport selection.** The client selects a transport type (`DAP_NET_TRANS_TLS_DIRECT`, `DAP_NET_TRANS_UDP_BASIC`, etc.) based on configuration and DPI resistance requirements.
2. **Connection establishment.** The IO layer performs a TCP connection (or UDP binding) through the platform event loop.
3. **Transport context creation.** The concrete transport creates a `dap_events_socket_t` and an associated `dap_stream_t`.
4. **DSHP Handshake.** Three-phase key exchange: the client sends a public key (Kyber KEM), the server responds with its own. `session_key` and `stream_key` are derived from the shared secret via KDF.
5. **Session creation.** The server creates a `dap_stream_session_t` with the requested channel list.
6. **Data transfer.** The application writes data to a channel. The channel forms a `dap_stream_ch_pkt_t`, which is wrapped in a `dap_stream_pkt_t`, split into fragments, and sent through the transport.
7. **Data reception.** The transport reassembles fragments, decrypts the packet, and dispatches data by channel ID.

**Details on client stages:** [transport/05-client-transport](transport/05-client-transport_en.md)

---

## 3. Key Design Decisions

### Transport-agnostic stream

DAP Stream does not know which transport is used. It interacts with `dap_net_trans_t` through a vtable of 17 operations. This allows adding new transports without modifying the protocol layer.

### Three-level keys

Three levels of cryptography serve different purposes and are not interchangeable:

| Level | Purpose | Algorithms |
|-------|---------|------------|
| Obfuscation (anti-DPI) | Mask packet structure from DPI | SALSA2012 + KDF |
| Handshake (key exchange) | Establish shared secret | Kyber (KEM) + Falcon/ECDSA (signatures) |
| Stream (data protection) | Encrypt transmitted data | ChaCha20, AES-GCM |

Obfuscation is **not** encryption. It hides packet structure but does not provide confidentiality. Cryptographic protection is provided by the upper layers.

**Details:** [transport/04-obfuscation](transport/04-obfuscation_en.md), [protocol/04-encryption](protocol/04-encryption_en.md)

### Worker-per-socket

Each socket is bound to exactly one worker. No shared state between threads -- no contention, no locks. Each worker's event loop only handles its own sockets.

### Sticky FSM binding

The client FSM always runs on the same thread. The thread is selected by the formula `uuid % pool_size`, ensuring cache locality and no races.

### Deferred free

Channel deletion is deferred via a worker callback. This prevents use-after-free: when one thread decides to delete a channel, the actual memory deallocation happens in the context of the worker that owns the socket.

### Fragment-first design

Fragmentation is built into the architecture from the start, not bolted on later. Each transport declares its MTU, and the stream protocol automatically splits packets into appropriately sized fragments.

### TLV-based handshake

DSHP uses TLV encoding (Type-Length-Value). Unknown types are skipped by their length field, providing forward compatibility: older clients correctly process newer fields by simply ignoring them.

**Details:** [protocol/03-handshake](protocol/03-handshake_en.md)

---

## 4. Comparison with Other Protocols

| Aspect | DAP Stream | QUIC | WireGuard | OpenVPN |
|--------|-----------|------|-----------|---------|
| Transport | Pluggable (7 enum values, 6 implemented) | UDP only | UDP only | TCP/UDP |
| Handshake | DSHP (custom TLV) | TLS 1.3 | Noise IK | TLS |
| Encryption | Post-quantum (Kyber) | AES-GCM / ChaCha20 | ChaCha20-Poly1305 | OpenSSL |
| Multiplexing | Channels in stream | Native streams | Single tunnel | Single tunnel |
| Obfuscation | Built-in (SALSA2012) | None | None | None |
| DPI resistance | TLS mimicry + obfuscation | QUIC bit | None | Stunnel |
| Fragmentation | Application-level | QUIC frames | IP-level | TCP MSS |
| Post-quantum | Yes (Kyber, Falcon) | No | No | No |

**DAP Stream advantages:**
- **Transport flexibility.** Six implemented transports (7 enum values) allow adapting to network constraints (blocked UDP? Use DNS tunnel).
- **Built-in obfuscation.** No external tools (stunnel, obfs4) needed for DPI bypass.
- **Post-quantum.** Resistance to quantum computer attacks at the handshake stage.
- **Channel multiplexing.** One stream, multiple logical channels -- no need to establish multiple connections.

---

## 5. Glossary

| Term | Definition |
|------|-----------|
| **DSHP** | DAP Stream Handshake Protocol -- a binary handshake protocol with TLV encoding, transport-agnostic. |
| **KEM** | Key Encapsulation Mechanism (Kyber). Falcon is a signature algorithm, not KEM. Allows two parties to derive a shared secret without transmitting the secret over the channel. |
| **TLV** | Type-Length-Value -- a field encoding format where each record consists of a type (2 bytes), length (2 bytes), and value (variable length). Provides forward compatibility. |
| **MTU** | Maximum Transmission Unit -- the maximum packet size that can be transmitted over a transport without fragmentation. Each transport declares its own MTU. |
| **DPI** | Deep Packet Inspection -- a technology for deep packet inspection used to analyze and block traffic. DAP SDK counters DPI through obfuscation and mimicry. |
| **Obfuscation** | Masking the structure of network packets from DPI. It is not encryption -- it hides the form but not the content. Implemented at the transport level (SALSA2012 + KDF). |
| **Mimicry** | A technique of imitating a legitimate protocol. TLS mimicry generates a realistic TLS 1.3 handshake on the wire that DPI systems identify as a normal TLS connection. |
| **Flow** | A continuous sequence of data between two nodes. In the DAP SDK context -- a single `dap_stream_t`. |
| **Channel** | A logical stream within a DAP Stream (`dap_stream_ch_t`). Each channel has a type and its own handler. Up to 256 channels per stream. |
| **Stream** | A binary stream protocol (`dap_stream_t`) providing fragmentation, encryption, and channel multiplexing. |
| **Session** | A session (`dap_stream_session_t`) -- the state of a connection between client and server, including keys, channel list, and parameters. |
| **Worker** | A worker thread (`dap_worker_t`) -- one thread from the pool, handling its own subset of sockets. The worker-to-socket relationship is 1:1. |
| **Esocket** | Event socket (`dap_events_socket_t`) -- a socket abstraction integrated with the event loop. Contains callbacks for read, write, and error events. |
| **FSM** | Finite State Machine -- the state machine governing client connection stages (BEGIN -> ENC_INIT -> STREAM_CTL -> STREAM_SESSION -> STREAM_CONNECTED -> STREAM_STREAMING). |
| **ARQ** | Automatic Repeat reQuest -- an automatic retransmission mechanism for guaranteed delivery. Used in the UDP Reliable transport. |

---

## 6. Document Index

Complete documentation set for DAP SDK transport and protocol:

### Transport (transport/)

| Document | Description |
|----------|-------------|
| [01-io-layer](transport/01-io-layer_en.md) | IO foundation -- event loop, workers, sockets, cross-platform support |
| [02-transport-abstraction](transport/02-transport-abstraction_en.md) | Transport abstraction layer -- vtable, registration, capabilities |
| [03-transports](transport/03-transports_en.md) | Concrete transport implementations -- TLS mimicry, UDP, HTTP, DNS, WebSocket |
| [04-obfuscation](transport/04-obfuscation_en.md) | Obfuscation and DPI bypass -- SALSA2012, mimicry, security model |
| [05-client-transport](transport/05-client-transport_en.md) | Client transport -- FSM, connection stages, reconnection |

### Protocol (protocol/)

| Document | Description |
|----------|-------------|
| [01-stream-protocol](protocol/01-stream-protocol_en.md) | DAP Stream -- packet structure, fragmentation, sessions |
| [02-channels](protocol/02-channels_en.md) | Channel multiplexing -- types, handlers, lifecycle |
| [03-handshake](protocol/03-handshake_en.md) | DSHP -- handshake protocol, TLV format, key exchange |
| [04-encryption](protocol/04-encryption_en.md) | Cryptographic model -- three key levels, KEM, KDF |
| [05-protocol-diagrams](protocol/05-protocol-diagrams_en.md) | Visual diagrams -- sequence diagrams, state machines |

### Overview

| Document | Description |
|----------|-------------|
| **This document** | Architecture overview, glossary, index |
