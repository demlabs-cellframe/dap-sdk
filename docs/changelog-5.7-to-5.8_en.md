# DAP SDK Changes v5.7 → v5.8

> **Version:** 1.0 | **Date:** 2026-07-20 | **Scope:** io/, net/, crypto/

## Overview

The v5.7 to v5.8 transition is the **largest architectural overhaul** in DAP SDK history. 675 files changed, 108,071 lines added, 24,767 removed. Three major directions:

1. **Transport Abstraction Layer** — complete decoupling from HTTP
2. **Lock-free reactor** — replacing proc-thread queue and inter-context queues with lock-free ring buffers
3. **Client FSM rewrite** — splitting the monolith into FSM + trans_ctx

## 1. Transport Abstraction Layer (entirely new subsystem)

### New directory: `net/trans/`

A completely new transport-level abstraction. DAP Stream no longer knows which transport is used — it works with a vtable of 17 operations.

**Key files (all new):**

| File | Purpose |
|------|---------|
| `dap_net_trans.h` | Central abstraction: `dap_net_trans_type_t` (7 types), `dap_net_trans_ops` vtable, transport registry |
| `dap_net_trans_ctx.h` | Per-connection context: owns `dap_stream_t`, holds encryption keys |
| `dap_net_trans_server.h` | Unified server API with vtable |
| `dap_net_trans_qos.h` | QoS probe/echo protocol for latency measurement |
| `dap_transport_obfuscation.h` | Packet-level obfuscation: KDF-SHAKE256 + SALSA2012 |

**Transport implementations (all new):**

| Transport | Directory | Lines | Description |
|-----------|-----------|-------|-------------|
| HTTP | `net/trans/http/` | ~2,737 | Backward-compatible wrapper over legacy HTTP |
| UDP | `net/trans/udp/` | ~6,081 | Datagram with Flow Control, session routing by (addr, port) |
| WebSocket | `net/trans/websocket/` | ~3,316 | RFC 6455 for DPI bypass, HTTP upgrade |
| TLS Direct | `net/trans/tls/` | ~1,077 | Direct TLS with mimicry support |
| DNS Tunnel | `net/trans/dns/` | ~1,690 | Tunneling through DNS queries (port 53) |

**Handshake obfuscation algorithm:**
1. Random padding to size in [850, 1350] bytes
2. KDF-SHAKE256(seed, packet_size) → 40 bytes [nonce(8) + key(32)]
3. SALSA2012 encryption
4. Result: variable-size blob, indistinguishable from random data

## 2. Lock-free Reactor

### Queue replacement

All inter-context worker queues replaced from pipe-based to lock-free ring buffers:

| Was (5.7) | Now (5.8) |
|-----------|-----------|
| `dap_events_socket_t*` queues | `dap_context_queue_t*` (lock-free ring buffer) |
| `DESCRIPTOR_TYPE_QUEUE` | Removed entirely |
| `epoll_wait(-1)` (block forever) | `epoll_wait(10000)` (heartbeat every 10s) |
| Mutex + condvar in proc thread | Fully lock-free |

### New IO components

| File | Lines | Purpose |
|------|-------|---------|
| `dap_context_queue.c` | 230 | Lock-free ring buffer for inter-context communication |
| `dap_io_flow.c` | 1,414 | Universal IO Flow API for datagram protocols |
| `dap_io_flow_ctrl.c` | 1,103 | Flow Control: sequence numbers, ACKs, RTT, retransmission |
| `dap_io_flow_datagram.c` | 499 | Datagram flow with remote address resolution |
| `dap_io_flow_socket.c` | 976 | Socket-level flow management |
| `dap_thread.c` | 224 | New thread abstraction |
| `dap_thread_pool.c` | 410 | Thread pool implementation |

### Platform-specific IO Flow extensions

| Platform | File | Technology |
|----------|------|-----------|
| Linux | `dap_io_flow_cbpf.c` | Classic BPF filtering |
| Linux | `dap_io_flow_ebpf.c` | eBPF filtering and routing |
| BSD | `dap_io_flow_bsd_lb.c` | Load balancing |
| macOS | `dap_io_flow_darwin_gcd.c` | Grand Central Dispatch |
| Windows | `dap_io_flow_win_rio.c` | Registered I/O |

### Datagram Packet Queue

New `dap_events_socket_packet_queue_t` — ring buffer for datagrams:
- Initial capacity: 16 packets
- Maximum: 4,096 packets
- Automatic growth (doubling)
- `dap_events_socket_sendto_unsafe()` for UDP/SCTP sendto

## 3. Client FSM Rewrite

### Removed

| File | Lines | Reason |
|------|-------|--------|
| `dap_client_pvt.c` | 1,343 | Monolithic state machine on arbitrary threads |
| `dap_client_pvt.h` | 109 | Old private header |

### Added

| File | Lines | Purpose |
|------|-------|---------|
| `dap_client_fsm.c` | 1,606 | FSM on dedicated thread pool (sticky binding: uuid % pool_size) |
| `dap_client_fsm.h` | 182 | FSM header |
| `dap_client_trans_ctx.c` | 781 | Minimal IO context (uuid, client pointer, fsm_uuid) |
| `dap_client_trans_ctx.h` | 70 | trans_ctx header |
| `dap_client_helpers.c` | 221 | Helper utilities |

### Architectural separation

```
Was (5.7):                           Now (5.8):
┌──────────────────────┐      ┌──────────────────────────────┐
│ dap_client_t         │      │ dap_client_t (public API)    │
│   └─ dap_client_pvt  │      │   └─ dap_client_fsm_t        │
│      (monolith,      │      │      (FSM + crypto,           │
│       any thread)    │      │       dedicated FSM thread)   │
│                      │      │   └─ dap_net_trans_ctx_t      │
│                      │      │      (keys, stream ownership) │
│                      │      │   └─ dap_client_trans_ctx_t   │
│                      │      │      (IO identity)            │
└──────────────────────┘      └──────────────────────────────┘
```

**Key improvements:**
- Heavy cryptography on FSM thread, not on IO worker
- Transport fallback: `tried_transports[]` tracks attempts
- Session resume mode: hot reconnect without full handshake
- Legacy enc_init fallback for P2P connections
- Atomic cross-thread readable stage/status copies

## 4. Stream Protocol Changes

### `dap_stream_t` field changes

| Field | Status | Description |
|-------|--------|-------------|
| `trans` | **Added** | Transport pointer (abstraction) |
| `trans_ctx` | **Added** | Back-reference to transport context |
| `flow` | **Added** | Datagram flow for UDP/SCTP |
| `_server_session` | **Added** | Server-side session (NULL on client) |
| `client_stream_ref` | **Added** | Dangling pointer protection |
| `esocket_worker` | **Added** | Esocket's worker |
| `conn_http` | **Removed** | Replaced by `trans` |

### New Stream components

| File | Lines | Purpose |
|------|-------|---------|
| `dap_stream_handshake.c` | 1,017 | DSHP v1.0 — binary TLV handshake (replaces HTTP-based) |
| `dap_stream_obfuscation.c` | 631 | Obfuscation engine: padding, timing, mimicry, polymorphism |
| `dap_stream_obfuscation_mimicry.c` | 809 | Mimicry: HTTPS (TLS record), HTTP/2 (binary framing), WebSocket |

### DSHP v1.0 (DAP Stream Handshake Protocol)

New binary protocol replaces HTTP-based handshake:

| Parameter | Legacy HTTP | DSHP v1.0 |
|-----------|------------|-----------|
| Format | HTTP POST + JSON | Binary TLV |
| Size | ~100+ bytes | ~64-68 bytes (approximately -34%, estimated) |
| Transport | HTTP only | Any binary |
| Compatibility | Native for HTTP | Requires parser |

**6 message types:** REQUEST → RESPONSE → SESSION_CREATE → SESSION_CREATE_RESPONSE → STREAM_READY → STREAM_START

## 5. Encryption Server Refactor

| Was (5.7) | Now (5.8) |
|-----------|-----------|
| `dap_enc_http.c` — monolithic HTTP handler | Thin HTTP adapter → `dap_enc_server` |
| Logic bound to HTTP | `dap_enc_server_process_request()` — transport-independent |

## 6. Crypto Layer Changes

### New components

| File | Purpose |
|------|---------|
| `dap_enc_kdf.h/.c` | Universal KDF via SHAKE256 with domain separation and ratcheting |
| `crypto/tls/` (5 files) | Cross-platform TLS: OpenSSL, Apple Security, SChannel |
| `dap_uuid.h/.c` | UUID utilities |
| `sha2-256/` | SHA2-256 hash implementation |

### Performance optimizations

| Operation | Was (5.7) | Now (5.8) | Speedup |
|-----------|-----------|-----------|---------|
| Key from raw bytes | ~50 µs (via Keccak) | ~500 ns (`new_from_raw_bytes`) | **100x** |
| Key update | Allocation + copy | Zero-allocation (`update_from_raw_bytes`) | **~200 ns** |
| Salsa2012 key | Random nonce | Deterministic nonce from raw bytes | Deterministic |

### New KEM API

```c
dap_enc_kem_result_t* dap_enc_kem_alice_generate_keypair(dap_enc_key_type_t a_kem_type);
dap_enc_kem_result_t* dap_enc_kem_bob_encapsulate(dap_enc_key_type_t a_kem_type,
                                                   const uint8_t *a_alice_pub, size_t a_alice_pub_size);
int                  dap_enc_kem_alice_decapsulate(dap_enc_kem_result_t *a_result,
                                                   const uint8_t *a_bob_pub, size_t a_bob_pub_size);
```

## 7. Architectural Impact Summary

```
v5.7:                                v5.8:
┌────────────────────┐      ┌────────────────────────────┐
│ DAP Stream         │      │ DAP Stream                 │
│ (HTTP-bound)       │      │ (transport-agnostic)       │
├────────────────────┤      ├────────────────────────────┤
│ HTTP client/server │      │ Transport Abstraction      │
│                    │      │ ┌────┐┌────┐┌────┐┌───┐┌──┐│
│                    │      │ │HTTP││UDP ││TLS ││DNS││WS││
├────────────────────┤      ├────────────────────────────┤
│ Reactor (pipe queue)│     │ Reactor (lock-free)        │
│ Mutex + condvar    │      │ Proc-thread queue lock-free (mutexes remain in other IO paths) │
├────────────────────┤      ├────────────────────────────┤
│ Client Pvt (monolith)│    │ Client FSM + TransCtx      │
│ Any thread         │      │ Dedicated FSM thread       │
└────────────────────┘      └────────────────────────────┘
```

## 8. Key Commits

| Hash | Message |
|------|---------|
| `55ce6523` | perf: fully lock-free reactor — remove mutex+condvar from proc thread |
| `21b9b3c2` | cleanup: remove dead pipe-based queue code |
| `12c487cd` | perf: remove mutex from flow control hot path |
| `f8238174` | perf: lock-free seq increment in flow control send path |
| `157e61e6` | feat: TLS mimicry transport for DPI resistance |
| `e96b486f` | feat(qos): STAGE_QOS_PROBE FSM branch + transport probe/echo protocol |
| `a4cc5808` | feat: transport fallback in client FSM, TLS auto-registration |
| `ca78707b` | refactor: replace dap_client_esocket with dap_client_trans_ctx |
| `e8bf79c8` | feat(crypto): add IAES2 type with strengthened IV derivation (Note: IAES2 type was later removed from release-5.8 HEAD) |
| `16c1167b` | fix: event_exit corruption guard + server refcount + HTTP timer fix |
| `51265d84` | fix: thread safety — remove double-free in TLS close, add ASan cmake support |
| `959cf0a4` | fix: EPOLLOUT busy loop, epoll fd mismatch, CLI timeout, worker queue diagnostics |

## Related Documents

- [Transport Abstraction Layer](transport/02-transport-abstraction_en.md)
- [Concrete Transports](transport/03-transports_en.md)
- [Obfuscation](transport/04-obfuscation_en.md)
- [Client Transport](transport/05-client-transport_en.md)
- [DSHP Handshake](protocol/03-handshake_en.md)
- [IO Layer](transport/01-io-layer_en.md)
