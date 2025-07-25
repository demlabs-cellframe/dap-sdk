"""
Integration tests for DAP SDK crypto module.
Tests crypto operations in real-world scenarios using existing API.
"""

import pytest
import os
from pathlib import Path
from dap.crypto import (
    DapKey, DapKeyType,
    DapSign, DapSignType,
    DapCert, DapHash
)

# Test data
TEST_DATA = b"Integration test data"
TEST_LARGE_DATA = os.urandom(1024 * 1024)  # 1MB of random data
TEST_CERT_NAME = "test_integration_cert"

@pytest.fixture
def test_keys():
    """Create test keys for integration tests"""
    return {
        'dilithium': DapKey(),  # Default DILITHIUM
        'falcon': DapKey(DapKeyType.FALCON),
        'chipmunk': DapKey(DapKeyType.CHIPMUNK)
    }

def test_full_signing_flow(test_keys):
    """Test complete signing flow with different key types"""
    # Sign data with each key type
    signatures = {}
    for key_name, key in test_keys.items():
        signatures[key_name] = DapSign(TEST_DATA, pvt_key=key)
    
    # Verify all signatures with their respective keys
    for key_name, signature in signatures.items():
        assert signature.verify(TEST_DATA, test_keys[key_name])
    
    # Cross-verification should fail (signature from one key shouldn't verify with another)
    assert not signatures["dilithium"].verify(TEST_DATA, test_keys["falcon"])
    assert not signatures["falcon"].verify(TEST_DATA, test_keys["chipmunk"])
    assert not signatures["chipmunk"].verify(TEST_DATA, test_keys["dilithium"])

def test_certificate_operations():
    """Test certificate creation and signing operations"""
    # Create certificate with default DILITHIUM
    cert = DapCert.create(TEST_CERT_NAME)
    assert cert is not None
    
    # Test certificate signing
    cert_signature = DapSign(TEST_DATA, pvt_cert=cert)
    assert cert_signature is not None
    
    # Verify signature with certificate
    assert cert_signature.verify(TEST_DATA, cert)

def test_batch_signature_verification():
    """Test verification of multiple signatures"""
    # Create multiple DILITHIUM keys (default)
    keys = [DapKey() for _ in range(10)]
    
    # Create multiple signatures
    signatures = []
    for key in keys:
        signature = DapSign(TEST_DATA, pvt_key=key)
        signatures.append((signature, key))
    
    # Verify all signatures individually
    for signature, key in signatures:
        assert signature.verify(TEST_DATA, key)

def test_signature_aggregation():
    """Test CHIPMUNK aggregated signatures"""
    # Create multiple CHIPMUNK keys for aggregation
    chipmunk_keys = [DapKey(DapKeyType.CHIPMUNK) for _ in range(5)]
    
    # Create aggregated signature
    aggregated_signature = DapSign(TEST_DATA, pvt_keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
    
    # Verify aggregated signature
    assert aggregated_signature.verify(TEST_DATA)

def test_large_data_operations():
    """Test crypto operations with large data"""
    # Create DILITHIUM key (default)
    key = DapKey()
    
    # Test signing large data
    signature = DapSign(TEST_LARGE_DATA, pvt_key=key)
    assert signature.verify(TEST_LARGE_DATA, key)
    
    # Test hashing large data
    hash_obj = DapHash(TEST_LARGE_DATA)
    assert hash_obj is not None

def test_key_persistence():
    """Test deterministic key generation with seeds"""
    # Create key with seed for deterministic generation
    seed = b"persistent_test_seed"
    key1 = DapKey(seed=seed)  # Uses default DILITHIUM
    
    # Sign data
    signature1 = DapSign(TEST_DATA, pvt_key=key1)
    
    # Create new key with same seed
    key2 = DapKey(seed=seed)  # Should be identical to key1
    
    # Verify signature with recovered key
    assert signature1.verify(TEST_DATA, key2)

def test_comprehensive_certificate_operations():
    """Test comprehensive certificate operations"""
    # Create certificates with default DILITHIUM
    cert1 = DapCert.create("cert1")
    cert2 = DapCert.create("cert2")
    
    # Sign same data with both certificates
    signature1 = DapSign(TEST_DATA, pvt_cert=cert1)
    signature2 = DapSign(TEST_DATA, pvt_cert=cert2)
    
    # Verify signatures with their respective certificates
    assert signature1.verify(TEST_DATA, cert1)
    assert signature2.verify(TEST_DATA, cert2)
    
    # Cross verification should fail
    assert not signature1.verify(TEST_DATA, cert2)
    assert not signature2.verify(TEST_DATA, cert1)

def test_context_manager_cleanup():
    """Test proper cleanup with context managers"""
    # Test key cleanup
    with DapKey() as key:  # Default DILITHIUM
        signature = DapSign(TEST_DATA, pvt_key=key)
        assert signature.verify(TEST_DATA, key)
    
    # Test certificate cleanup
    with DapCert.create(TEST_CERT_NAME) as cert:
        signature = DapSign(TEST_DATA, pvt_cert=cert)
        assert signature.verify(TEST_DATA, cert)
    
    # Test signature cleanup
    key = DapKey()
    with DapSign(TEST_DATA, pvt_key=key) as signature:
        assert signature.verify(TEST_DATA, key)

def test_production_crypto_standards():
    """Test that production crypto uses post-quantum safe defaults"""
    # Test default key creation uses DILITHIUM (post-quantum safe)
    default_key = DapKey()
    assert default_key.key_type == DapKeyType.DILITHIUM, "Production default should be DILITHIUM"
    
    # Test recommended post-quantum algorithms
    pq_algorithms = [DapKeyType.DILITHIUM, DapKeyType.FALCON, DapKeyType.CHIPMUNK]
    for alg in pq_algorithms:
        key = DapKey(alg)
        signature = DapSign(TEST_DATA, pvt_key=key)
        assert signature.verify(TEST_DATA, key), f"Post-quantum algorithm {alg.name} should work"

def test_comprehensive_aggregated_signatures():
    """Test comprehensive coverage of aggregated signature functionality"""
    # Create CHIPMUNK keys for aggregation (best algorithm for aggregation)
    chipmunk_keys = [DapKey(DapKeyType.CHIPMUNK) for _ in range(5)]
    
    # Test aggregated signature with same message (common case)
    common_message = b"common_message_for_all"
    aggregated_signature = DapSign(common_message, pvt_keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
    
    assert aggregated_signature.verify(common_message), "Aggregated signatures with common message should verify"
    
    # Test aggregated signature with original test data
    test_aggregated = DapSign(TEST_DATA, pvt_keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
    assert test_aggregated.verify(TEST_DATA), "Aggregated signature with test data should verify"

def test_large_scale_multi_signature():
    """Test multi-signature operations with larger number of signers"""
    # Test with larger number of signers (10) - composite
    large_dilithium_set = [DapKey() for _ in range(10)]  # Default DILITHIUM
    
    # Test composite multi-signature with large key set
    composite_sign = DapSign(TEST_DATA, pvt_keys=large_dilithium_set, sign_type=DapSignType.COMPOSITE)
    assert composite_sign.verify(TEST_DATA, large_dilithium_set), \
           "Large scale composite multi-signature should verify"
    
    # Test aggregated multi-signature with CHIPMUNK keys
    chipmunk_large_set = [DapKey(DapKeyType.CHIPMUNK) for _ in range(10)]
    
    aggregated_sign = DapSign(TEST_DATA, pvt_keys=chipmunk_large_set, sign_type=DapSignType.CHIPMUNK)
    assert aggregated_sign.verify(TEST_DATA), \
           "Large scale aggregated multi-signature should verify" 