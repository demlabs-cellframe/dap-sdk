"""
Unit tests for DAP SDK crypto module.
Tests all crypto operations with real DAP SDK functions.
"""

import pytest
from dap.crypto import (
    DapCryptoKey, DapKeyType, DapKeyError,
    DapSign, DapSignError, DapSignType,
    DapCert, DapCertError, DapCertType,
    DapHash, DapHashError, DapHashType,
    quick_sign, quick_verify, quick_hash_fast
)

# Test data
TEST_DATA = b"Hello, DAP!"
TEST_CERT_NAME = "test_cert"

@pytest.fixture
def dilithium_key():
    """Create DILITHIUM key for tests"""
    return DapCryptoKey(DapKeyType.DILITHIUM)

@pytest.fixture
def falcon_key():
    """Create FALCON key for tests"""
    return DapCryptoKey(DapKeyType.FALCON)

@pytest.fixture
def chipmunk_key():
    """Create CHIPMUNK key for tests"""
    return DapCryptoKey(DapKeyType.CHIPMUNK)

@pytest.fixture
def test_cert():
    """Create test certificate"""
    return DapCert.create(TEST_CERT_NAME)

def test_key_creation():
    """Test key creation with different types"""
    # Test all supported key types
    for key_type in DapKeyType:
        key = DapCryptoKey(key_type)
        assert key is not None
        assert key.key_type == key_type

def test_key_creation_from_seed():
    """Test deterministic key creation from seed"""
    seed = b"test_seed"
    key1 = DapCryptoKey(DapKeyType.DILITHIUM, seed)
    key2 = DapCryptoKey(DapKeyType.DILITHIUM, seed)
    
    # Same seed should produce same key
    sig1 = key1.sign(TEST_DATA)
    assert key2.verify(sig1, TEST_DATA)

def test_key_creation_invalid():
    """Test key creation with invalid parameters"""
    with pytest.raises(DapKeyError):
        DapCryptoKey("invalid_type")

def test_key_signing(dilithium_key):
    """Test signing with key"""
    # Test both bytes and string data
    for data in [TEST_DATA, TEST_DATA.decode()]:
        signature = dilithium_key.sign(data)
        assert signature is not None
        assert dilithium_key.verify(signature, data)

def test_key_verification(dilithium_key):
    """Test signature verification"""
    signature = dilithium_key.sign(TEST_DATA)
    
    # Valid signature should verify
    assert dilithium_key.verify(signature, TEST_DATA)
    
    # Invalid data should not verify
    assert not dilithium_key.verify(signature, b"wrong data")

def test_quick_sign_verify(dilithium_key):
    """Test quick signing helpers"""
    signature = quick_sign(dilithium_key, TEST_DATA)
    assert quick_verify(signature, dilithium_key, TEST_DATA)

def test_hash_creation():
    """Test hash creation"""
    # Test both bytes and string data
    for data in [TEST_DATA, TEST_DATA.decode()]:
        hash_obj = DapHash.create(data)
        assert hash_obj is not None

def test_quick_hash():
    """Test quick hash helper"""
    hash_obj = quick_hash_fast(TEST_DATA)
    assert hash_obj is not None

def test_cert_creation():
    """Test certificate creation"""
    cert = DapCert.create(TEST_CERT_NAME)
    assert cert is not None

def test_cert_signing(test_cert, dilithium_key):
    """Test certificate signing"""
    signature = test_cert.sign(TEST_DATA)
    assert signature is not None
    assert test_cert.verify(signature, TEST_DATA)

def test_cert_verification(test_cert, dilithium_key):
    """Test certificate verification"""
    signature = test_cert.sign(TEST_DATA)
    
    # Valid signature should verify
    assert test_cert.verify(signature, TEST_DATA)
    
    # Invalid data should not verify
    assert not test_cert.verify(signature, b"wrong data")

def test_cert_chain():
    """Test certificate chain"""
    chain = DapCertChain()
    cert1 = DapCert.create("cert1")
    cert2 = DapCert.create("cert2")
    
    chain.add_certificate(cert1)
    chain.add_certificate(cert2)
    
    # Test verification with chain
    signature = cert1.sign(TEST_DATA)
    assert chain.verify_chain(TEST_DATA, signature)

def test_cert_store():
    """Test certificate store"""
    store = DapCertStore()
    cert = DapCert.create(TEST_CERT_NAME)
    
    # Add certificate
    store.add_certificate(TEST_CERT_NAME, cert)
    assert store.get_certificate(TEST_CERT_NAME) == cert
    
    # Delete certificate
    store.delete_certificate(TEST_CERT_NAME)
    with pytest.raises(KeyError):
        store.get_certificate(TEST_CERT_NAME)

def test_key_manager():
    """Test key manager"""
    manager = DapKeyManager()
    
    # Create and store key
    key = manager.create_key("test_key", DapKeyType.DILITHIUM)
    assert manager.get_key("test_key") == key
    
    # Delete key
    manager.delete_key("test_key")
    with pytest.raises(KeyError):
        manager.get_key("test_key")

def test_signature_aggregator(dilithium_key):
    """Test signature aggregation"""
    aggregator = DapSignatureAggregator()
    
    # Add multiple signatures
    signature1 = quick_sign(dilithium_key, TEST_DATA)
    signature2 = quick_sign(dilithium_key, TEST_DATA)
    
    aggregator.add_signature(signature1, dilithium_key, TEST_DATA)
    aggregator.add_signature(signature2, dilithium_key, TEST_DATA)
    
    # Verify all signatures
    assert aggregator.verify_all()

def test_batch_verifier(dilithium_key):
    """Test batch signature verification"""
    verifier = DapBatchVerifier()
    
    # Add multiple signatures
    signature1 = quick_sign(dilithium_key, TEST_DATA)
    signature2 = quick_sign(dilithium_key, TEST_DATA)
    
    verifier.add_signature(signature1, dilithium_key, TEST_DATA)
    verifier.add_signature(signature2, dilithium_key, TEST_DATA)
    
    # Verify all signatures
    assert verifier.verify_all()

def test_context_managers():
    """Test context manager support"""
    # Test key context manager
    with DapCryptoKey(DapKeyType.DILITHIUM) as key:
        signature = key.sign(TEST_DATA)
        assert key.verify(signature, TEST_DATA)
    
    # Test certificate context manager
    with DapCert.create(TEST_CERT_NAME) as cert:
        signature = cert.sign(TEST_DATA)
        assert cert.verify(signature, TEST_DATA)
    
    # Test certificate chain context manager
    with DapCertChain() as chain:
        cert = DapCert.create(TEST_CERT_NAME)
        chain.add_certificate(cert)
        signature = cert.sign(TEST_DATA)
        assert chain.verify_chain(TEST_DATA, signature)
    
    # Test certificate store context manager
    with DapCertStore() as store:
        cert = DapCert.create(TEST_CERT_NAME)
        store.add_certificate(TEST_CERT_NAME, cert)
        assert store.get_certificate(TEST_CERT_NAME) == cert 