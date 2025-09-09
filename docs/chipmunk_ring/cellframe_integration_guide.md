# ChipmunkRing Integration Guide for Cellframe

## Overview

This guide provides step-by-step instructions for integrating ChipmunkRing post-quantum ring signatures into Cellframe blockchain applications, enabling anonymous transactions with quantum-resistant security.

## Prerequisites

- DAP SDK with ChipmunkRing support
- Cellframe node environment
- C/C++ development tools
- Understanding of blockchain transaction structure

## Integration Steps

### 1. Enable ChipmunkRing Support

Add ChipmunkRing to your Cellframe build configuration:

```cmake
# CMakeLists.txt
set(DAP_MODULES ${DAP_MODULES} crypto)
add_definitions(-DDAP_CHIPMUNK_RING_ENABLED)
```

### 2. Initialize ChipmunkRing Module

```c
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>

// Initialize during node startup
int init_anonymous_transactions() {
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize ChipmunkRing");
        return -1;
    }
    
    log_it(L_INFO, "ChipmunkRing initialized for anonymous transactions");
    return 0;
}
```

### 3. Transaction Structure Modification

Extend Cellframe transaction structure to support ring signatures:

```c
typedef struct cellframe_tx_anonymous {
    // Standard transaction fields
    cellframe_tx_header_t header;
    
    // Ring signature specific fields
    uint32_t ring_size;                    // Number of participants
    uint8_t ring_hash[32];                 // Hash of ring public keys
    dap_sign_t *ring_signature;            // ChipmunkRing signature
    
    // Transaction payload
    uint8_t *transaction_data;
    size_t transaction_size;
} cellframe_tx_anonymous_t;
```

### 4. Anonymous Transaction Creation

```c
cellframe_tx_anonymous_t* create_anonymous_transaction(
    dap_enc_key_t *signer_key,
    dap_enc_key_t **ring_keys,
    size_t ring_size,
    const void *tx_data,
    size_t tx_size
) {
    // Allocate transaction structure
    cellframe_tx_anonymous_t *tx = DAP_NEW_Z(cellframe_tx_anonymous_t);
    
    // Set transaction header
    tx->header.type = CELLFRAME_TX_TYPE_ANONYMOUS;
    tx->header.timestamp = dap_time_now();
    tx->ring_size = ring_size;
    
    // Calculate ring hash for verification
    calculate_ring_hash(ring_keys, ring_size, tx->ring_hash);
    
    // Create ring signature
    tx->ring_signature = dap_sign_create_ring(
        signer_key, tx_data, tx_size,
        ring_keys, ring_size,
        find_signer_index(signer_key, ring_keys, ring_size)
    );
    
    if (!tx->ring_signature) {
        log_it(L_ERROR, "Failed to create ring signature");
        DAP_DELETE(tx);
        return NULL;
    }
    
    // Copy transaction data
    tx->transaction_size = tx_size;
    tx->transaction_data = DAP_NEW_SIZE(uint8_t, tx_size);
    memcpy(tx->transaction_data, tx_data, tx_size);
    
    return tx;
}
```

### 5. Anonymous Transaction Verification

```c
bool verify_anonymous_transaction(
    const cellframe_tx_anonymous_t *tx,
    dap_enc_key_t **ring_keys
) {
    // Verify transaction structure
    if (!tx || !tx->ring_signature || !ring_keys) {
        return false;
    }
    
    // Verify ring hash
    uint8_t calculated_hash[32];
    calculate_ring_hash(ring_keys, tx->ring_size, calculated_hash);
    if (memcmp(tx->ring_hash, calculated_hash, 32) != 0) {
        log_it(L_ERROR, "Ring hash mismatch");
        return false;
    }
    
    // Verify ring signature
    int result = dap_sign_verify_ring(
        tx->ring_signature,
        tx->transaction_data, tx->transaction_size,
        ring_keys, tx->ring_size
    );
    
    if (result != 0) {
        log_it(L_ERROR, "Ring signature verification failed");
        return false;
    }
    
    log_it(L_INFO, "Anonymous transaction verified successfully");
    return true;
}
```

### 6. Consensus Integration

#### Ring Formation

```c
typedef struct consensus_ring {
    dap_enc_key_t **public_keys;
    size_t size;
    uint8_t ring_hash[32];
    uint64_t formation_timestamp;
} consensus_ring_t;

consensus_ring_t* form_consensus_ring(
    cellframe_node_t **active_nodes,
    size_t node_count
) {
    if (node_count < 2 || node_count > 64) {
        log_it(L_ERROR, "Invalid ring size: %zu", node_count);
        return NULL;
    }
    
    consensus_ring_t *ring = DAP_NEW_Z(consensus_ring_t);
    ring->size = node_count;
    ring->public_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, node_count);
    
    // Collect public keys from active nodes
    for (size_t i = 0; i < node_count; i++) {
        ring->public_keys[i] = active_nodes[i]->signing_key;
    }
    
    // Calculate ring hash for verification
    calculate_ring_hash(ring->public_keys, ring->size, ring->ring_hash);
    ring->formation_timestamp = dap_time_now();
    
    log_it(L_INFO, "Formed consensus ring with %zu participants", ring->size);
    return ring;
}
```

#### Anonymous Voting

```c
bool submit_anonymous_vote(
    dap_enc_key_t *voter_key,
    const consensus_ring_t *ring,
    const char *proposal_id,
    bool vote_yes
) {
    // Prepare vote data
    vote_data_t vote = {
        .proposal_id_hash = hash_string(proposal_id),
        .vote = vote_yes ? 1 : 0,
        .timestamp = dap_time_now()
    };
    
    // Find voter position in ring
    size_t voter_index = find_key_index(voter_key, ring->public_keys, ring->size);
    if (voter_index == SIZE_MAX) {
        log_it(L_ERROR, "Voter key not found in consensus ring");
        return false;
    }
    
    // Create anonymous vote signature
    dap_sign_t *vote_signature = dap_sign_create_ring(
        voter_key,
        &vote, sizeof(vote),
        ring->public_keys, ring->size,
        voter_index
    );
    
    if (!vote_signature) {
        log_it(L_ERROR, "Failed to create anonymous vote signature");
        return false;
    }
    
    // Submit to consensus mechanism
    bool submitted = consensus_submit_anonymous_vote(
        proposal_id, vote_signature, ring
    );
    
    DAP_DELETE(vote_signature);
    return submitted;
}
```

## Performance Considerations

### Ring Size Selection

Choose ring size based on anonymity vs performance requirements:

| Ring Size | Anonymity Level | Signature Size | Performance |
|-----------|-----------------|----------------|-------------|
| 2-4       | Low             | 12.3-12.4KB    | Fastest     |
| 8-16      | Medium          | 12.8-13.6KB    | Good        |
| 32-64     | High            | 15.1-18.1KB    | Acceptable  |

### Memory Management

```c
// Efficient ring key management
typedef struct ring_cache {
    dap_enc_key_t **keys;
    size_t size;
    uint64_t last_update;
} ring_cache_t;

// Cache frequently used rings
static ring_cache_t *g_active_rings[MAX_CACHED_RINGS];
```

## Security Best Practices

### 1. Ring Formation Security

- Use cryptographically secure randomness for ring formation
- Verify all ring members are online and valid
- Rotate rings periodically to prevent correlation attacks

### 2. Key Management

```c
// Secure key storage
typedef struct secure_key_store {
    dap_enc_key_t *key;
    uint8_t key_hash[32];
    bool is_locked;
} secure_key_store_t;

// Zero sensitive memory
void secure_key_cleanup(secure_key_store_t *store) {
    if (store && store->key) {
        memset(store->key->priv_key_data, 0, store->key->priv_key_data_size);
        dap_enc_key_delete(store->key);
        memset(store, 0, sizeof(secure_key_store_t));
    }
}
```

### 3. Transaction Validation

```c
bool validate_anonymous_transaction_security(
    const cellframe_tx_anonymous_t *tx
) {
    // Check ring size limits
    if (tx->ring_size < 2 || tx->ring_size > 64) {
        return false;
    }
    
    // Verify signature size consistency
    size_t expected_size = dap_enc_chipmunk_ring_get_signature_size(tx->ring_size);
    if (tx->ring_signature->header.sign_size != expected_size) {
        return false;
    }
    
    // Check timestamp validity
    uint64_t current_time = dap_time_now();
    if (tx->header.timestamp > current_time + MAX_FUTURE_OFFSET) {
        return false;
    }
    
    return true;
}
```

## Deployment Checklist

### Pre-deployment

- [ ] Verify all tests pass (26/26)
- [ ] Run performance benchmarks
- [ ] Security audit completed
- [ ] Memory leak testing with Valgrind
- [ ] Cross-platform compatibility verified

### Network Deployment

- [ ] Configure ring size parameters
- [ ] Set up ring formation protocols
- [ ] Deploy consensus rule updates
- [ ] Monitor transaction processing performance
- [ ] Implement rollback procedures

### Monitoring

```c
typedef struct chipmunk_ring_metrics {
    uint64_t signatures_created;
    uint64_t signatures_verified;
    uint64_t verification_failures;
    double avg_signing_time_ms;
    double avg_verification_time_ms;
} chipmunk_ring_metrics_t;

// Monitor performance in production
void log_performance_metrics(const chipmunk_ring_metrics_t *metrics) {
    log_it(L_INFO, "ChipmunkRing metrics: created=%lu, verified=%lu, failed=%lu",
           metrics->signatures_created, metrics->signatures_verified, 
           metrics->verification_failures);
    log_it(L_INFO, "Performance: sign=%.2fms, verify=%.2fms",
           metrics->avg_signing_time_ms, metrics->avg_verification_time_ms);
}
```

## Troubleshooting

### Common Issues

1. **Memory allocation failures**: Increase system memory or reduce ring size
2. **Signature verification failures**: Check ring key consistency
3. **Performance degradation**: Monitor ring size and optimize accordingly
4. **Integration errors**: Verify DAP SDK version compatibility

### Debug Mode

Enable detailed logging for debugging:

```c
// Enable debug mode (development only)
extern bool s_debug_more;
s_debug_more = true;  // Enable detailed ChipmunkRing logging
```

## Support

For technical support and questions:
- Documentation: [DAP SDK Documentation]
- Issues: [GitHub Issues]
- Community: [Cellframe Developer Forum]
- Email: support@cellframe.net
