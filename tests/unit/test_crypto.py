"""
Unit tests for DAP SDK crypto module.
Tests all crypto operations with real DAP SDK functions.
"""

import pytest
from dap.crypto import (
    DapCryptoKey, DapKeyType, DapKeyError,
    DapSign, DapSignError, DapSignType,
    DapMultiSign, DapMultiSignType,
    quick_sign, quick_verify, quick_multi_sign
)

# Test data
TEST_DATA = b"Hello, DAP!"
TEST_LARGE_DATA = b"Large data" * 1024  # ~10KB of data
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
def key_list():
    """Create list of keys for multi-signature tests"""
    return [
        DapCryptoKey(DapKeyType.DILITHIUM),
        DapCryptoKey(DapKeyType.FALCON),
        DapCryptoKey(DapKeyType.CHIPMUNK)
    ]

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

def test_composite_multi_sign(key_list):
    """Test composite multi-signature creation and verification"""
    # Create multi-signature
    with DapMultiSign(DapMultiSignType.COMPOSITE) as multi_sign:
        # Add signatures from each key
        for key in key_list:
            signature = quick_sign(key, TEST_DATA)
            multi_sign.add_signature(signature)
        
        # Combine signatures
        combined = multi_sign.combine()
        assert combined is not None
        
        # Verify combined signature
        assert DapMultiSign.verify(combined, TEST_DATA, key_list, DapMultiSignType.COMPOSITE)
        
        # Invalid data should not verify
        assert not DapMultiSign.verify(combined, b"wrong data", key_list, DapMultiSignType.COMPOSITE)

def test_aggregated_multi_sign(key_list):
    """Test aggregated multi-signature creation and verification"""
    # Create aggregated signature
    with DapMultiSign(DapMultiSignType.AGGREGATED_CHIPMUNK) as multi_sign:
        # Add signatures from each key
        for key in key_list:
            signature = quick_sign(key, TEST_DATA)
            multi_sign.add_signature(signature, key)
        
        # Combine signatures
        combined = multi_sign.combine()
        assert combined is not None
        
        # Verify combined signature
        assert DapMultiSign.verify(combined, TEST_DATA, sign_type=DapMultiSignType.AGGREGATED_CHIPMUNK)
        
        # Invalid data should not verify
        assert not DapMultiSign.verify(combined, b"wrong data", sign_type=DapMultiSignType.AGGREGATED_CHIPMUNK)

def test_quick_multi_sign(key_list):
    """Test quick multi-signature helpers"""
    # Test composite multi-signature
    composite_sign = quick_multi_sign(key_list, TEST_DATA, DapMultiSignType.COMPOSITE)
    assert DapMultiSign.verify(composite_sign, TEST_DATA, key_list, DapMultiSignType.COMPOSITE)
    
    # Test aggregated multi-signature
    aggregated_sign = quick_multi_sign(key_list, TEST_DATA, DapMultiSignType.AGGREGATED_CHIPMUNK)
    assert DapMultiSign.verify(aggregated_sign, TEST_DATA, sign_type=DapMultiSignType.AGGREGATED_CHIPMUNK)

def test_multi_sign_invalid_type():
    """Test multi-signature creation with invalid type"""
    with pytest.raises(ValueError):
        DapMultiSign("invalid_type")

def test_multi_sign_missing_key():
    """Test aggregated signature without key"""
    with DapMultiSign(DapMultiSignType.AGGREGATED_CHIPMUNK) as multi_sign:
        signature = quick_sign(DapCryptoKey(DapKeyType.CHIPMUNK), TEST_DATA)
        with pytest.raises(ValueError):
            multi_sign.add_signature(signature)  # Missing required key

def test_multi_sign_empty_keys():
    """Test multi-signature verification with empty keys list"""
    with pytest.raises(ValueError):
        quick_multi_sign([], TEST_DATA)

def test_multi_sign_large_data(key_list):
    """Test multi-signatures with large data"""
    # Test composite multi-signature
    composite_sign = quick_multi_sign(key_list, TEST_LARGE_DATA, DapMultiSignType.COMPOSITE)
    assert DapMultiSign.verify(composite_sign, TEST_LARGE_DATA, key_list, DapMultiSignType.COMPOSITE)
    
    # Test aggregated multi-signature
    aggregated_sign = quick_multi_sign(key_list, TEST_LARGE_DATA, DapMultiSignType.AGGREGATED_CHIPMUNK)
    assert DapMultiSign.verify(aggregated_sign, TEST_LARGE_DATA, sign_type=DapMultiSignType.AGGREGATED_CHIPMUNK)

def test_multi_sign_context_manager():
    """Test multi-signature context manager cleanup"""
    multi_sign = DapMultiSign()
    handle = multi_sign._handle
    
    with multi_sign:
        assert multi_sign._handle == handle
    
    assert multi_sign._handle is None 