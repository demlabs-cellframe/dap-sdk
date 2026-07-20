# Cryptographic Model of DAP Stream

> **Version:** 1.0 | **Date:** 2026-07-20 | **Module:** `dap-sdk/crypto/`, `dap-sdk/net/trans/`

## Overview

DAP Stream uses a layered cryptographic model with three levels of keys. Obfuscation protects against DPI, the DSHP handshake provides key exchange, and stream encryption secures the data payload.

## Three-Level Key Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│ Level 1: session_key_open (asymmetric, KEM)                 │
│   Kyber / Falcon / ECDSA / MSRLN                            │
│   Public key exchange between Alice and Bob                 │
│   → Shared secret                                           │
├─────────────────────────────────────────────────────────────┤
│ Level 2: session_key (symmetric, session)                   │
│   Derived from the shared secret via KDF                    │
│   Used to encrypt DSHP messages after handshake             │
├─────────────────────────────────────────────────────────────┤
│ Level 3: stream_key (stream encryption)                     │
│   Derived from session_key                                  │
│   Encrypts / decrypts stream packets (dap_stream_pkt_t)     │
└─────────────────────────────────────────────────────────────┘
```

### Key Flow

```
Alice                          Bob
  │                              │
  │  pub_key_alice ─────────────→│
  │                              │ shared_secret = KEM(alice_pub, bob_priv)
  │←───────────── pub_key_bob    │
  │                              │
  shared_secret = KEM(bob_pub, alice_priv)
  │                              │
  session_key = KDF(shared_secret)
  stream_key  = KDF(session_key)
  │                              │
  │  encrypted stream data ←────→│
```

## Core Structure: dap_enc_key_t

`dap_enc_key_t` is a universal wrapper for all encryption algorithms. The `priv_key_data` and `shared_key` fields are in a union to support both asymmetric keys and shared secrets:

```c
typedef struct dap_enc_key {
    // Key data sizes (union: for asymmetric or shared secrets)
    union {
        size_t      priv_key_data_size;
        size_t      shared_key_size;
    };
    union {
        void        *priv_key_data;     // Private key (asymmetric)
        byte_t      *shared_key;        // Shared secret (symmetric)
    };
    size_t              pub_key_data_size;
    void                *pub_key_data;  // Public key
    time_t              last_used_timestamp;
    dap_enc_key_type_t  type;           // Algorithm type

    // Function pointers — encryption
    dap_enc_callback_dataop_t     enc;      // Encryption (authenticated)
    dap_enc_callback_dataop_t     dec;      // Decryption (authenticated)
    dap_enc_callback_dataop_na_t  enc_na;   // Encryption (no-auth)
    dap_enc_callback_dataop_na_t  dec_na;   // Decryption (no-auth)
    dap_enc_callback_dataop_na_ext_t dec_na_ext; // Decryption (extended)

    // Function pointers — signatures
    dap_enc_callback_sign_op_t    sign_get;     // Signature generation
    dap_enc_callback_sign_op_t    sign_verify;  // Signature verification

    // Function pointers — KEM (Key Encapsulation)
    dap_enc_gen_alice_shared_key  gen_alice_shared_key; // Alice side KEM
    dap_enc_gen_bob_shared_key    gen_bob_shared_key;   // Bob side KEM

    // Additional fields
    void                *pbk_list_data;     // Public key list data
    size_t              pbk_list_size;
    dap_enc_get_allpbk_list get_all_pbk_list;
    void                *_pvt;              // Private data
    void                *_inheritor;        // Inheritor data
    size_t              _inheritor_size;
} dap_enc_key_t;
```

## Supported Algorithms

### Asymmetric (KEM / Signature)

| Algorithm | Type | Description |
|-----------|------|-------------|
| Kyber | KEM | Post-quantum key encapsulation (Kyber512/768/1024) |
| Falcon | Signature | Post-quantum signature (Falcon-512/1024) |
| Dilithium | Signature | Post-quantum signature (Dilithium2/3/5) |
| ECDSA | Signature | Elliptic Curve (secp256k1, P-256) |
| SPHINCS+ | Signature | Post-quantum hash-based signature |
| MSRLN | KEM | Legacy lattice-based KEM (P2P compatibility) |

### Symmetric

| Algorithm | Type | Description |
|-----------|------|-------------|
| AES | Block cipher | AES-128/192/256 (OAES mode) |
| SALSA2012 | Stream cipher | Salsa20/12 (used in obfuscation) |
| GOST | Block cipher | GOST 28147-89 OFB (Magma) / GOST 28147-14 OFB (Kuznechik) |
| Blowfish | Block cipher | Blowfish |
| SEED | Block cipher | Korean SEED cipher |

### Hashing and KDF

| Algorithm | Description |
|-----------|-------------|
| SHAKE256 | KDF for obfuscation (produces nonce + key) |
| SHA-256/512 | Standard hash |
| Streebog | Russian GOST hash |
| BLAKE2 | High-performance hash |
| MD5 | Legacy (JA3 fingerprints) |

## TLS Wrapper

An abstraction for real TLS (not mimicry) via platform-specific APIs:

| Implementation | Platform | Module |
|----------------|----------|--------|
| OpenSSL | Linux, BSD, generic | `crypto/tls/os/openssl/dap_tls_openssl.c` |
| Apple Security | macOS, iOS | `crypto/tls/os/apple/dap_tls_apple.c` |
| SChannel | Windows | `crypto/tls/os/windows/dap_tls_schannel.c` |

Interface: `crypto/tls/include/dap_tls.h`

## Obfuscation (Transport Layer)

Obfuscation uses SALSA2012 with a key derived from the packet size:

```c
// KDF: seed + "obfuscation" + packet_size → 40 bytes
// [0..7]  = nonce (8 bytes)
// [8..39] = key (32 bytes)
dap_enc_kdf_derive(seed, "obfuscation", packet_size) → [nonce(8), key(32)]

// Encryption
dap_enc_code(SALSA2012, key, nonce, plaintext) → ciphertext
```

**Properties:**
- The key is ephemeral -- it depends on the packet size
- The seed is static: `"cellframe-transport-obfuscation-v1"`
- This is not cryptographic protection -- it is anti-DPI only
- The key is discarded after deobfuscation

## Stream Packet Encryption

### Write (dap_stream_pkt_write_unsafe)

```c
if (session->key) {
    // Encrypt via enc_na (no-auth) or enc (authenticated)
    session->key->enc_na(session->key, data, size, &encrypted, &encrypted_size);
} else {
    // No encryption (plain copy)
    memcpy(output, data, size);
}
```

### Read (dap_stream_pkt_read_unsafe)

```c
if (session->key) {
    session->key->dec_na(session->key, data, size, &decrypted, &decrypted_size);
} else {
    memcpy(output, data, size);
}
```

**Encryption overhead:** `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD = 200 bytes` -- maximum overhead from encryption (padding, nonce, auth tag).

## Channel Encryption

A channel (`dap_stream_ch_pkt_t`) carries an `enc_type` field in its header:

```c
typedef struct dap_stream_ch_pkt_hdr {
    uint8_t     id;           // Channel ID
    uint8_t     enc_type;     // Encryption type (0 = unencrypted)
    uint8_t     type;         // Packet type
    uint8_t     padding;
    uint64_t    seq_id;
    uint32_t    data_size;
} __attribute__((packed)) dap_stream_ch_pkt_hdr_t;
```

If `enc_type != 0`, the channel data is encrypted independently of the stream-level encryption.

## Keys in dap_net_trans_ctx_t

The transport context stores all three key levels:

```c
typedef struct dap_net_trans_ctx {
    dap_enc_key_t *session_key_open;   // Asymmetric KEM
    dap_enc_key_t *session_key;        // Symmetric session key
    dap_enc_key_t *stream_key;         // Stream encryption key
    char          *session_key_id;     // Key identifier
    // ...
} dap_net_trans_ctx_t;
```

## Security

### What Each Level Protects Against

| Level | Protects Against | Does NOT Protect Against |
|-------|-----------------|--------------------------|
| Obfuscation | DPI, traffic analysis | Targeted decryption |
| DSHP handshake | Key interception | Compromised endpoint |
| Stream encryption | Eavesdropping | Compromised endpoint, side-channel attacks |

### Post-Quantum Readiness

The DAP SDK supports post-quantum algorithms:
- **Kyber** -- KEM (replaces ECDH)
- **Falcon / Dilithium** -- signatures (replace ECDSA)
- **SPHINCS+** -- hash-based signatures (backup)

## Related Documents

- [03 -- DSHP Handshake](03-handshake_en.md) -- key exchange protocol
- [04 -- Obfuscation](../transport/04-obfuscation_en.md) -- transport obfuscation
- [01 -- Stream Protocol](01-stream-protocol_en.md) -- packet format
- Header files: `crypto/include/dap_enc_key.h`, `dap_enc.h`, `crypto/tls/include/dap_tls.h`
