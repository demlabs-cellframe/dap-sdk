# DAP SDK Transport Implementations -- Technical Reference

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/trans/`

## Purpose

This document provides a deep technical reference for all five concrete transport implementations plugged into the DAP SDK Transport Abstraction Layer. Each transport implements the `dap_net_trans_ops_t` vtable and is registered at startup via `dap_net_trans_register()`. The upper layers (DAP Stream, channels) interact exclusively through the abstract interface and never call transport-specific code directly.

Transport selection depends on the deployment environment: network restrictions, DPI presence, latency requirements, and the client platform (native, browser, embedded).

---

## 1. TLS Mimicry Transport

**Source:** `net/trans/tls/`
**Type:** `DAP_NET_TRANS_TLS_DIRECT` (0x06)
**Socket:** TCP

### What It Is (and What It Is Not)

The TLS Mimicry transport is **not** a real TLS implementation. It contains zero cryptographic TLS operations -- no key derivation, no AES-GCM encryption, no certificate verification. Instead, it generates a byte-accurate reproduction of a TLS 1.3 handshake on the wire so that DPI systems (including those using JA3/JA4 fingerprinting) classify the traffic as legitimate HTTPS.

Actual encryption is handled entirely by the DAP Stream layer above, which runs its own DSHP handshake and uses `dap_enc` for symmetric encryption. The mimicry engine acts as a transparent framing layer.

### State Machine

```
                    ClientHello sent
    INIT ──────────────────────────────────> CLIENT_HELLO_SENT
     (0)                                          (1)
                                                     │
                                     ServerHello+CCS+fake received
                                                     │
                                                     v
                                          SERVER_HELLO_RCVD
                                                  (2)
                                                     │
                                         CCS+fake Finished sent
                                                     │
                                                     v
                                               ESTABLISHED
                                                  (3)
```

### Wire-Level Handshake

The handshake produces three message exchanges that exactly match what a real TLS 1.3 client/server pair would send:

**Exchange 1: Client -> Server (ClientHello)**

The ClientHello is a TLS Record containing a Handshake message of type ClientHello (0x01). The record layer version is TLS 1.0 (0x0301), which is standard for ClientHello in real TLS stacks. The body contains:

- `legacy_version`: 0x0303 (TLS 1.2 -- the real version is in the supported_versions extension)
- `random`: 32 bytes from `randombytes()`
- `session_id`: 32 bytes
- `cipher_suites`: Three TLS 1.3 cipher suites, matching Chrome/Firefox defaults
- `compression`: null only
- Nine extensions (detailed below)

The nine extensions in order:

| Extension | ID | Content |
|-----------|----|---------|
| server_name (SNI) | 0x0000 | Hostname, patched by fingerprint profile |
| supported_groups | 0x000A | X25519, secp256r1, secp384r1 |
| signature_algorithms | 0x000D | rsa_pss_rsae_sha256, ecdsa_secp256r1_sha256, ed25519 |
| supported_versions | 0x002B | TLS 1.3 (0x0304) |
| key_share | 0x0033 | X25519 fake public key (32 random bytes) |
| session_ticket | 0x0023 | Empty (0 data bytes) |
| encrypt_then_mac | 0x0016 | Empty |
| extended_master_secret | 0x0017 | Empty |
| psk_key_exchange_modes | 0x002D | psk_dhe_ke (1) |

**Exchange 2: Server -> Client (ServerHello + CCS + fake EncryptedExtensions)**

The server response is three consecutive TLS records in a single TCP segment:

Record 1 -- ServerHello (Handshake type 0x02):
- `legacy_version`: 0x0303
- `server_random`: 32 random bytes
- `session_id`: echoed from client
- `cipher_suite`: TLS_CS_AES_256_GCM_SHA384 (0x1302)
- Extensions: supported_versions + key_share (X25519)

Record 2 -- ChangeCipherSpec:
- Single byte payload: 0x01

Record 3 -- Fake EncryptedExtensions:
- Content type: Application Data (0x17)
- Payload: 1500-2500 bytes of random data (size chosen uniformly at random)
- This mimics the real EncryptedExtensions message which typically contains ALPN, key_share, and other server parameters

**Exchange 3: Client -> Server (CCS + fake Finished)**

Record 1 -- ChangeCipherSpec (0x01)

Record 2 -- Fake Finished:
- Content type: Application Data (0x17)
- Payload: 48-64 bytes of random data (mimics the 32-byte verify_data + padding of a real Finished message)

### Post-Handshake Record Layer

Once the state reaches `ESTABLISHED`, all application data is wrapped in TLS Application Data records:

```
Byte 0:     0x17 (Application Data)
Bytes 1-2:  0x03 0x03 (TLS 1.2 version in record layer -- standard)
Bytes 3-4:  payload length (big-endian uint16)
Bytes 5+:   payload data (up to 16384 bytes per record)
```

The `dap_tls_mimicry_wrap()` function splits input data into chunks of at most `DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD` (16384) bytes, prepending a 5-byte record header to each chunk. The `dap_tls_mimicry_unwrap()` function reverses this, silently skipping any non-Application Data records (such as late CCS messages).

### Browser Fingerprint Profiles

The fingerprint registry (`dap_tls_fingerprint.c`) loads six profiles at process startup using a GCC constructor attribute. Each profile is a raw ClientHello template captured from a real browser, with known byte offsets for SNI patching.

Profiles are stored as `dap_tls_fp_profile_t` structures containing:

| Field | Purpose |
|-------|---------|
| `name` | Human-readable identifier (e.g. "chrome_120") |
| `ja3_string` | Expected JA3 string for this browser |
| `ja3_hash` | MD5 of the JA3 string (32 hex chars) |
| `clienthello` | Raw ClientHello bytes (handshake header + body) |
| `clienthello_size` | Template size in bytes |
| `sni_hostname_length_offset` | Offset of 2-byte BE hostname length within SNI extension |
| `sni_hostname_offset` | Offset where hostname bytes begin |
| `sni_data_length_offset` | Offset of 2-byte BE SNI data length |
| `extensions_length_offset` | Offset of 2-byte BE total extensions length |

Available profiles:

| Profile | File | JA3 Hash | Template Size |
|---------|------|----------|---------------|
| chrome_120 | `chrome_120.c` | cd08e31494f9531f560d64c695473da9 | 152 bytes |
| firefox_121 | `firefox_121.c` | b32309a26951912be7dba376398abc3b | 150 bytes |
| edge_120 | `edge_120.c` | -- | -- |
| safari_17 | `safari_17.c` | -- | -- |
| android_14 | `android_14.c` | -- | -- |
| telegram_android | `telegram_android.c` | e7d705a3286e19ea42f587b344ee6865 | 205 bytes |

**SNI Patching Algorithm** (`dap_tls_fp_build_clienthello()`):

When building a ClientHello from a template, the function performs a multi-field patch:

1. Copies the template into a new buffer, resized to accommodate the hostname
2. Patches `sni_hostname_length` (2 bytes BE) with the hostname length
3. Patches `sni_data_length` (hostname_length + 5)
4. Patches the server_name_list_length (hostname_length + 3)
5. Increases `extensions_length` by the hostname length delta
6. Copies hostname bytes at `sni_hostname_offset`
7. Recalculates the handshake length (first 4 bytes: type + 3-byte length)

If no profile is set, the engine falls back to inline ClientHello construction with the nine extensions listed above.

### JA3 Fingerprint Calculation

The JA3 module (`dap_tls_ja3.c`) computes the JA3 fingerprint from raw ClientHello data. The JA3 format is:

```
TLSVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats
```

Each field uses decimal values separated by dashes within the field, and commas between fields. The final JA3 hash is `MD5(ja3_string)` expressed as 32 lowercase hex characters.

Example for chrome_120:
```
JA3 String: 771,4866-4865-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,0-23-65281-10-11-35-16-5-13-18-51-45-43-27-17513,29-23-24,0
JA3 Hash:   cd08e31494f9531f560d64c695473da9
```

Three entry points are provided:

- `dap_tls_ja3_from_tls_record()` -- parses from TLS record layer (content type 0x16)
- `dap_tls_ja3_from_handshake()` -- parses from Handshake message header
- `dap_tls_ja3_from_client_hello_body()` -- parses from the ClientHello body (starts at legacy_version)

The parser extracts: TLS version, cipher suites, extension types (for the extensions field), supported_groups (for the curves field), and ec_point_formats (for the points field).

### Key API

```c
dap_tls_mimicry_t *dap_tls_mimicry_new(bool a_is_server);
void               dap_tls_mimicry_free(dap_tls_mimicry_t *a_m);

void dap_tls_mimicry_set_sni(dap_tls_mimicry_t *a_m, const char *a_hostname);
int  dap_tls_mimicry_set_profile(dap_tls_mimicry_t *a_m,
                                  const dap_tls_fp_profile_t *a_profile);

int dap_tls_mimicry_create_client_hello(dap_tls_mimicry_t *a_m,
                                         void **a_out, size_t *a_out_size);
int dap_tls_mimicry_process_client_hello(dap_tls_mimicry_t *a_m,
                                          const void *a_data, size_t a_size,
                                          void **a_response, size_t *a_response_size);
int dap_tls_mimicry_process_server_hello(dap_tls_mimicry_t *a_m,
                                          const void *a_data, size_t a_size,
                                          void **a_response, size_t *a_response_size);

int dap_tls_mimicry_wrap(dap_tls_mimicry_t *a_m,
                          const void *a_data, size_t a_size,
                          void **a_out, size_t *a_out_size);
int dap_tls_mimicry_unwrap(dap_tls_mimicry_t *a_m,
                            const void *a_data, size_t a_size,
                            void **a_out, size_t *a_out_size,
                            size_t *a_consumed);
```

---

## 2. UDP Transport

**Source:** `net/trans/udp/`
**Type:** `DAP_NET_TRANS_UDP_BASIC` (0x02) / `DAP_NET_TRANS_UDP_RELIABLE` (0x03)
**Socket:** UDP

### Overview

The UDP transport provides datagram-based communication with two operational modes. In Basic mode, packets are sent fire-and-forget with minimal overhead. In Reliable mode, an ARQ (Automatic Repeat reQuest) layer adds retransmission, ordering, and congestion feedback.

The entire packet payload is encrypted. DPI sees only random bytes of variable length. There are no plaintext headers, magic numbers, or version bytes on the wire.

### MTU and Fragmentation

The effective MTU for DAP Stream channel packets is **1200 bytes** (`DAP_STREAM_UDP_MAX_PAYLOAD_SIZE`), derived from:

```
Standard IPv4 MTU:              1500 bytes
- IPv4 header:                   -20
- UDP header:                     -8
- Internal FC+UDP header:        -50
- Encryption overhead (padding): -20
- Safety margin:                -200
= 1200 bytes usable payload
```

DAP Stream automatically fragments channel packets when `get_max_packet_size()` returns 1200.

### Packet Header Structure

The full header (`dap_stream_trans_udp_full_header_t`) extends the base Flow Control header with UDP-specific fields. It is serialized using `dap_serialize` with an extended schema and encrypted as part of the packet body:

```
Field           Size     Description
─────────────────────────────────────────
seq_num         64 bit   FC sequence number
ack_seq         64 bit   Highest received in-order sequence
timestamp_ms    32 bit   Timestamp for RTT calculation
fc_flags         8 bit   Flow control flags (keepalive, retransmit, FIN)
type             8 bit   Packet type (HANDSHAKE/DATA/KEEPALIVE/CLOSE)
session_id      64 bit   Per-connection session identifier
```

### Packet Types

| Code | Name | Description |
|------|------|-------------|
| 0x01 | HANDSHAKE | Kyber512 key exchange (800 bytes, obfuscated) |
| 0x02 | SESSION_CREATE | Session establishment (encrypted) |
| 0x03 | DATA | Stream data payload (encrypted) |
| 0x04 | KEEPALIVE | Heartbeat (encrypted) |
| 0x05 | CLOSE | Session teardown (encrypted) |

HANDSHAKE packets are the only partially unencrypted packets -- they carry an obfuscated Kyber512 public key.

### Server Architecture: Sharded Listeners

The UDP server (`dap_net_trans_udp_server_t`) uses a cross-worker packet forwarding architecture:

```
                    ┌──────────────────┐
                    │  UDP Listener    │
                    │  (shared socket) │
                    └────────┬─────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
        ┌─────┴─────┐ ┌─────┴─────┐ ┌─────┴─────┐
        │ Worker 0  │ │ Worker 1  │ │ Worker N  │
        │ pipe_read │ │ pipe_read │ │ pipe_read │
        └─────┬─────┘ └─────┬─────┘ └─────┬─────┘
              │              │              │
        ┌─────┴─────┐ ┌─────┴─────┐ ┌─────┴─────┐
        │ Sessions  │ │ Sessions  │ │ Sessions  │
        │ Hash Table│ │ Hash Table│ │ Hash Table│
        └───────────┘ └───────────┘ └───────────┘
```

Each worker has a `udp_worker_context_t` containing:
- A read-end pipe esocket for receiving forwarded packets
- An array of write-end esockets (one per other worker) for forwarding
- Atomic counters for packets sent/received and batches flushed

When a packet arrives on a worker that does not own the target session, it is forwarded through the pipe to the correct worker with zero intermediate buffering -- packet pointers are written directly into the pipe's output buffer.

### Reliable Mode (ARQ)

When registered as `DAP_NET_TRANS_UDP_RELIABLE`, the transport attaches a `dap_io_flow_ctrl_t` instance that provides:

- **Retransmission**: Lost packets are detected via missing ACKs and retransmitted
- **Ordering**: `seq_num` / `ack_seq` fields ensure in-order delivery
- **RTT measurement**: `timestamp_ms` enables round-trip time estimation
- **Flow control flags**: fc_flags byte carries keepalive, retransmit, and FIN signals

The per-stream context (`dap_net_trans_udp_ctx_t`) also manages:
- Handshake retransmission via `dap_timerfd_t` (separate from FC)
- Buffered packet queue during handshake (packets queued until FC is ready)
- Handshake retry counter with configurable limits

### Configuration

```c
dap_stream_trans_udp_config_t {
    max_packet_size:  1400,     // max UDP packet size
    keepalive_ms:     30000,    // keepalive interval (30s)
    enable_checksum:  true,     // payload checksum validation
    allow_fragmentation: false  // IP fragmentation (not recommended)
};
```

### Key API

```c
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name);
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_server,
                                    const char *a_addr, uint16_t a_port);
void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_server);

int dap_net_trans_udp_stream_register(void);
dap_stream_trans_udp_config_t dap_stream_trans_udp_config_default(void);
```

---

## 3. HTTP Transport

**Source:** `net/trans/http/`
**Type:** `DAP_NET_TRANS_HTTP` (0x01)
**Socket:** TCP

### Role

The HTTP transport is the legacy default -- the original transport used by all DAP clients before the abstraction layer existed. It provides full backward compatibility with existing deployments while bridging into the new transport architecture.

### Protocol Flow

The HTTP transport uses a classic request-response pattern over TCP:

```
Client                              Server
  │                                   │
  │  POST /enc/gd4y5yh78w42aaagh      │
  │  ?enc_type=2,pkey_exchange_...    │  Encryption handshake
  │<──────────────────────────────────>│
  │                                   │
  │  POST /stream_ctl (encrypted)     │
  │  channels=C,F,N&enc_type=...      │  Session creation
  │<──────────────────────────────────>│
  │                                   │
  │  GET /stream/globaldb             │
  │  ?session_id=12345                │  Data streaming (long-poll)
  │<──────────────────────────────────>│
  │                                   │
```

The encryption handshake parameters are passed as HTTP query string key-value pairs:
```
enc_type=2,pkey_exchange_type=5,pkey_exchange_size=1184,block_key_size=32
```

### Protocol Translation Layer

The HTTP stream adapter (`dap_net_trans_http_stream.c`) provides bidirectional translation between the HTTP world and the TLV-based transport abstraction:

- `dap_stream_trans_http_parse_query_params()` -- extracts `dap_net_handshake_params_t` from an HTTP query string
- `dap_stream_trans_http_format_query_params()` -- serializes handshake parameters into HTTP query format
- `dap_stream_trans_http_translate_request_to_http()` -- converts a TLV handshake request into HTTP-compatible format
- `dap_stream_trans_http_translate_response_from_http()` -- parses an HTTP JSON response into a TLV handshake response

This translation layer allows the DAP Stream protocol to remain transport-agnostic while the HTTP adapter handles all HTTP-specific encoding.

### Server Module

The HTTP server (`dap_net_trans_http_server_t`) wraps `dap_http_server_t` and registers DAP protocol endpoint handlers:

- `/enc` -- Encryption handshake endpoint (`dap_stream_trans_http_add_enc_proc()`)
- `/stream` -- Stream data endpoint (`dap_stream_trans_http_add_proc()`)
- `/stream_ctl` -- Session control endpoint

Multiple address:port pairs are supported through `dap_net_trans_http_server_start()`, which iterates over the provided arrays and creates listeners for each.

### Request API

Two public functions handle outgoing HTTP requests:

- `dap_net_trans_http_request()` -- sends an unencrypted HTTP request (used by `dap_client_request()`)
- `dap_net_trans_http_request_enc()` -- sends an encrypted HTTP request (used by `dap_client_request_enc()`)

Both are thread-safe and route through the HTTP transport's internal queuing.

### Characteristics

- `get_max_packet_size()` returns 0 -- streaming transport, no fragmentation
- No built-in DPI resistance -- HTTP traffic is trivially identifiable
- Lowest implementation complexity of all transports
- Highest ecosystem maturity and test coverage

---

## 4. DNS Tunnel Transport

**Source:** `net/trans/dns/`
**Type:** `DAP_NET_TRANS_DNS_TUNNEL` (0x07)
**Socket:** UDP (port 53)

### Purpose

The DNS tunnel transport carries data through DNS queries and responses. It is designed for environments where DNS is the only permitted protocol -- restrictive firewalls, captive portals, and networks with strict egress filtering.

### MTU

The effective payload size is **1200 bytes** per DNS transaction -- the same conservative limit as the UDP transport. It is constrained by:

- DNS UDP query limit: 512 bytes (RFC 1035)
- DNS TXT record limit: 255 bytes
- Base32/Base64 encoding overhead: ~30-40%
- DNS + IP/UDP headers: ~40-60 bytes

This makes DNS the lowest-bandwidth transport in the toolkit.

### Data Encoding Pipeline

```
Application payload
       │
       v
  Optional compression (zlib)
       │
       v
  Base32 or Base64 encoding
       │
       v
  Split into DNS TXT record-sized chunks (max 255 bytes each)
       │
       v
  Embed into DNS query/response
       │
       v
  UDP datagram to port 53
```

The client encodes data into DNS query names (e.g., `<encoded-data>.example.com`), and the server responds with TXT records containing the response data.

### Server Architecture

The DNS server (`dap_net_trans_dns_server_t`) routes clients by IP:port pair since DNS is connectionless:

- `dns_server_client_session_t` -- per-client session stored in a hash table keyed by `(remote_addr, remote_port)`
- Each session owns its own `handshake_key` (derived from Kyber512 exchange) and `stream` instance
- The sessions hash table is protected by `pthread_mutex_t` because it is shared across all sharded listener sockets (one per worker thread)

### Configuration

```c
dap_stream_trans_dns_config_t {
    max_record_size:     255,          // RFC 1035 TXT record limit
    max_query_size:      512,          // UDP DNS query limit
    query_timeout_ms:    5000,         // per-query timeout
    use_base32:          true,         // true=Base32, false=Base64
    enable_compression:  false,        // pre-encoding compression
    domain_suffix:       NULL        // query domain suffix
};
```

### DPI Resistance

DNS tunneling offers the strongest DPI resistance of any transport:

- DNS traffic is permitted by virtually all firewalls and network policies
- Queries look like standard DNS lookups to passive observers
- Port 53 is rarely blocked or rate-limited at the network level
- Active probing (sending fake DNS responses) can be detected, but most DPI systems do not attempt this

The tradeoff is severe: throughput is measured in kilobits per second, and latency is high due to the query-response nature of DNS.

---

## 5. WebSocket Transport

**Source:** `net/trans/websocket/`
**Type:** `DAP_NET_TRANS_WEBSOCKET` (0x05)
**Socket:** TCP

### Overview

The WebSocket transport implements RFC 6455 over HTTP, providing full-duplex bidirectional communication. It is the natural choice for browser-based clients and environments where HTTP reverse proxies are in the path.

### Connection Establishment

The WebSocket connection begins with an HTTP Upgrade handshake:

```
Client -> Server:
  GET /stream HTTP/1.1
  Host: vpn.example.com
  Upgrade: websocket
  Connection: Upgrade
  Sec-WebSocket-Key: <base64-encoded 16 bytes>
  Sec-WebSocket-Version: 13

Server -> Client:
  HTTP/1.1 101 Switching Protocols
  Upgrade: websocket
  Connection: Upgrade
  Sec-WebSocket-Accept: <base64 SHA-1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")>
```

After the 101 response, the TCP connection switches to WebSocket framing. The deferred `ready_callback` is invoked at this point to signal that the stream is operational.

### Frame Format

All data flows as WebSocket frames with the standard RFC 6455 header:

- **FIN bit**: 1 for final fragment, 0 for continuation
- **Opcode**: frame type (text, binary, ping, pong, close, continuation)
- **MASK bit**: set for client-to-server frames (required by RFC 6455)
- **Payload length**: 7-bit (0-125), 16-bit (126), or 64-bit (127) extended
- **Masking key**: 4 bytes, XORed with payload when MASK=1

### Frame Types

| Opcode | Name | Direction | Purpose |
|--------|------|-----------|---------|
| 0x00 | CONTINUATION | Both | Subsequent fragments of a fragmented message |
| 0x01 | TEXT | Both | UTF-8 text data |
| 0x02 | BINARY | Both | Binary data (used by DAP Stream) |
| 0x08 | CLOSE | Both | Graceful connection teardown |
| 0x09 | PING | Both | Heartbeat request |
| 0x0A | PONG | Both | Heartbeat response |

### HTTP Coexistence

A key design feature is the ability to share a port with plain HTTP. The function `dap_net_trans_websocket_try_upgrade()` is designed to be called at the beginning of an HTTP stream handler's `headers_read` callback. If the request contains WebSocket Upgrade headers, it performs the full 101 upgrade and switches to WebSocket mode. Otherwise, it returns -1 and normal HTTP processing continues.

This allows a single server port to serve both HTTP and WebSocket clients simultaneously.

### Per-Stream State

Each WebSocket connection maintains its own `dap_net_trans_websocket_private_t` containing:

- Frame assembly buffer for handling partial reads
- Fragment tracking (opcode of first fragment, remaining bytes)
- Current masking key for client frames
- Ping timer and pong timeout tracking
- Connection state machine: CONNECTING -> OPEN -> CLOSING -> CLOSED
- Statistics counters: frames/bytes sent and received

### Close Protocol

Graceful closure uses `dap_net_trans_websocket_send_close()` with RFC 6455 status codes:

| Code | Name | Meaning |
|------|------|---------|
| 1000 | NORMAL | Clean shutdown |
| 1001 | GOING_AWAY | Server shutting down or client navigating away |
| 1002 | PROTOCOL_ERROR | Protocol violation |
| 1006 | ABNORMAL | Reserved -- connection dropped without close frame |
| 1011 | UNEXPECTED | Server encountered an unexpected condition |

### Configuration

```c
dap_net_trans_websocket_config_t {
    max_frame_size:      1048576,       // max single frame payload (1 MB)
    ping_interval_ms:    30000,         // heartbeat interval
    pong_timeout_ms:     10000,         // pong timeout before disconnect
    enable_compression:  false,         // permessage-deflate extension
    client_mask_frames:  true,          // client masking (RFC 6455 required)
    server_mask_frames:  false,         // server masking (usually off)
    subprotocol:         NULL           // Sec-WebSocket-Protocol value
};
```

### Key API

```c
dap_net_trans_websocket_server_t *dap_net_trans_websocket_server_new(const char *name);
int dap_net_trans_websocket_server_start(dap_net_trans_websocket_server_t *ws,
                                          const char *cfg, const char **addrs,
                                          uint16_t *ports, size_t count);
int dap_net_trans_websocket_server_add_upgrade_handler(
    dap_net_trans_websocket_server_t *ws, const char *path);
int dap_net_trans_websocket_try_upgrade(dap_http_client_t *http_client);

int dap_net_trans_websocket_parse_frame(...);
int dap_net_trans_websocket_build_frame(...);
int dap_net_trans_websocket_send_close(dap_stream_t *s, dap_ws_close_code_t code, const char *reason);
int dap_net_trans_websocket_send_ping(dap_stream_t *s, const void *payload, size_t size);
int dap_net_trans_websocket_get_stats(const dap_stream_t *s, uint64_t *sent, uint64_t *recv, ...);
```

---

## Transport Comparison Matrix

| Property | TLS Mimicry | UDP Basic | UDP Reliable | HTTP | DNS Tunnel | WebSocket |
|----------|-------------|-----------|--------------|------|------------|-----------|
| **Type code** | 0x06 | 0x02 | 0x03 | 0x01 | 0x07 | 0x05 |
| **Socket type** | TCP | UDP | UDP | TCP | UDP/53 | TCP |
| **Latency** | Low | Minimal | Medium | High | Very high | Medium |
| **Throughput** | High | High | High | Medium | Very low | Medium |
| **Delivery guarantee** | Yes (TCP) | No | Yes (ARQ) | Yes (TCP) | No | Yes (TCP) |
| **Ordering** | Yes | No | Yes | Yes | No | Yes |
| **DPI resistance** | High | Medium | Medium | Low | Very high | Medium |
| **MTU / fragmentation** | 0 (streaming) | 1200 | 1200 | 0 (streaming) | 1200 | 0 (streaming) |
| **Full duplex** | Yes | Yes | Yes | No (req/resp) | Yes | Yes |
| **Implementation complexity** | High | Medium | High | Low | Medium | Medium |
| **Capabilities flags** | RELIABLE, ORDERED, OBFUSCATION, MIMICRY, HIGH_THROUGHPUT, BIDIRECTIONAL | LOW_LATENCY, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL | OBFUSCATION, LOW_LATENCY, BIDIRECTIONAL | RELIABLE, ORDERED, BIDIRECTIONAL, MULTIPLEXING |

### Selection Guide

| Scenario | Recommended Transport |
|----------|----------------------|
| DPI bypass in censored networks | TLS Mimicry with browser fingerprint profile |
| Lowest possible latency (gaming, VoIP) | UDP Basic |
| Reliable low-latency (file transfer, streaming) | UDP Reliable |
| Legacy client compatibility | HTTP |
| Maximum censorship resistance | DNS Tunnel |
| Browser client or reverse proxy environment | WebSocket |
| Embedded device behind strict firewall | DNS Tunnel (fallback: TLS Mimicry) |

---

## Related Documents

- [02-transport-abstraction](02-transport-abstraction_en.md) -- TAL architecture, vtable interface, transport registration, capability flags
- [04-obfuscation](04-obfuscation_en.md) -- Obfuscation engine, padding, mimicry hooks
- [01-io-layer](01-io-layer_en.md) -- IO layer, event loop, worker thread pool
