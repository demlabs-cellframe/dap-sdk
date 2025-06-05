# Chipmunk: Better Synchronized Multi-Signatures from Lattices

## Overview

Chipmunk is a lattice-based multi-signature scheme designed for synchronized settings, particularly suitable for blockchain applications like Ethereum. It allows for compressing multiple signatures for the same message into a single aggregated signature while maintaining security against quantum adversaries.

## Key Features

- **Quantum-resistant**: Based on lattice cryptography (Short Integer Solution problem)
- **Non-interactive aggregation**: No interaction required between signers
- **Rogue-key attack resistant**: Secure against malicious key generation
- **Synchronized setting**: Optimized for blockchain consensus where signers are naturally synchronized
- **Compact signatures**: Significantly smaller than previous lattice-based constructions

## Technical Specifications

### Security Parameters

The scheme supports two main security levels:
- **112-bit security**: Suitable for most applications
- **128-bit security**: Higher security for critical applications

### Parameter Sets (n=512)

#### 112-bit Security Level
| τ | ρ | Aggregate Signatures | Size |
|---|---|---------------------|------|
| 21 | 1024 | 1,024 | 118 KB |
| 21 | 8192 | 8,192 | 136 KB |
| 21 | 131072 | 131,072 | 159 KB |

#### 128-bit Security Level
| τ | ρ | Aggregate Signatures | Size |
|---|---|---------------------|------|
| 21 | 1024 | 1,024 | 120 KB |
| 21 | 8192 | 8,192 | 168 KB |
| 21 | 131072 | 131,072 | 197 KB |

## Algorithm Components

### 1. Key Generation
- Generates public/private key pairs for each signer
- Uses lattice-based cryptographic primitives
- Resistant to rogue-key attacks

### 2. Signing Process
- Takes message and time parameter as input
- Produces individual signatures that can be aggregated
- Uses rejection sampling for security

### 3. Signature Aggregation
- Non-interactive process
- Combines multiple signatures into single aggregate
- Maintains verification properties

### 4. Verification
- Verifies aggregate signatures against multiple public keys
- Ensures all signers participated in signing process
- Quantum-resistant verification algorithm

## Implementation Details

### Core Functions

#### Key Generation
```c
// Generate key pair for signer
int chipmunk_keygen(chipmunk_public_key_t *pk, 
                   chipmunk_private_key_t *sk);
```

#### Individual Signing
```c
// Sign message with time parameter
int chipmunk_sign(chipmunk_signature_t *sig,
                 const uint8_t *message,
                 size_t message_len,
                 uint32_t time_step,
                 const chipmunk_private_key_t *sk);
```

#### Signature Aggregation
```c
// Aggregate multiple signatures
int chipmunk_aggregate(chipmunk_aggregate_signature_t *agg_sig,
                      const chipmunk_signature_t *signatures,
                      size_t num_signatures);
```

#### Verification
```c
// Verify aggregate signature
int chipmunk_verify(const chipmunk_aggregate_signature_t *agg_sig,
                   const uint8_t *message,
                   size_t message_len,
                   uint32_t time_step,
                   const chipmunk_public_key_t *public_keys,
                   size_t num_keys);
```

## Mathematical Foundation

### Ring-SIS Problem
The security is based on the Ring Short Integer Solution (Ring-SIS) problem over polynomial rings:
- Ring dimension: n = 512
- Modulus: q (varies by parameter set)
- Polynomial degree bound: β

### NTT Operations
- Uses Number Theoretic Transform for efficient polynomial operations
- Optimized for specific moduli and ring structures
- Critical for performance in signing and verification

### Hash Functions
- Uses cryptographic hash functions for Fiat-Shamir transform
- Ensures non-interactive zero-knowledge proofs
- Provides binding to message content

## Performance Characteristics

### Signature Sizes
- Individual signature: ~1-2 KB
- Aggregate signature (8192 signers): ~136-168 KB
- 5.6× improvement over previous constructions

### Computational Complexity
- Key generation: O(n log n)
- Signing: O(n log n)
- Aggregation: O(k) where k is number of signatures
- Verification: O(n log n)

## Security Properties

### Quantum Resistance
- Based on lattice problems believed hard for quantum computers
- No known polynomial-time quantum algorithms
- Future-proof against quantum adversaries

### Rogue-Key Resistance
- Prevents malicious signers from forging on behalf of honest parties
- No need for proof-of-possession protocols
- Secure key aggregation

### Unforgeability
- Existential unforgeability under chosen message attacks
- Security reduction to Ring-SIS problem
- Tight security bounds

## Blockchain Integration

### Consensus Applications
- Suitable for proof-of-stake consensus
- Efficient validator signature aggregation
- Reduced on-chain storage requirements

### Ethereum Compatibility
- Designed with Ethereum 2.0 in mind
- Compatible with existing validator infrastructure
- Supports large validator sets (>100k validators)

## Implementation Notes

### Memory Requirements
- Moderate memory footprint
- Efficient polynomial representations
- Optimized for embedded systems

### Timing Considerations
- Constant-time operations where possible
- Side-channel attack resistance
- Deterministic execution paths

### Error Handling
- Robust error detection and reporting
- Graceful degradation on parameter mismatches
- Comprehensive input validation

## References

1. Fleischhacker, N., Herold, G., Simkin, M., Zhang, Z. (2023). "Chipmunk: Better Synchronized Multi-Signatures from Lattices". IACR ePrint Archive, Report 2023/1820.

2. Boneh, D., Lynn, B., Shacham, H. (2001). "Short signatures from the Weil pairing". ASIACRYPT 2001.

3. Lyubashevsky, V. (2012). "Lattice signatures without trapdoors". EUROCRYPT 2012.

## License and Usage

This implementation follows the specifications from the academic paper and is intended for research and production use in quantum-resistant cryptographic applications.

---

*This documentation is based on the research paper "Chipmunk: Better Synchronized Multi-Signatures from Lattices" by Fleischhacker, Herold, Simkin, and Zhang (2023).* 