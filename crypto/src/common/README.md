# Post-Quantum Cryptography Common Modules

## 🔐 Overview

This directory contains the unified common modules for post-quantum cryptographic algorithms in the Cellframe SDK. These modules eliminate code duplication across Kyber, Falcon, Chipmunk, and Dilithium implementations by providing a unified API while maintaining algorithm-specific optimizations.

## 📊 Code Deduplication Goals

- **Target**: Reduce code duplication by 70%+ across all post-quantum algorithms
- **Current**: ~70% duplication in polynomial arithmetic, NTT, reduction, and hash functions
- **Goal**: Achieve ~20% duplication through unified modules
- **Performance**: Maintain or improve performance (max 5% degradation)
- **Security**: Preserve constant-time operations and cryptographic security

## 🏗️ Architecture

### Module Structure

```
common/
├── poly/                    # Polynomial arithmetic
│   └── pq_common_poly.h    # Unified polynomial interface
├── ntt/                     # Number Theoretic Transform
│   └── pq_common_ntt.h     # Unified NTT interface
├── reduce/                  # Modular reduction
│   └── pq_common_reduce.h  # Unified reduction interface
├── hash/                    # Hash functions
│   └── pq_common_hash.h    # Unified hash interface
├── pq_common.h             # Main header (includes all modules)
└── README.md               # This file
```

### Algorithm Support

| Algorithm | Polynomial Degree | Modulus | Coefficient Type | Hash Function |
|-----------|------------------|---------|------------------|---------------|
| Kyber-512 | 256 | 3329 | int16_t | SHAKE128 |
| Kyber-768 | 256 | 3329 | int16_t | SHAKE256 |
| Kyber-1024 | 256 | 3329 | int16_t | SHAKE256 |
| Falcon-512 | 512 | 12289 | double | SHAKE256 |
| Falcon-1024 | 1024 | 12289 | double | SHAKE256 |
| Chipmunk | 512 | 3168257 | int32_t | SHAKE256 |
| Dilithium-2 | 256 | 8380417 | int32_t | SHAKE256 |
| Dilithium-3 | 256 | 8380417 | int32_t | SHAKE256 |
| Dilithium-5 | 256 | 8380417 | int32_t | SHAKE256 |

## 🔧 Usage

### Basic Usage

```c
#include "common/pq_common.h"

// Initialize contexts for Kyber-768
pq_poly_t poly;
pq_ntt_ctx_t ntt_ctx;
pq_reduce_ctx_t reduce_ctx;
pq_hash_ctx_t hash_ctx;

pq_poly_init(&poly, PQ_ALG_KYBER_768);
pq_ntt_init(&ntt_ctx, PQ_ALG_KYBER_768);
pq_reduce_init(&reduce_ctx, PQ_ALG_KYBER_768);
pq_hash_init(&hash_ctx, PQ_ALG_KYBER_768, PQ_HASH_SHAKE256, 32);

// Generate uniform polynomial
uint8_t seed[32] = {0};
uint16_t nonce = 0;
pq_poly_uniform(&poly, seed, 32, nonce);

// Transform to NTT domain
pq_ntt_forward(&ntt_ctx, &poly);

// Reduce coefficients
pq_reduce_poly(&reduce_ctx, &poly, PQ_REDUCE_BARRETT);

// Clean up
pq_poly_free(&poly);
pq_ntt_free(&ntt_ctx);
pq_reduce_free(&reduce_ctx);
pq_hash_free(&hash_ctx);
```

### Advanced Usage with Automatic Operations

```c
// Initialize all modules at once
pq_poly_t poly;
pq_ntt_ctx_t ntt_ctx;
pq_reduce_ctx_t reduce_ctx;
pq_hash_ctx_t hash_ctx;

pq_common_init_all(PQ_ALG_KYBER_768, &poly, &ntt_ctx, &reduce_ctx, &hash_ctx,
                   PQ_HASH_SHAKE256, 32);

// Use automatic operations
pq_common_uniform_poly(&poly, &ntt_ctx, &hash_ctx, seed, 32, nonce);
pq_common_poly_mul(&result, &a, &b, &ntt_ctx, &reduce_ctx);

// Clean up all modules
pq_common_free_all(&poly, &ntt_ctx, &reduce_ctx, &hash_ctx);
```

## 📋 Module Details

### Polynomial Arithmetic (`poly/`)

**Purpose**: Unified polynomial operations across all algorithms

**Key Features**:
- Algorithm-specific parameter handling
- Coefficient type abstraction (int16_t, int32_t, double)
- Automatic memory management
- Constant-time operations where required

**Common Operations**:
- `pq_poly_init()` / `pq_poly_free()` - Memory management
- `pq_poly_add()` / `pq_poly_sub()` - Arithmetic operations
- `pq_poly_uniform()` / `pq_poly_noise()` - Random generation
- `pq_poly_pack()` / `pq_poly_unpack()` - Serialization

### NTT Transform (`ntt/`)

**Purpose**: Unified Number Theoretic Transform operations

**Key Features**:
- Algorithm-specific zeta values and parameters
- Montgomery and Barrett reduction support
- Optimized implementations for different platforms
- Vectorized operations where available

**Common Operations**:
- `pq_ntt_forward()` / `pq_ntt_inverse()` - Transform operations
- `pq_ntt_pointwise_mul()` - Multiplication in NTT domain
- `pq_ntt_basemul()` - Base multiplication for NTT
- `pq_ntt_optimized()` - Platform-specific optimizations

### Modular Reduction (`reduce/`)

**Purpose**: Unified modular reduction operations

**Key Features**:
- Multiple reduction methods (Barrett, Montgomery, Center, Freeze)
- Algorithm-specific parameter optimization
- Constant-time operations
- Vectorized implementations

**Common Operations**:
- `pq_reduce_barrett()` - Barrett reduction
- `pq_reduce_montgomery()` - Montgomery reduction
- `pq_reduce_center()` - Center around 0
- `pq_reduce_freeze()` - Final canonical form

### Hash Functions (`hash/`)

**Purpose**: Unified hash function operations

**Key Features**:
- Support for SHAKE, SHA2, SHA3, and BLAKE2
- Algorithm-specific hash function selection
- Optimized implementations
- Hash-based random number generation

**Common Operations**:
- `pq_hash_shake128()` / `pq_hash_shake256()` - SHAKE functions
- `pq_hash_sha2_256()` / `pq_hash_sha2_512()` - SHA2 functions
- `pq_hash_uniform_poly()` - Generate uniform polynomials
- `pq_hash_noise_poly()` - Generate noise polynomials

## 🔄 Migration Guide

### Phase 1: Analysis (Completed)
- ✅ Analyzed existing implementations
- ✅ Identified common patterns and code duplication
- ✅ Designed unified interfaces

### Phase 2: Implementation (Current)
- ✅ Created common module headers
- 🔄 Implementing common module source files
- 🔄 Creating algorithm-specific adapters

### Phase 3: Integration (Next)
- 🔄 Update Kyber implementation to use common modules
- 🔄 Update Falcon implementation to use common modules
- 🔄 Update Chipmunk implementation to use common modules
- 🔄 Update Dilithium implementation to use common modules

### Phase 4: Testing and Optimization (Final)
- 🔄 Comprehensive testing across all algorithms
- 🔄 Performance benchmarking and optimization
- 🔄 Security validation

## 🧪 Testing Strategy

### Unit Tests
- Individual module functionality
- Algorithm-specific parameter handling
- Edge cases and boundary conditions

### Integration Tests
- End-to-end algorithm functionality
- Cross-algorithm compatibility
- Performance regression testing

### Security Tests
- Constant-time operation validation
- Known answer tests against reference implementations
- Side-channel attack resistance

## 📊 Performance Metrics

### Targets
- **Code Duplication**: Reduce from 70% to 20%
- **Performance**: Max 5% degradation
- **Binary Size**: 10% reduction
- **Test Coverage**: 90%+

### Monitoring
- Automated performance regression detection
- Cross-platform benchmarking
- Memory usage analysis

## 🔒 Security Considerations

### Implementation Requirements
- **Constant-time operations**: All cryptographic operations must be constant-time
- **Memory security**: Secure allocation and zeroing of sensitive data
- **Side-channel resistance**: Protection against timing and cache attacks
- **Algorithm compatibility**: Maintain exact compatibility with existing implementations

### Validation
- Known answer tests against standard test vectors
- Cross-reference with reference implementations
- Formal verification where applicable

## 🤝 Contributing

### Development Workflow
1. **Review**: Understand the existing algorithm implementations
2. **Design**: Follow the unified interface patterns
3. **Implement**: Create algorithm-specific adapters
4. **Test**: Ensure compatibility and performance
5. **Document**: Update documentation and examples

### Code Standards
- Follow existing code style and conventions
- Maintain constant-time security properties
- Add comprehensive error handling
- Include performance optimizations where appropriate

## 📚 References

### Algorithm Specifications
- **Kyber**: [NIST PQC Round 3 Submission](https://pq-crystals.org/kyber/)
- **Falcon**: [NIST PQC Round 3 Submission](https://falcon-sign.info/)
- **Chipmunk**: [Cellframe Implementation](https://gitlab.demlabs.net/cellframe)
- **Dilithium**: [NIST PQC Round 3 Submission](https://pq-crystals.org/dilithium/)

### Technical References
- **NTT**: Number Theoretic Transform for polynomial multiplication
- **Barrett Reduction**: Efficient modular reduction
- **Montgomery Reduction**: Alternative modular reduction method
- **SHAKE**: Extensible output function based on SHA3

---

*Created: 2025-06-23*  
*Version: 1.0.0*  
*Status: Implementation Phase* 