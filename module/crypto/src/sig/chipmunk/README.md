# Chipmunk Ring Signature

Post-quantum ring signature scheme based on lattice cryptography, providing signer anonymity within a group.

## Security Level

Default configuration provides **NIST Level V+** security:
- ~300-bit classical security
- ~150-bit quantum security  
- Requires ~131,000 logical qubits for quantum attack
- Designed for 100+ year quantum resistance

## Quick Start

```c
#include "dap_enc_chipmunk_ring.h"
#include "chipmunk_ring.h"

// Initialize with default security (Level V+)
dap_enc_chipmunk_ring_init();

// Or choose specific security level
dap_enc_chipmunk_ring_init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_III);

// Generate keys
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, 
                                               NULL, 0, NULL, 0, 0);

// Create ring signature
dap_sign_t *sig = dap_sign_create_ring(key, ring_keys, ring_size, 
                                        required_signers, message, msg_size);

// Verify
int result = dap_sign_verify_ring(sig, message, msg_size, ring_keys, ring_size);
```

## Security Levels

| Level | Classical | Quantum | Qubits | Use Case |
|-------|-----------|---------|--------|----------|
| I | ~150 bit | ~75 bit | 32K | General |
| III | ~224 bit | ~112 bit | 65K | Financial |
| V | ~299 bit | ~150 bit | 90K | High security |
| V+ | ~300 bit | ~150 bit | 131K | Long-term (default) |

```c
// Check current security info
chipmunk_ring_security_info_t info;
dap_enc_chipmunk_ring_get_security_info(&info);
printf("Security: %u bits classical, %lu qubits required\n", 
       info.classical_bits, info.logical_qubits_required);

// Validate minimum level
if (dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V) != 0) {
    // Below required security level
}
```

## Performance (AMD Ryzen 9 7950X3D, AVX-512)

| Ring Size | Signature | Sign | Verify |
|-----------|-----------|------|--------|
| 2 | 20.5 KB | 1.3 ms | 0.2 ms |
| 8 | 45.6 KB | 3.9 ms | 0.5 ms |
| 32 | 145.9 KB | 13.0 ms | 1.5 ms |
| 64 | 279.7 KB | 22.5 ms | 2.9 ms |

## Features

- **Anonymity**: Signer identity hidden within ring
- **Linkability**: Optional linkability tags for double-spend prevention
- **Multi-signer**: Threshold signatures (M-of-N)
- **SIMD Optimized**: AVX2, AVX-512, NEON runtime dispatch
- **Post-Quantum**: Lattice-based (Ring-LWE, NTRU, Code-based layers)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    dap_sign API                         │
│  dap_sign_create_ring() / dap_sign_verify_ring()       │
├─────────────────────────────────────────────────────────┤
│                 dap_enc_key API                         │
│  DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING                    │
├─────────────────────────────────────────────────────────┤
│              chipmunk_ring_sign/verify                  │
│  Acorn ZK proofs, secret sharing, serialization        │
├─────────────────────────────────────────────────────────┤
│                  chipmunk base                          │
│  NTT polynomials (SIMD: AVX2/AVX-512/NEON)            │
└─────────────────────────────────────────────────────────┘
```

## Files

| File | Description |
|------|-------------|
| `chipmunk_ring.c/h` | Core sign/verify implementation |
| `chipmunk_ring_acorn.c/h` | Acorn verification proofs |
| `chipmunk_ring_secret_sharing.c/h` | Threshold secret sharing |
| `chipmunk_ring_serialize_schema.c/h` | Binary serialization |
| `chipmunk_ring_errors.c/h` | Error handling |

## API Reference

### Initialization

```c
int dap_enc_chipmunk_ring_init(void);
int dap_enc_chipmunk_ring_init_with_security_level(chipmunk_ring_security_level_t level);
int dap_enc_chipmunk_ring_get_security_info(chipmunk_ring_security_info_t *info);
int dap_enc_chipmunk_ring_validate_security_level(chipmunk_ring_security_level_t min_level);
```

### Signing

```c
// Single signer ring signature
dap_sign_t *dap_sign_create_ring(dap_enc_key_t *signer_key,
                                  dap_enc_key_t **ring_keys,
                                  size_t ring_size,
                                  uint32_t required_signers,  // 1 for single
                                  const void *message,
                                  size_t message_size);

// Verification
int dap_sign_verify_ring(const dap_sign_t *sign,
                         const void *message,
                         size_t message_size,
                         dap_enc_key_t **ring_keys,
                         size_t ring_size);
```

### Low-level API

```c
int chipmunk_ring_sign(const chipmunk_private_key_t *priv_key,
                       const void *message, size_t message_size,
                       const chipmunk_ring_public_key_t *ring_pub_keys,
                       size_t ring_size,
                       chipmunk_ring_signature_t *signature);

int chipmunk_ring_verify(const void *message, size_t message_size,
                         const chipmunk_ring_signature_t *signature,
                         const chipmunk_ring_public_key_t *ring_pub_keys,
                         size_t ring_size);
```

## License

GNU General Public License v3.0
