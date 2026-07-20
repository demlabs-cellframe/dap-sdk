# DAP Stream Handshake Protocol (DSHP)

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/stream/stream/`

## Overview

DSHP (DAP Stream Handshake Protocol) is a binary, TLV-encoded, transport-agnostic handshake protocol. It replaces the legacy HTTP-based handshake, achieving 34% smaller message size. DSHP works over any binary transport: UDP, WebSocket, TLS, DNS.

### Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Magic | `0x44415053` | `'DAPS'` in ASCII -- protocol signature |
| Version | `0x01000000` | 1.0.0.0 (major.minor.patch.build) |
| Max TLV size | 65535 bytes | Maximum value length for a single TLV record |

## TLV Format

Each protocol field is encoded as a TLV triplet (Type-Length-Value):

### Header Structure (4 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          type (uint16)        |        length (uint16)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         value[length]                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Type | Byte Order | Description |
|-------|------|------------|-------------|
| type | uint16 | Network (big-endian) | TLV record type |
| length | uint16 | Network (big-endian) | Length of the value field in bytes |
| value | uint8[length] | -- | Arbitrary structured data |

### Compatibility

- **Forward compatibility:** unknown TLV types are silently skipped -- the parser advances the pointer by `length` bytes.
- **Version check:** only the major byte is compared. If the major version matches, differences in the minor version are allowed.

## TLV Type Ranges

| Range | Group | Description |
|-------|-------|-------------|
| `0x0100`-`0x01FF` | Header | Base fields: MAGIC, VERSION, MESSAGE_TYPE, STATUS |
| `0x0200`-`0x02FF` | Encryption | Encryption parameters: ENC_TYPE, PKEY_EXCHANGE_TYPE, PKEY_EXCHANGE_SIZE, BLOCK_KEY_SIZE |
| `0x0300`-`0x03FF` | Auth/Alice | Client authentication: ALICE_PUB_KEY, ALICE_SIGNATURE, ALICE_CERT |
| `0x0400`-`0x04FF` | Extensions | Extensions: OBFUSCATION_PARAMS, TRANSPORT_HINTS |
| `0x0500`-`0x05FF` | Session | Session management: SESSION_ID, SESSION_TIMEOUT |
| `0x0600`-`0x06FF` | Bob | Server response: BOB_PUB_KEY, BOB_SIGNATURE |
| `0x0700`-`0x07FF` | Errors | Error reporting: ERROR_CODE, ERROR_MESSAGE |
| `0x0800`-`0x08FF` | Stream | Stream parameters: CHANNELS, STREAM_ENC_TYPE, STREAM_ENC_SIZE, STREAM_ENC_HDR |

## Message Types

The protocol defines 6 message types for a three-phase handshake:

| # | Name | Code | Direction | Description |
|---|------|------|-----------|-------------|
| 1 | `DSHP_MSG_HANDSHAKE_REQUEST` | `0x0001` | Client -> Server | Initial handshake |
| 2 | `DSHP_MSG_HANDSHAKE_RESPONSE` | `0x0002` | Server -> Client | Response with Bob's key |
| 3 | `DSHP_MSG_SESSION_CREATE` | `0x0003` | Client -> Server | Create session (encrypted) |
| 4 | `DSHP_MSG_SESSION_CREATE_RESPONSE` | `0x0004` | Server -> Client | Session result (encrypted) |
| 5 | `DSHP_MSG_STREAM_READY` | `0x0005` | Client -> Server | Ready to stream |
| 6 | `DSHP_MSG_STREAM_START` | `0x0006` | Server -> Client | Start streaming |

## State Diagram

```
    Client                                  Server
      |                                       |
      |--- HANDSHAKE_REQUEST (0x0001) ------->|
      |    [magic, version, enc_type,         |
      |     pkey_exchange, alice_pub_key]     |
      |                                       |
      |<--- HANDSHAKE_RESPONSE (0x0002) ------|
      |    [status, session_id,               |
      |     bob_pub_key, timeout]             |
      |                                       |
      |=== Session Encrypted (ECDH) ==========|
      |                                       |
      |--- SESSION_CREATE (0x0003) ---------->|
      |    [channels, enc_type, enc_key]      |
      |                                       |
      |<--- SESSION_CREATE_RESPONSE ----------|
      |    [status, session_id]               |
      |                                       |
      |--- STREAM_READY (0x0005) ------------>|
      |                                       |
      |<--- STREAM_START (0x0006) ------------|
      |                                       |
      |===== Stream Data ====================|
      |                                       |
```

## Message Structures

### Handshake Request (0x0001)

The client initiates the handshake by sending encryption parameters and its public key.

| TLV Type | Required | Description |
|----------|----------|-------------|
| MAGIC | Mandatory | `0x44415053` |
| VERSION | Mandatory | `0x01000000` |
| ENC_TYPE | Mandatory | Encryption type |
| PKEY_EXCHANGE_TYPE | Mandatory | Key exchange type |
| PKEY_EXCHANGE_SIZE | Mandatory | Key exchange size |
| BLOCK_KEY_SIZE | Mandatory | Block key size |
| ALICE_PUB_KEY | Mandatory | Client's public key |
| ALICE_SIGNATURE | Optional | Client's signature |
| ALICE_CERT | Optional | Client's certificate |

### Handshake Response (0x0002)

The server responds with the result and its public key.

| TLV Type | Required | Description |
|----------|----------|-------------|
| MAGIC | Mandatory | `0x44415053` |
| VERSION | Mandatory | `0x01000000` |
| STATUS | Mandatory | Operation status (1 byte: 0=success, 1=error) |
| SESSION_ID | Mandatory | Session identifier (4 bytes) |
| SESSION_TIMEOUT | Mandatory | Session timeout in seconds (4 bytes) |
| BOB_PUB_KEY | Mandatory | Server's public key |
| BOB_SIGNATURE | Optional | Server's signature |
| ERROR_CODE | Optional | Error code (4 bytes, on failure) |
| ERROR_MESSAGE | Optional | Error message text |

### Session Create (0x0003)

The client creates a session within the encrypted channel.

| TLV Type | Required | Description |
|----------|----------|-------------|
| MAGIC | Mandatory | `0x44415053` |
| VERSION | Mandatory | `0x01000000` |
| CHANNELS | Mandatory | Channel string, e.g. `"C,F,N"` |
| ENC_TYPE | Mandatory | Stream encryption type |
| STREAM_ENC_SIZE | Mandatory | Encryption key size |
| STREAM_ENC_HDR | Optional | Encryption header |

### Session Create Response (0x0004)

The server confirms or rejects session creation.

| TLV Type | Required | Description |
|----------|----------|-------------|
| MAGIC | Mandatory | `0x44415053` |
| VERSION | Mandatory | `0x01000000` |
| STATUS | Mandatory | Operation status (1 byte: 0=success, 1=error) |
| SESSION_ID | Mandatory | Session identifier (4 bytes) |
| ERROR_CODE | Optional | Error code (4 bytes) |
| ERROR_MESSAGE | Optional | Error message text |

## Wire Format Examples

### Handshake Request (hex dump)

A typical handshake request with ECDH key exchange:

```
44 41 50 53                                     ; MAGIC 'DAPS'
01 00 00 00                                     ; VERSION 1.0.0.0
01 02 00 02 00 01                               ; MESSAGE_TYPE = 0x0001 (REQUEST)
02 00 00 02 00 01                               ; ENC_TYPE = 0x01
02 01 00 02 00 01                               ; PKEY_EXCHANGE_TYPE = 0x01
02 02 00 02 00 20                               ; PKEY_EXCHANGE_SIZE = 32
02 03 00 02 00 20                               ; BLOCK_KEY_SIZE = 32
03 00 00 20                                      ; ALICE_PUB_KEY (32 bytes)
   a1 b2 c3 d4 e5 f6 a7 b8 c9 d0 e1 f2 a3 b4 c5 d6
   a1 b2 c3 d4 e5 f6 a7 b8 c9 d0 e1 f2 a3 b4 c5 d6
```

**Total size:** 70 bytes (TLV headers + data).

### Handshake Response (hex dump)

Server response with session identifier:

```
44 41 50 53                                     ; MAGIC 'DAPS'
01 00 00 00                                     ; VERSION 1.0.0.0
01 02 00 02 00 02                               ; MESSAGE_TYPE = 0x0002 (RESPONSE)
01 03 00 01 00                               ; STATUS = 0x0000 (OK)
05 00 00 04 a1 b2 c3 d4                         ; SESSION_ID = 0xa1b2c3d4
05 01 00 04 00 00 0e 10                         ; SESSION_TIMEOUT = 3600 sec
06 00 00 20                                      ; BOB_PUB_KEY (32 bytes)
   f1 e2 d3 c4 b5 a6 97 88 79 6a 5b 4c 3d 2e 1f 00
   f1 e2 d3 c4 b5 a6 97 88 79 6a 5b 4c 3d 2e 1f 00
```

**Total size:** 66 bytes.

## Comparison with Legacy HTTP Handshake

| Parameter | Legacy HTTP | DSHP v1.0 |
|-----------|------------|-----------|
| Format | HTTP POST `/enc/gd4y5yh78w42aaagh` with JSON body | Binary TLV |
| Size | ~100+ bytes (HTTP headers + JSON) | ~64-68 bytes |
| Transport | HTTP/HTTPS only | Any binary (UDP, WS, TLS, DNS) |
| Compression | None | 34% smaller |
| Compatibility | Native for HTTP servers | Requires DSHP parser |

### Legacy Mode

For backward compatibility with P2P links, the legacy HTTP handshake is still supported via the `legacy_enc_handshake` flag. When enabled:

- The client sends an HTTP POST to `/enc/<base32-encoded-key>`
- The request body contains a JSON with encryption parameters
- The server responds with JSON containing the public key and session ID

## Memory Management

| Function Type | Behavior |
|---------------|----------|
| `*_create()` | Allocates output buffer; caller owns the memory |
| `*_free()` | Safely handles NULL pointers |
| `*_parse()` | Allocates result structure; caller must call `*_free()` |

Rules:
- Always check return pointers for NULL
- Always call the corresponding `*_free()` when done
- Never use a pointer after `*_free()`

## Cross-References

- [01 -- Stream Protocol](01-stream-protocol_en.md) -- stream protocol core
- [04 -- Encryption](04-encryption_en.md) -- cryptographic model
- [05 -- Client Transport](../transport/05-client-transport_en.md) -- client transport layer
- Header files: `stream/include/dap_stream_handshake.h`
