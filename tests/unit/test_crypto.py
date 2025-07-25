"""
Unit tests for DAP SDK crypto module.
Tests all crypto operations with real DAP SDK functions using existing API.
"""

import pytest
from dap.crypto import (
    DapKey, DapKeyType, 
    DapSign, DapSignType, DapSignError
)

# Test data
TEST_DATA = b"Hello, DAP!"
TEST_LARGE_DATA = b"Large data" * 1024  # ~10KB of data
TEST_CERT_NAME = "test_cert"

@pytest.fixture
def dilithium_key():
    """Create DILITHIUM key for tests (default)"""
    return DapKey()  # Uses DILITHIUM by default

@pytest.fixture
def falcon_key():
    """Create FALCON key for tests"""
    return DapKey(DapKeyType.FALCON)

@pytest.fixture
def chipmunk_key():
    """Create CHIPMUNK key for tests"""
    return DapKey(DapKeyType.CHIPMUNK)

@pytest.fixture
def key_list():
    """Create list of keys for multi-signature tests"""
    return [
        DapKey(),  # Default DILITHIUM
        DapKey(DapKeyType.FALCON),
        DapKey(DapKeyType.CHIPMUNK)
    ]

@pytest.fixture
def chipmunk_key_list():
    """Create list of CHIPMUNK keys for aggregated signature tests"""
    return [
        DapKey(DapKeyType.CHIPMUNK),
        DapKey(DapKeyType.CHIPMUNK),
        DapKey(DapKeyType.CHIPMUNK)
    ]

def test_key_creation():
    """Test key creation with different types"""
    # Test all supported key types
    for key_type in DapKeyType:
        key = DapKey(key_type)
        assert key is not None
        assert key.key_type == key_type

def test_key_creation_from_seed():
    """Test deterministic key creation from seed"""
    seed = b"test_seed"
    key1 = DapKey(DapKeyType.DILITHIUM, seed)
    key2 = DapKey(DapKeyType.DILITHIUM, seed)
    
    # Same seed should produce same key
    sig1 = key1.sign(TEST_DATA)
    assert key2.verify(sig1, TEST_DATA)

def test_key_creation_invalid():
    """Test key creation with invalid parameters"""
    with pytest.raises(DapKeyError):
        DapKey("invalid_type")

def test_key_signing(dilithium_key):
    """Test signing with key using existing API"""
    # Test signing with DapSign constructor
    signature = DapSign(TEST_DATA, pvt_key=dilithium_key)
    assert signature is not None
    assert signature.verify(TEST_DATA, dilithium_key)

def test_key_verification(dilithium_key):
    """Test signature verification"""
    signature = DapSign(TEST_DATA, pvt_key=dilithium_key)
    
    # Valid signature should verify
    assert signature.verify(TEST_DATA, dilithium_key)
    
    # Invalid data should not verify  
    assert not signature.verify(b"wrong data", dilithium_key)

def test_string_data_signing(dilithium_key):
    """Test signing with string data"""
    # Test both bytes and string data
    test_string = TEST_DATA.decode()
    signature = DapSign(test_string, pvt_key=dilithium_key)
    assert signature.verify(test_string, dilithium_key)

def test_composite_multi_sign(key_list):
    """Test composite multi-signature using existing DapSign API"""
    # Create composite multi-signature - auto-detects as COMPOSITE for mixed keys
    multi_signature = DapSign(TEST_DATA, pvt_keys=key_list)
    assert multi_signature is not None
    
    # Verify multi-signature
    assert multi_signature.verify(TEST_DATA, key_list)
    
    # Invalid data should not verify
    assert not multi_signature.verify(b"wrong data", key_list)

def test_aggregated_multi_sign(chipmunk_key_list):
    """Test aggregated multi-signature using existing DapSign API"""
    # Create aggregated signature - auto-detects as CHIPMUNK for CHIPMUNK keys
    aggregated_signature = DapSign(TEST_DATA, pvt_keys=chipmunk_key_list)
    assert aggregated_signature is not None
    
    # Verify aggregated signature (CHIPMUNK aggregated signatures verify differently)
    assert aggregated_signature.verify(TEST_DATA)
    
    # Invalid data should not verify
    assert not aggregated_signature.verify(b"wrong data")

def test_explicit_signature_types(key_list, chipmunk_key_list):
    """Test explicit signature type specification"""
    # Force composite signature type
    composite_sign = DapSign(TEST_DATA, pvt_keys=key_list, sign_type=DapSignType.COMPOSITE)
    assert composite_sign.verify(TEST_DATA, key_list)
    
    # Force aggregated signature type (with CHIPMUNK keys)
    aggregated_sign = DapSign(TEST_DATA, pvt_keys=chipmunk_key_list, sign_type=DapSignType.CHIPMUNK)
    assert aggregated_sign.verify(TEST_DATA)

def test_multi_sign_invalid_type():
    """Test multi-signature creation with invalid type"""
    with pytest.raises(ValueError):
        DapSign(TEST_DATA, pvt_keys=[DapKey()], sign_type="invalid_type")

def test_multi_sign_empty_keys():
    """Test multi-signature verification with empty keys list"""
    with pytest.raises(ValueError):
        DapSign(TEST_DATA, pvt_keys=[])  # Empty keys list should fail

def test_multi_sign_large_data(key_list, chipmunk_key_list):
    """Test multi-signatures with large data"""
    # Test composite multi-signature with large data
    composite_sign = DapSign(TEST_LARGE_DATA, pvt_keys=key_list)
    assert composite_sign.verify(TEST_LARGE_DATA, key_list)
    
    # Test aggregated multi-signature with large data
    aggregated_sign = DapSign(TEST_LARGE_DATA, pvt_keys=chipmunk_key_list)
    assert aggregated_sign.verify(TEST_LARGE_DATA)

def test_signature_context_manager():
    """Test signature cleanup with context managers"""
    key = DapKey()
    
    # Test signature cleanup
    with DapSign(TEST_DATA, pvt_key=key) as signature:
        assert signature.verify(TEST_DATA, key)
    
    # After context exit, signature should be cleaned up
    # (depends on implementation details)

def test_legacy_crypto_deprecated():
    """Test deprecated and quantum-unsafe algorithms in separate unit test"""
    # Test deprecated/classical algorithms that are quantum-unsafe
    # These are tested separately as they should not be used in production
    
    # ECDSA - classical, quantum-unsafe (if available)
    try:
        ecdsa_key = DapKey(DapKeyType.ECDSA)
        signature = DapSign(TEST_DATA, pvt_key=ecdsa_key)
        assert signature.verify(TEST_DATA, ecdsa_key)
        # This should trigger deprecation warning
    except (AttributeError, ValueError):
        # ECDSA might not be available in this build, skip
        pytest.skip("ECDSA not available in this build")
    
    # BLISS - deprecated post-quantum algorithm  
    try:
        bliss_key = DapKey(DapKeyType.BLISS)
        signature = DapSign(TEST_DATA, pvt_key=bliss_key)
        assert signature.verify(TEST_DATA, bliss_key)
    except (AttributeError, ValueError):
        # BLISS might not be available, skip
        pytest.skip("BLISS not available in this build")
    
    # PICNIC - less preferred post-quantum algorithm
    try:
        picnic_key = DapKey(DapKeyType.PICNIC)
        signature = DapSign(TEST_DATA, pvt_key=picnic_key)
        assert signature.verify(TEST_DATA, picnic_key)
    except (AttributeError, ValueError):
        # PICNIC might not be available, skip
        pytest.skip("PICNIC not available in this build")

def test_comprehensive_multi_signature_coverage():
    """Test comprehensive coverage of all multi-signature functions using existing API"""
    # Create keys for testing
    dilithium_keys = [DapKey() for _ in range(3)]  # Default DILITHIUM
    chipmunk_keys = [DapKey(DapKeyType.CHIPMUNK) for _ in range(3)]
    
    # Test composite multi-signature comprehensive coverage
    composite_signature = DapSign(TEST_DATA, pvt_keys=dilithium_keys, sign_type=DapSignType.COMPOSITE)
    assert composite_signature is not None, "Failed to create composite multi-signature"
    
    # Test verification with all keys
    assert composite_signature.verify(TEST_DATA, dilithium_keys), \
           "Failed to verify composite multi-signature"
    
    # Test verification with wrong keys should fail
    wrong_keys = [DapKey() for _ in range(3)]  # Different DILITHIUM keys
    assert not composite_signature.verify(TEST_DATA, wrong_keys), \
           "Composite multi-signature verified with wrong keys"
    
    # Test aggregated multi-signature comprehensive coverage
    aggregated_signature = DapSign(TEST_DATA, pvt_keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
    assert aggregated_signature is not None, "Failed to create aggregated multi-signature"
    
    # Test verification (aggregated signatures verify without explicit keys)
    assert aggregated_signature.verify(TEST_DATA), \
           "Failed to verify aggregated multi-signature"
    
    # Test verification with wrong data should fail
    assert not aggregated_signature.verify(b"wrong data"), \
           "Aggregated multi-signature verified with wrong data"

def test_default_dilithium_usage():
    """Test that DILITHIUM is used as default throughout the system"""
    # Test default key creation uses DILITHIUM
    default_key = DapKey()  # No type specified
    assert default_key.key_type == DapKeyType.DILITHIUM, "Default key type should be DILITHIUM"
    
    # Test default signature creation with DILITHIUM
    signature = DapSign(TEST_DATA, pvt_key=default_key)
    assert signature.verify(TEST_DATA, default_key), "Default DILITHIUM signature should verify"
    
    # Test multi-signature with default DILITHIUM keys
    default_keys = [DapKey() for _ in range(3)]  # All default to DILITHIUM
    multi_sig = DapSign(TEST_DATA, pvt_keys=default_keys)
    assert multi_sig.verify(TEST_DATA, default_keys), \
           "Multi-signature with default DILITHIUM keys should verify" 