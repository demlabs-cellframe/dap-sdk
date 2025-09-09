# ChipmunkRing: Post-Quantum Ring Signatures for Blockchain

## Overview

ChipmunkRing is the first sufficiently compact and performant post-quantum ring signature scheme designed specifically for blockchain applications. Built on lattice-based cryptography, it provides quantum-resistant anonymous signatures with practical performance characteristics.

## Key Features

🔐 **Post-Quantum Security**: 112-bit quantum resistance based on Ring-LWE  
⚡ **High Performance**: Sub-millisecond signing and verification  
📦 **Compact Signatures**: 12.3-18.1KB signatures (5-8× smaller than alternatives)  
🔗 **Blockchain Ready**: Optimized for consensus mechanisms and transaction processing  
🛡️ **Anonymous**: Perfect anonymity among ring participants  
✅ **Production Ready**: 100% test coverage, memory-safe implementation  

## Performance Metrics

| Ring Size | Signature Size | Signing Time | Verification Time |
|-----------|----------------|--------------|-------------------|
| 2         | 12.3KB         | 0.4ms        | 0.0ms             |
| 16        | 13.6KB         | 0.6ms        | 0.2ms             |
| 64        | 18.1KB         | 1.4ms        | 0.7ms             |

*Measurements on x86_64 with GCC -O3 optimization*

## Quick Start

### Installation

ChipmunkRing is integrated into the DAP SDK. Build with:

```bash
git clone https://github.com/demlabs-cellframe/dap-sdk.git
cd dap-sdk
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Basic Usage

```c
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>

// Initialize
dap_enc_chipmunk_ring_init();

// Generate ring keys
const size_t ring_size = 16;
dap_enc_key_t *ring_keys[ring_size];
for (size_t i = 0; i < ring_size; i++) {
    ring_keys[i] = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0
    );
}

// Create anonymous signature
const char *message = "Anonymous blockchain transaction";
dap_sign_t *signature = dap_sign_create_ring(
    ring_keys[0],                    // Signer's key
    message, strlen(message),        // Message to sign
    ring_keys, ring_size,            // Ring of participants
    0                                // Signer index (hidden)
);

// Verify signature (anonymously)
int result = dap_sign_verify_ring(
    signature, message, strlen(message),
    ring_keys, ring_size
);

// result == 0 means valid signature, but signer identity remains hidden
```

## Documentation

📚 **[API Reference](api_reference.md)** - Complete API documentation  
🔗 **[Cellframe Integration Guide](cellframe_integration_guide.md)** - Blockchain integration  
🔬 **[Technical Specification](technical_specification.md)** - Implementation details  
📄 **[Scientific Paper](../papers/chipmunk_ring_scientific_paper.tex)** - Academic publication  

## Architecture

```
ChipmunkRing Architecture
├── Core Algorithm (chipmunk_ring.c)
│   ├── Zero-Knowledge Proofs
│   ├── Fiat-Shamir Transform
│   └── Ring Verification Logic
├── DAP SDK Integration (dap_enc_chipmunk_ring.c)
│   ├── Key Management
│   ├── Signature API
│   └── Memory Management
└── Test Framework
    ├── Unit Tests (24 suites)
    ├── Integration Tests
    ├── Security Tests
    └── Performance Benchmarks
```

## Security Properties

### Anonymity
- **Perfect anonymity** among ring participants
- **Unlinkability** of signatures from same signer
- **Forward secrecy** with key rotation

### Unforgeability
- **Existential unforgeability** under chosen message attack
- **Ring-LWE reduction** for quantum resistance
- **Fiat-Shamir soundness** in random oracle model

### Optional Features
- **Linkability tags** for double-spending prevention
- **Threshold variants** for advanced protocols
- **Batch verification** for performance optimization

## Comparison with Alternatives

| Feature | Classical LSAG | Lattice-RS | Hash-RS | ChipmunkRing |
|---------|----------------|------------|---------|--------------|
| Quantum Secure | ❌ | ✅ | ✅ | ✅ |
| Signature Size | 1KB | >100KB | >200KB | **12-18KB** |
| Signing Time | 10ms | >1000ms | >500ms | **0.4-1.4ms** |
| Verification Time | 5ms | >500ms | >200ms | **0.0-0.7ms** |
| Blockchain Ready | ✅ | ❌ | ❌ | **✅** |

## Use Cases

### Anonymous Transactions
```c
// Create anonymous payment
create_anonymous_payment(
    sender_key,           // Hidden among ring
    recipient_address,    // Public destination
    amount,              // Transaction amount
    ring_participants,   // Anonymity set
    ring_size            // Anonymity level
);
```

### Private Voting
```c
// Submit anonymous vote
submit_anonymous_vote(
    voter_key,           // Hidden identity
    proposal_id,         // Public proposal
    vote_choice,         // Yes/No vote
    eligible_voters,     // Voter anonymity set
    voter_count          // Participation level
);
```

### Confidential Consensus
```c
// Anonymous consensus participation
participate_in_consensus(
    validator_key,       // Hidden validator
    consensus_round,     // Round number
    proposed_block,      // Block to validate
    validator_set,       // Active validators
    validator_count      // Network size
);
```

## Development

### Building Tests
```bash
cd build
make test_unit_crypto_chipmunk_ring_basic      # Basic functionality
make test_unit_crypto_chipmunk_ring_performance # Performance benchmarks
make test_security_ring_zkp                    # Security validation
make test_integration_crypto_network           # Network integration
```

### Running Benchmarks
```bash
# Performance analysis
./tests/bin/test_unit_crypto_chipmunk_ring_performance

# Security validation  
./tests/bin/test_security_ring_zkp

# Integration testing
./tests/bin/test_integration_crypto_network
```

### Debug Mode
```c
// Enable detailed logging (development only)
extern bool s_debug_more;
s_debug_more = true;
```

## Contributing

### Code Standards
- Follow DAP SDK coding standards
- All functions must have doxygen documentation
- Comprehensive error handling required
- Memory safety is mandatory

### Testing Requirements
- Unit tests for all new functionality
- Performance regression testing
- Security property validation
- Cross-platform compatibility

### Security Review
- Cryptographic changes require expert review
- Formal verification for critical components
- Side-channel analysis for timing-sensitive code

## License

ChipmunkRing is released under the GNU General Public License v3.0. See LICENSE file for details.

## Support

- **Documentation**: [DAP SDK Documentation](https://docs.cellframe.net)
- **Issues**: [GitHub Issues](https://github.com/demlabs-cellframe/dap-sdk/issues)
- **Community**: [Cellframe Developer Forum](https://forum.cellframe.net)
- **Email**: research@cellframe.net

## Citation

If you use ChipmunkRing in academic work, please cite:

```bibtex
@article{chipmunkring2025,
  title={ChipmunkRing: A Practical Post-Quantum Ring Signature Scheme for Blockchain Applications},
  author={Cellframe Development Team},
  journal={IACR Cryptology ePrint Archive},
  year={2025},
  note={Implementation available at https://github.com/demlabs-cellframe/dap-sdk}
}
```

## Acknowledgments

Special thanks to the cryptographic research community and the Cellframe development team for their contributions to post-quantum blockchain security.
