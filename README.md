# Python DAP SDK

Python bindings for DAP SDK with full cryptographic support.

## Features

- ✅ Complete cryptographic operations:
  - Post-quantum signatures (DILITHIUM, FALCON, PICNIC)
  - Multi-signature support (CHIPMUNK)
  - Certificate management
  - Fast hashing (KECCAK)
- ✅ Clean Python API:
  - Proper exception handling
  - Type hints
  - Context managers
  - No legacy fallbacks
- ✅ Production ready:
  - Comprehensive test suite
  - Full documentation
  - CI/CD integration

## Installation

```bash
# From PyPI
pip install python-dap

# From source
git clone https://gitlab.demlabs.net/dap/python-dap
cd python-dap
pip install -e .
```

## Quick Start

```python
from dap.crypto import DapCryptoKey, DapKeyType, quick_sign, quick_verify

# Create key
key = DapCryptoKey(DapKeyType.DILITHIUM)

# Sign data
data = b"Hello, DAP!"
signature = quick_sign(key, data)

# Verify signature
assert quick_verify(signature, key, data)
```

## Cryptographic Operations

### Key Management

```python
from dap.crypto import DapCryptoKey, DapKeyType, DapKeyManager

# Create key manager
manager = DapKeyManager()

# Create keys of different types
dilithium_key = manager.create_key("dilithium", DapKeyType.DILITHIUM)
falcon_key = manager.create_key("falcon", DapKeyType.FALCON)
chipmunk_key = manager.create_key("chipmunk", DapKeyType.CHIPMUNK)

# Use context manager for automatic cleanup
with DapCryptoKey(DapKeyType.DILITHIUM) as key:
    # Key will be automatically deleted after use
    signature = key.sign(b"data")
```

### Digital Signatures

```python
from dap.crypto import DapSign, DapSignatureAggregator, DapBatchVerifier

# Create signature
signature = DapSign.create(key, b"data")

# Verify signature
assert signature.verify(key, b"data")

# Aggregate multiple signatures
aggregator = DapSignatureAggregator()
aggregator.add_signature(signature1, key1, data)
aggregator.add_signature(signature2, key2, data)
assert aggregator.verify_all()

# Batch verify signatures
verifier = DapBatchVerifier()
verifier.add_signature(signature1, key1, data)
verifier.add_signature(signature2, key2, data)
assert verifier.verify_all()
```

### Certificates

```python
from dap.crypto import DapCert, DapCertChain, DapCertStore

# Create certificate
cert = DapCert.create("my_cert")

# Sign with certificate
signature = cert.sign(b"data")
assert cert.verify(signature, b"data")

# Create certificate chain
chain = DapCertChain()
chain.add_certificate(root_cert)
chain.add_certificate(intermediate_cert)
chain.add_certificate(end_cert)

# Verify through chain
assert chain.verify_chain(data, signature)

# Store certificates
store = DapCertStore()
store.add_certificate("cert1", cert1)
store.add_certificate("cert2", cert2)
```

### Hashing

```python
from dap.crypto import DapHash, quick_hash_fast

# Create hash
hash_obj = DapHash.create(b"data")

# Quick hash helper
hash_obj = quick_hash_fast(b"data")
```

## Development

```bash
# Install development dependencies
pip install -e ".[dev]"

# Run tests
pytest

# Run tests with coverage
pytest --cov=dap

# Run type checker
mypy dap

# Format code
black dap
isort dap
```

## Documentation

Full documentation is available at [docs.demlabs.net/python-dap/](https://docs.demlabs.net/python-dap/).

## License

This project is licensed under the GNU Affero General Public License v3 - see the [LICENSE](LICENSE) file for details.

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for details. 