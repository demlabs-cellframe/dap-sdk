# ChipmunkRing API Reference

## Overview

ChipmunkRing provides post-quantum ring signature capabilities for anonymous blockchain transactions. This document describes the complete API for integrating ChipmunkRing into applications.

## Core Functions

### Key Management

#### `dap_enc_chipmunk_ring_key_new_generate()`

```c
int dap_enc_chipmunk_ring_key_new_generate(
    struct dap_enc_key *a_key, 
    const void *a_seed,
    size_t a_seed_size, 
    size_t a_key_size
);
```

**Description**: Generates a new ChipmunkRing key pair.

**Parameters**:
- `a_key`: Pointer to key structure to initialize
- `a_seed`: Optional seed for deterministic generation (can be NULL)
- `a_seed_size`: Size of seed in bytes (0 for random generation)
- `a_key_size`: Reserved parameter (set to 0)

**Returns**: 0 on success, negative error code on failure

**Example**:
```c
dap_enc_key_t *key = dap_enc_key_new_generate(
    DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0
);
```

### Ring Signature Operations

#### `dap_sign_create_ring()`

```c
dap_sign_t *dap_sign_create_ring(
    dap_enc_key_t *a_signer_key,
    const void *a_data,
    size_t a_data_size,
    dap_enc_key_t **a_ring_keys,
    size_t a_ring_size,
    size_t a_signer_index
);
```

**Description**: Creates a ring signature using ChipmunkRing.

**Parameters**:
- `a_signer_key`: Private key of the signer
- `a_data`: Data to sign
- `a_data_size`: Size of data in bytes
- `a_ring_keys`: Array of public keys forming the ring
- `a_ring_size`: Number of keys in the ring (2-1024)
- `a_signer_index`: Index of signer's key in the ring

**Returns**: Pointer to signature structure, NULL on failure

#### `dap_sign_verify_ring()`

```c
int dap_sign_verify_ring(
    dap_sign_t *a_sign, 
    const void *a_data, 
    size_t a_data_size,
    dap_enc_key_t **a_ring_keys, 
    size_t a_ring_size
);
```

**Description**: Verifies a ring signature.

**Parameters**:
- `a_sign`: Ring signature to verify
- `a_data`: Original signed data
- `a_data_size`: Size of data in bytes
- `a_ring_keys`: Array of ring public keys
- `a_ring_size`: Number of keys in ring

**Returns**: 0 if valid, negative error code if invalid

### Utility Functions

#### `dap_enc_chipmunk_ring_get_signature_size()`

```c
size_t dap_enc_chipmunk_ring_get_signature_size(size_t a_ring_size);
```

**Description**: Calculates signature size for given ring size.

**Parameters**:
- `a_ring_size`: Number of participants in ring

**Returns**: Required signature buffer size in bytes

## Data Structures

### Key Sizes
- **Public Key**: 4,128 bytes (4.0KB)
- **Private Key**: 4,208 bytes (4.1KB)

### Signature Sizes
- **Ring size 2**: 12,552 bytes (12.3KB)
- **Ring size 16**: 13,896 bytes (13.6KB)
- **Ring size 64**: 18,504 bytes (18.1KB)

## Performance Characteristics

| Ring Size | Signing Time | Verification Time |
|-----------|--------------|-------------------|
| 2         | 0.4ms        | 0.0ms             |
| 16        | 0.6ms        | 0.2ms             |
| 64        | 1.4ms        | 0.7ms             |

## Error Codes

- `0`: Success
- `-EINVAL`: Invalid parameters
- `-ENOMEM`: Memory allocation failure
- `-EOVERFLOW`: Buffer overflow detected

## Security Considerations

1. **Key Management**: Store private keys securely and zero memory after use
2. **Ring Construction**: Ensure all ring members use valid ChipmunkRing keys
3. **Message Handling**: Hash large messages before signing
4. **Timing Attacks**: Implementation uses constant-time operations
5. **Memory Safety**: All allocations are bounds-checked

## Integration Example

```c
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>

// Initialize ChipmunkRing module
dap_enc_chipmunk_ring_init();

// Generate ring keys
const size_t ring_size = 16;
dap_enc_key_t *ring_keys[ring_size];
for (size_t i = 0; i < ring_size; i++) {
    ring_keys[i] = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0
    );
}

// Create ring signature
const char *message = "Anonymous transaction";
dap_sign_t *signature = dap_sign_create_ring(
    ring_keys[0],           // Signer's key
    message, strlen(message),
    ring_keys, ring_size,
    0                       // Signer index
);

// Verify signature
int result = dap_sign_verify_ring(
    signature, message, strlen(message),
    ring_keys, ring_size
);

// Cleanup
for (size_t i = 0; i < ring_size; i++) {
    dap_enc_key_delete(ring_keys[i]);
}
DAP_DELETE(signature);
```

## Build Requirements

- **Compiler**: GCC 9+ or Clang 10+
- **Build System**: CMake 3.10+
- **Dependencies**: DAP SDK core and crypto modules
- **Platform**: Linux, macOS, Windows (x86_64, ARM64)

## Testing

ChipmunkRing includes comprehensive test coverage:
- **Unit Tests**: 24 test suites covering all functionality
- **Integration Tests**: Network and consensus integration
- **Security Tests**: Zero-knowledge property validation
- **Performance Tests**: Detailed benchmarking framework

Run tests with:
```bash
make test_crypto
ctest -L chipmunk_ring
```
