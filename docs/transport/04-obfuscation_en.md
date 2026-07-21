# Obfuscation and DPI Bypass -- Architecture and Implementation

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/net/trans/`

## Overview

DAP SDK implements a multi-layer obfuscation system to defeat Deep Packet Inspection (DPI) and network traffic analysis. The system operates at three levels: transport-level obfuscation of handshake packets, stream-level obfuscation of data, and TLS mimicry.

**Key principle:** obfuscation is NOT encryption. It is a means of disguising packet structure from DPI. Cryptographic protection is provided by upper layers (Kyber shared secret -> KDF -> session keys).

**Position in the stack:**

```
+------------------------------------------------------------------+
| L4: Applications (VPN, Services)                                 |
+------------------------------------------------------------------+
| L3: DAP Stream + DSHP Handshake (real encryption)                |
+------------------------------------------------------------------+
| L2: Transport Abstraction (dap_net_trans_t)                      |
|     + Transport Obfuscation Hook                                 |
+------------------------------------------------------------------+
| L1: Concrete Transports (TLS mimicry, UDP, HTTP, DNS)            |
+------------------------------------------------------------------+
| L0: IO Layer (dap_events_socket_t, dap_worker_t)                 |
+------------------------------------------------------------------+
```

## Security Model

Obfuscation in DAP SDK is built on three layers, each solving a distinct problem:

```
+---------------------------------------------------------+
| Layer 3: Transport Obfuscation                          |
| (anti-DPI: disguises packet structure)                  |
| -> dap_transport_obfuscation.c                          |
| -> SALSA2012 + KDF, ephemeral keys from packet size     |
+---------------------------------------------------------+
| Layer 2: DSHP Handshake                                 |
| (key exchange: Kyber shared secret -> KDF -> session key)|
| -> dap_stream_ch_pkt.c                                  |
+---------------------------------------------------------+
| Layer 1: Stream Encryption                              |
| (data protection: encrypts stream with session key)      |
| -> dap_stream.c, dap_enc                                |
+---------------------------------------------------------+
```

**Critical:** after deobfuscation, the obfuscation key is discarded. It does not participate in the cryptographic chain. The obfuscation layer exists solely to make packets look random to network observers; it provides zero cryptographic security on its own.

---

## Transport-Level Obfuscation (dap_transport_obfuscation.c)

### Design Philosophy: "Gift Wrapping"

The `dap_transport_obfuscation.c` module handles obfuscation at the handshake packet level. Think of it as gift wrapping: it hides the box, not what is inside. Real security comes from Kyber shared secret -> KDF -> session keys.

- Obfuscation keys are ephemeral, derived from packet size
- NOT part of the cryptographic chain
- After deobfuscation, the key is discarded

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DAP_TRANSPORT_OBFUSCATION_MIN_SIZE` | 850 bytes | Minimum obfuscated packet size. Accommodates Kyber512 public key (800 bytes) + header + padding |
| `DAP_TRANSPORT_OBFUSCATION_MAX_SIZE` | 1350 bytes | Maximum size. Under typical MTU. Range [850, 1350] gives ~500 bytes of variability for DPI resistance |
| `DAP_TRANSPORT_OBFUSCATION_SEED` | `"cellframe-transport-obfuscation-v1"` | Static seed for KDF. Must match on client and server |
| `SALSA20_NONCE_SIZE` | 8 bytes | Nonce size for SALSA2012 |

### Obfuscated Packet Header Structure

```c
typedef struct {
    uint16_t cleartext_total_size;  // Total cleartext size (for KDF)
    uint16_t handshake_size;        // Actual handshake data size
    // Followed by: handshake_data + random padding
} DAP_ALIGN_PACKED obfuscated_header_t;
```

The header is serialized via `dap_serialize` in little-endian byte order. The serialization schema is defined statically:

```c
static dap_serialize_schema_t s_obfuscated_header_schema = {
    .magic = DAP_SERIALIZE_MAGIC_NUMBER,
    .version = 1,
    .name = "obfuscated_header",
    .fields = s_obfuscated_header_fields,
    .field_count = 2,
    .struct_size = sizeof(obfuscated_header_t),
    .validate_func = NULL
};
```

### Obfuscate Algorithm (dap_transport_obfuscate_handshake)

```
Input:  handshake_data (e.g. Kyber512 public key, ~800 bytes)
Output: obfuscated packet of variable size

1. Choose random final cleartext size:
   cleartext_size = random(MIN_SIZE - 8, MAX_SIZE - 8)
   (subtract 8 bytes for SALSA2012 nonce overhead that will be added during encryption)

2. Serialize 4-byte header:
   [cleartext_total_size (uint16 LE)] [handshake_size (uint16 LE)]

3. Build cleartext packet:
   [header (4 bytes)] [handshake_data] [random_padding]
   padding_size = cleartext_size - header_size - handshake_size

4. Derive encryption key:
   dap_enc_kdf_derive(SEED, "obfuscation", packet_size) -> 40 bytes
   = [nonce (8 bytes)] + [key (32 bytes)] for SALSA2012

5. Encrypt:
   dap_enc_code(SALSA2012, cleartext) -> encrypted_data

6. Output size:
   encrypted_size = cleartext_size + 8 (SALSA2012 nonce is prepended automatically)
```

**Usage example:**

```c
uint8_t *l_obf = NULL;
size_t l_obf_size = 0;
int ret = dap_transport_obfuscate_handshake(kyber_key, 800, &l_obf, &l_obf_size);
// l_obf now contains 850-1350 bytes of encrypted data
send(socket, l_obf, l_obf_size);
DAP_DELETE(l_obf);
```

### Deobfuscate Algorithm (dap_transport_deobfuscate_handshake)

```
Input:  obfuscated packet
Output: original handshake data

1. Quick size check:
   dap_transport_is_obfuscated_handshake_size(size)
   -> verifies size falls within [MIN_SIZE, MAX_SIZE]

2. Calculate cleartext size:
   cleartext_size = encrypted_size - 8 (subtract SALSA2012 nonce)

3. Derive key (same KDF):
   dap_enc_kdf_derive(SEED, "obfuscation", cleartext_size) -> 40 bytes

4. Decrypt:
   dap_enc_decode(SALSA2012, encrypted_data) -> cleartext

5. Parse header:
   dap_deserialize_from_buffer_raw() -> obfuscated_header_t
   Validate: cleartext_total_size == actual decrypted size

6. Extract data:
   handshake_data = cleartext + header_size
   padding is discarded
```

### KDF: Deriving Key from Packet Size

The function `s_get_cipher_key_for_size` implements the core key derivation logic:

```c
static dap_enc_key_t* s_get_cipher_key_for_size(size_t a_packet_size,
                                                  dap_enc_key_t *a_existing_key)
{
    uint8_t l_kdf_key[40];
    uint64_t l_size_counter = (uint64_t)a_packet_size;

    dap_enc_kdf_derive(
        DAP_TRANSPORT_OBFUSCATION_SEED,  // "cellframe-transport-obfuscation-v1"
        strlen(DAP_TRANSPORT_OBFUSCATION_SEED),
        "obfuscation",
        strlen("obfuscation"),
        l_size_counter,
        l_kdf_key,
        sizeof(l_kdf_key)
    );

    // SALSA2012 extracts nonce from bytes [0:7], key from [8:39]
    dap_enc_key_t *l_key = dap_enc_key_new_from_raw_bytes(
        DAP_ENC_KEY_TYPE_SALSA2012, l_kdf_key, sizeof(l_kdf_key));

    // Zero KDF buffer (volatile to prevent compiler optimization)
    volatile void *l_volatile_ptr = l_kdf_key;
    memset((void*)l_volatile_ptr, 0, sizeof(l_kdf_key));

    return l_key;
}
```

The key is unambiguously determined by the packet size -- both sides (client and server) compute the same key for the same size. This is possible because the receiver knows the encrypted packet size (it is the number of bytes received on the wire), from which it subtracts the SALSA2012 nonce overhead to obtain the cleartext size used as the KDF counter.

### Integration with Transport Abstraction Layer

Transport Abstraction Layer attaches obfuscation via a hook:

```c
// Attach obfuscation engine to transport
dap_net_trans_attach_obfuscation(trans, obfuscation);

// Transparent obfuscation on write
dap_net_trans_write_obfuscated(ctx, data, size);

// Transparent deobfuscation on read
dap_net_trans_read_deobfuscated(ctx, buf, buf_size);
```

---

## Stream-Level Obfuscation (dap_stream_obfuscation.c)

Stream-level obfuscation operates on DAP Stream protocol data, above the transport layer. It provides four obfuscation techniques.

### Obfuscation Techniques

```c
typedef enum dap_stream_obfuscation_type {
    DAP_STREAM_OBFS_NONE        = 0x00,  // No obfuscation
    DAP_STREAM_OBFS_PADDING     = 0x01,  // Add random padding
    DAP_STREAM_OBFS_MIMICRY     = 0x02,  // Protocol mimicry
    DAP_STREAM_OBFS_TIMING      = 0x04,  // Timing obfuscation
    DAP_STREAM_OBFS_POLYMORPHIC = 0x08,  // Polymorphic magic numbers
    DAP_STREAM_OBFS_MIXING      = 0x10,  // Mix with fake traffic
    DAP_STREAM_OBFS_ALL         = 0x1F   // All techniques
} dap_stream_obfuscation_type_t;
```

### Obfuscation Levels

| Level | Techniques | Padding | Delay | Fake Traffic |
|-------|-----------|---------|-------|-------------|
| `NONE` | -- | -- | -- | -- |
| `LOW` | PADDING + TIMING | 8-64 bytes, 30% probability | 5-20 ms | -- |
| `MEDIUM` | PADDING + TIMING + MIXING | 16-256 bytes, 70% probability | 10-50 ms | 1 KB/s |
| `HIGH` | PADDING + TIMING + MIXING + MIMICRY | 32-512 bytes, 90% probability | 20-100 ms | 4 KB/s |
| `PARANOID` | ALL | 64-1024 bytes, 100% | 50-200 ms | 10 KB/s |

### Traffic Padding

Padding appends random bytes to real data. Probability and size are configurable:

```c
// Padding configuration
struct {
    size_t min_padding;           // Minimum padding size (bytes)
    size_t max_padding;           // Maximum padding size (bytes)
    float padding_probability;    // Probability of adding padding (0.0-1.0)
} padding;
```

During deobfuscation, the last byte contains the padding length (1-16), which allows recovery of the original data size.

### Fake Traffic Generation

Fake traffic masks real communication patterns. Packets of random size are generated and mixed into the stream:

```c
int dap_stream_obfuscation_generate_fake_traffic(
    dap_stream_obfuscation_t *a_obfs,
    void **a_fake_data,    // Output: random data
    size_t *a_fake_size    // Output: random size [min_packet_size, max_packet_size]
);
```

Mixing configuration:
```c
struct {
    uint32_t artificial_traffic_rate;  // Fake traffic rate (bytes/sec)
    uint32_t min_packet_size;          // Minimum fake packet size
    uint32_t max_packet_size;          // Maximum fake packet size
} mixing;
```

### Timing Obfuscation

Random delays between packets break flow patterns that DPI systems use for fingerprinting:

```c
uint32_t dap_stream_obfuscation_calc_delay(dap_stream_obfuscation_t *a_obfs);
// Returns delay in milliseconds [min_delay_ms, max_delay_ms]
```

---

## Protocol Mimicry (dap_stream_obfuscation_mimicry.c)

### Architecture

Protocol mimicry wraps data in the format of a legitimate protocol. DPI sees "real" HTTPS, HTTP/2, or WebSocket traffic.

```c
typedef enum dap_stream_mimicry_protocol {
    DAP_STREAM_MIMICRY_NONE      = 0,  // No mimicry
    DAP_STREAM_MIMICRY_HTTPS     = 1,  // HTTPS (TLS 1.2/1.3)
    DAP_STREAM_MIMICRY_HTTP2     = 2,  // HTTP/2
    DAP_STREAM_MIMICRY_WEBSOCKET = 3,  // WebSocket
    DAP_STREAM_MIMICRY_QUIC      = 4   // QUIC (UDP-based)
} dap_stream_mimicry_protocol_t;
```

### HTTPS Mimicry

Packets are wrapped in TLS Application Data records:

```
[0x17] [0x03 0x03] [length_be16] [payload]
  |        |            |           |
  |        |            |           +-- Real DAP stream data
  |        |            +-- Payload length (big-endian)
  |        +-- TLS version 1.2 on wire
  +-- Content Type: Application Data (0x17)
```

TLS ClientHello/ServerHello generation emulates the handshake:

```c
// Client generates ClientHello
dap_stream_mimicry_generate_client_hello(mimicry, &client_hello, &hello_size);

// Server responds with ServerHello
dap_stream_mimicry_generate_server_hello(mimicry, client_hello, client_hello_size,
                                          &server_hello, &hello_size);
```

### WebSocket Mimicry

Packets are wrapped in WebSocket frames:

```
[FIN=1, opcode=binary] [MASK + payload_len] [masking_key(4)] [masked_payload]
```

Client frame masking is supported (as in real WebSocket).

---

## TLS Mimicry (dap_tls_mimicry.c)

### Purpose

The `dap_tls_mimicry` module implements full TLS 1.3 mimicry at the wire level. DPI sees a genuine TLS handshake and Application Data records. However, no real TLS cryptography is performed -- DAP stream with its own DSHP handshake and `dap_enc` runs on top, treating the mimicry layer as a transparent byte pipe wrapped in TLS record framing.

### Wire Layout

```
Client -> Server:  TLS Record(Handshake/ClientHello)
Server -> Client:  TLS Record(Handshake/ServerHello)
                 + TLS Record(ChangeCipherSpec)
                 + TLS Record(ApplicationData) [fake EncryptedExtensions, 1500-2500 bytes]
Client -> Server:  TLS Record(ChangeCipherSpec)
                 + TLS Record(ApplicationData) [fake Finished, 48-64 bytes]
```

After handshake, all data is wrapped in Application Data records:
```
[0x17][0x03 0x03][length_be16][payload]
```

### Browser Fingerprint Profiles

To ensure realism, the mimicry engine supports browser fingerprint profiles:

```c
typedef enum dap_stream_browser_type {
    DAP_STREAM_BROWSER_CHROME  = 0,  // Google Chrome
    DAP_STREAM_BROWSER_FIREFOX = 1,  // Mozilla Firefox
    DAP_STREAM_BROWSER_SAFARI  = 2,  // Apple Safari
    DAP_STREAM_BROWSER_EDGE    = 3   // Microsoft Edge
} dap_stream_browser_type_t;
```

Profiles define the exact order and set of TLS extensions, cipher suites, and other ClientHello parameters matching real browsers. When a profile is set, template-based generation is used via `dap_tls_fp_build_clienthello()`.

### Wire-Compatible TLS Extensions

The mimicry generates standard TLS 1.3 extensions:

| Extension | ID | Purpose |
|-----------|----|---------|
| server_name (SNI) | 0x0000 | Hostname indication |
| supported_groups | 0x000A | X25519, secp256r1, secp384r1 |
| signature_algorithms | 0x000D | RSA-PSS, ECDSA, Ed25519 |
| supported_versions | 0x002B | TLS 1.3 (0x0304) |
| key_share | 0x0033 | X25519 (32-byte fake pubkey) |
| session_ticket | 0x0023 | Empty (no ticket) |
| encrypt_then_mac | 0x0016 | Supported |
| extended_master_secret | 0x0017 | Supported |
| psk_key_exchange_modes | 0x002D | psk_dhe_ke |

### Record Layer

Wrap/unwrap functions handle TLS record framing:

```c
// Wrap payload into TLS Application Data records
int dap_tls_mimicry_wrap(dap_tls_mimicry_t *a_m,
                         const void *a_data, size_t a_size,
                         void **a_out, size_t *a_out_size);

// Extract payload from TLS records
int dap_tls_mimicry_unwrap(dap_tls_mimicry_t *a_m,
                           const void *a_data, size_t a_size,
                           void **a_out, size_t *a_out_size,
                           size_t *a_consumed);
```

Maximum payload per record: 16384 bytes (`DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD`). Larger data is split across multiple records. Non-APPLICATION_DATA records (e.g. CCS arriving after handshake) are silently skipped during unwrap.

---

## Traffic Padding and Protocol Rotation

### Region-Based Obfuscation Profiles

The system supports regional obfuscation profiles adapted to the DPI intensity in different countries:

| Profile | Level | Description |
|---------|-------|-------------|
| `DEFAULT` | NONE | No obfuscation (for regions without DPI) |
| `RU` | MEDIUM | Moderate obfuscation (padding + timing + mixing) |
| `CN` / `IR` | HIGH | Strong obfuscation (all techniques + mimicry) |

### Traffic Padding

The historical name `fake_traffic` has been renamed to `traffic_padding`. Parameters:

- **min_padding / max_padding** -- padding size range
- **padding_probability** -- probability of adding padding (0.0-1.0)
- At 100% probability (PARANOID level), every packet receives padding

### Protocol Rotation

The historical name `protocol_rotation` has been renamed to `transport_rotation`. This mechanism allows dynamic switching of the transport and obfuscation profile during a session, making traffic analysis significantly harder.

---

## Full Packet Processing Chain

```
Outbound packet:
  Application Data
    |
  Stream Encryption (session_key)
    |
  Stream Obfuscation (padding, timing, mixing)
    |
  TLS Mimicry wrap (TLS Application Data record)
    |
  Transport Obfuscation (handshake packets only)
    |
  Network (appears as normal HTTPS traffic)

Inbound packet:
  Network
    |
  Transport Deobfuscation (handshake packets only)
    |
  TLS Mimicry unwrap
    |
  Stream Deobfuscation (remove padding)
    |
  Stream Decryption (session_key)
    |
  Application Data
```

## Related Documents

- [02 -- Transport Abstraction Layer](02-transport-abstraction_en.md) -- vtable, transport registration, common interface
- [03 -- Concrete Transports](03-transports_en.md) -- TLS, UDP, HTTP, DNS, WS implementations
- [05 -- Client Transport](05-client-transport_en.md) -- client-side connection logic
- Header files: `net/trans/include/dap_transport_obfuscation.h`, `net/stream/stream/include/dap_stream_obfuscation.h`, `net/stream/stream/include/dap_stream_obfuscation_mimicry.h`, `net/trans/tls/include/dap_tls_mimicry.h`
