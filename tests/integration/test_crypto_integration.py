"""
Integration tests for DAP SDK crypto module.
Tests crypto operations in real-world scenarios.
"""

import pytest
import os
from pathlib import Path
from dap.crypto import (
    DapCryptoKey, DapKeyType,
    DapSign, DapSignType,
    DapCert, DapCertType,
    DapHash, DapHashType,
    DapKeyManager, DapCertStore,
    DapSignatureAggregator, DapBatchVerifier,
    quick_sign, quick_verify, quick_hash_fast
)

# Test data
TEST_DATA = b"Integration test data"
TEST_LARGE_DATA = os.urandom(1024 * 1024)  # 1MB of random data
TEST_CERT_NAME = "test_integration_cert"

@pytest.fixture
def key_manager():
    """Create key manager for tests"""
    return DapKeyManager()

@pytest.fixture
def cert_store():
    """Create certificate store for tests"""
    return DapCertStore()

def test_full_signing_flow(key_manager):
    """Test complete signing flow with key management"""
    # Create keys of different types
    dilithium_key = key_manager.create_key("dilithium", DapKeyType.DILITHIUM)
    falcon_key = key_manager.create_key("falcon", DapKeyType.FALCON)
    chipmunk_key = key_manager.create_key("chipmunk", DapKeyType.CHIPMUNK)
    
    # Sign data with each key
    signatures = {
        "dilithium": quick_sign(dilithium_key, TEST_DATA),
        "falcon": quick_sign(falcon_key, TEST_DATA),
        "chipmunk": quick_sign(chipmunk_key, TEST_DATA)
    }
    
    # Verify all signatures
    assert quick_verify(signatures["dilithium"], dilithium_key, TEST_DATA)
    assert quick_verify(signatures["falcon"], falcon_key, TEST_DATA)
    assert quick_verify(signatures["chipmunk"], chipmunk_key, TEST_DATA)
    
    # Cross-verification should fail
    assert not quick_verify(signatures["dilithium"], falcon_key, TEST_DATA)
    assert not quick_verify(signatures["falcon"], chipmunk_key, TEST_DATA)
    assert not quick_verify(signatures["chipmunk"], dilithium_key, TEST_DATA)

def test_certificate_chain_validation(cert_store):
    """Test certificate chain validation"""
    # Create root and intermediate certificates
    root_cert = DapCert.create("root")
    intermediate_cert = DapCert.create("intermediate")
    end_cert = DapCert.create("end")
    
    # Store certificates
    cert_store.add_certificate("root", root_cert)
    cert_store.add_certificate("intermediate", intermediate_cert)
    cert_store.add_certificate("end", end_cert)
    
    # Create certificate chain
    chain = DapCertChain()
    chain.add_certificate(root_cert)
    chain.add_certificate(intermediate_cert)
    chain.add_certificate(end_cert)
    
    # Sign data with end certificate
    signature = end_cert.sign(TEST_DATA)
    
    # Verify through chain
    assert chain.verify_chain(TEST_DATA, signature)

def test_batch_signature_verification(key_manager):
    """Test batch signature verification"""
    # Create multiple keys
    keys = [
        key_manager.create_key(f"key_{i}", DapKeyType.DILITHIUM)
        for i in range(10)
    ]
    
    # Create batch verifier
    verifier = DapBatchVerifier()
    
    # Add multiple signatures to batch
    for key in keys:
        signature = quick_sign(key, TEST_DATA)
        verifier.add_signature(signature, key, TEST_DATA)
    
    # Verify all signatures in batch
    assert verifier.verify_all()

def test_signature_aggregation(key_manager):
    """Test signature aggregation"""
    # Create multiple keys
    keys = [
        key_manager.create_key(f"key_{i}", DapKeyType.CHIPMUNK)
        for i in range(5)
    ]
    
    # Create aggregator
    aggregator = DapSignatureAggregator()
    
    # Add multiple signatures
    for key in keys:
        signature = quick_sign(key, TEST_DATA)
        aggregator.add_signature(signature, key, TEST_DATA)
    
    # Verify aggregated signatures
    assert aggregator.verify_all()

def test_large_data_operations(key_manager):
    """Test crypto operations with large data"""
    # Create key
    key = key_manager.create_key("large_data_key", DapKeyType.DILITHIUM)
    
    # Test signing large data
    signature = quick_sign(key, TEST_LARGE_DATA)
    assert quick_verify(signature, key, TEST_LARGE_DATA)
    
    # Test hashing large data
    hash_obj = quick_hash_fast(TEST_LARGE_DATA)
    assert hash_obj is not None

def test_key_persistence(tmp_path):
    """Test key persistence and recovery"""
    # Create key with seed for deterministic generation
    seed = b"persistent_test_seed"
    key1 = DapCryptoKey(DapKeyType.DILITHIUM, seed)
    
    # Sign data
    signature1 = quick_sign(key1, TEST_DATA)
    
    # Create new key with same seed
    key2 = DapCryptoKey(DapKeyType.DILITHIUM, seed)
    
    # Verify signature with recovered key
    assert quick_verify(signature1, key2, TEST_DATA)

def test_certificate_operations(cert_store):
    """Test comprehensive certificate operations"""
    # Create certificates
    cert1 = DapCert.create("cert1")
    cert2 = DapCert.create("cert2")
    
    # Store certificates
    cert_store.add_certificate("cert1", cert1)
    cert_store.add_certificate("cert2", cert2)
    
    # Sign same data with both certificates
    signature1 = cert1.sign(TEST_DATA)
    signature2 = cert2.sign(TEST_DATA)
    
    # Verify signatures
    assert cert1.verify(signature1, TEST_DATA)
    assert cert2.verify(signature2, TEST_DATA)
    
    # Cross verification should fail
    assert not cert1.verify(signature2, TEST_DATA)
    assert not cert2.verify(signature1, TEST_DATA)

def test_context_manager_cleanup():
    """Test proper cleanup with context managers"""
    # Test key cleanup
    with DapCryptoKey(DapKeyType.DILITHIUM) as key:
        signature = quick_sign(key, TEST_DATA)
        assert quick_verify(signature, key, TEST_DATA)
    
    # Test certificate cleanup
    with DapCert.create(TEST_CERT_NAME) as cert:
        signature = cert.sign(TEST_DATA)
        assert cert.verify(signature, TEST_DATA)
    
    # Test chain cleanup
    with DapCertChain() as chain:
        with DapCert.create("chain_cert") as cert:
            chain.add_certificate(cert)
            signature = cert.sign(TEST_DATA)
            assert chain.verify_chain(TEST_DATA, signature)
    
    # Test store cleanup
    with DapCertStore() as store:
        with DapCert.create("store_cert") as cert:
            store.add_certificate("store_cert", cert)
            retrieved = store.get_certificate("store_cert")
            assert retrieved == cert 