"""
Unit tests for DAP SDK crypto module.
Tests unified DapSign API with all signature types including composite and aggregated.
"""

import pytest
from dap.crypto import (
    DapCryptoKey, DapKeyType,
    DapSign, DapSignError, DapSignType, DapSignMetadata,
    get_recommended_signature_types,
    get_deprecated_signature_types,
    get_quantum_vulnerable_signature_types,
    get_legacy_deprecated_signature_types,
    check_signature_compatibility
)

# Test data
TEST_DATA = b"Hello, DAP!"
TEST_LARGE_DATA = b"Large data" * 1024  # ~10KB of data
TEST_STRING_DATA = "String data for testing"

@pytest.fixture
def dilithium_key():
    """Create DILITHIUM private key for tests"""
    return DapCryptoKey(DapKeyType.DILITHIUM)

@pytest.fixture
def falcon_key():
    """Create FALCON private key for tests"""
    return DapCryptoKey(DapKeyType.FALCON)

@pytest.fixture
def chipmunk_key():
    """Create CHIPMUNK private key for tests"""
    return DapCryptoKey(DapKeyType.CHIPMUNK)

@pytest.fixture
def recommended_keys():
    """Create keys for all recommended signature types"""
    return [DapCryptoKey(DapKeyType(key_type)) for key_type in get_recommended_signature_types()]

@pytest.fixture
def composite_keys():
    """Create keys for composite multi-signature tests"""
    return [
        DapCryptoKey(DapKeyType.DILITHIUM),
        DapCryptoKey(DapKeyType.FALCON),
        DapCryptoKey(DapKeyType.SPHINCSPLUS)
    ]

@pytest.fixture
def chipmunk_keys():
    """Create CHIPMUNK keys for aggregated signature tests"""
    return [
        DapCryptoKey(DapKeyType.CHIPMUNK),
        DapCryptoKey(DapKeyType.CHIPMUNK),
        DapCryptoKey(DapKeyType.CHIPMUNK)
    ]

class TestDapSignMetadata:
    """Test signature metadata functionality"""
    
    def test_get_metadata(self):
        """Test metadata retrieval for different signature types"""
        for sign_type in DapSignType:
            metadata = DapSignMetadata.get_metadata(sign_type)
            assert isinstance(metadata, dict)
            assert 'quantum_secure' in metadata
            assert 'deprecated' in metadata
            assert 'description' in metadata
    
    def test_supports_multi_signature(self):
        """Test multi-signature support detection"""
        assert DapSignMetadata.supports_multi_signature(DapSignType.COMPOSITE)
        assert DapSignMetadata.supports_multi_signature(DapSignType.CHIPMUNK)
        assert not DapSignMetadata.supports_multi_signature(DapSignType.DILITHIUM)
    
    def test_quantum_security_classification(self):
        """Test quantum security and vulnerability classification"""
        # Recommended types should be quantum secure
        for sign_type in get_recommended_signature_types():
            assert not DapSignMetadata.is_quantum_vulnerable(sign_type)
        
        # Quantum vulnerable types should be marked as such
        for sign_type in get_quantum_vulnerable_signature_types():
            assert DapSignMetadata.is_quantum_vulnerable(sign_type)
            assert DapSignMetadata.is_deprecated(sign_type)

class TestDapSignSingle:
    """Test single signature operations"""
    
    def test_single_signature_creation_static(self, dilithium_key):
        """Test single signature creation via static method"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        
        assert signature is not None
        assert signature.sign_type == DapSignType.DILITHIUM
        assert not signature.supports_multi_signature()
        assert signature.is_quantum_secure()
        assert not signature.is_deprecated()
        assert len(signature.keys) == 1  # Should contain extracted public key
    
    def test_single_signature_creation_constructor(self, falcon_key):
        """Test single signature creation via constructor"""
        signature = DapSign(to_sign=TEST_DATA, pvt_key=falcon_key)
        
        assert signature is not None
        assert signature.sign_type == DapSignType.FALCON
        assert len(signature.keys) == 1
    
    def test_single_signature_string_data(self, dilithium_key):
        """Test single signature with string data"""
        signature = DapSign.sign(to_sign=TEST_STRING_DATA, pvt_key=dilithium_key)
        
        assert signature is not None
        assert signature.verify(to_sign=TEST_STRING_DATA)
    
    def test_single_signature_verification(self, dilithium_key):
        """Test single signature verification"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        
        # Should verify with stored keys
        assert signature.verify(to_sign=TEST_DATA)
        
        # Should verify with explicit key
        pub_key = dilithium_key.get_public_key()
        assert signature.verify(to_sign=TEST_DATA, pub_keys=[pub_key])
        
        # Should fail with wrong data
        assert not signature.verify(to_sign=b"wrong data")
    
    def test_single_signature_all_types(self, recommended_keys):
        """Test single signatures for all recommended types"""
        for key in recommended_keys:
            signature = DapSign.sign(to_sign=TEST_DATA, pvt_key=key)
            assert signature.verify(to_sign=TEST_DATA)
            assert signature.is_quantum_secure()
            assert not signature.is_deprecated()

class TestDapSignComposite:
    """Test composite multi-signature operations"""
    
    def test_composite_creation_auto_detect(self, composite_keys):
        """Test composite signature creation with auto-detection"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=composite_keys)
        
        assert signature is not None
        assert signature.sign_type == DapSignType.COMPOSITE
        assert signature.supports_multi_signature()
        assert not signature.supports_aggregation()
        assert len(signature.keys) == len(composite_keys)
    
    def test_composite_creation_explicit(self, composite_keys):
        """Test composite signature creation with explicit type"""
        signature = DapSign.sign(
            to_sign=TEST_DATA, 
            pvt_keys=composite_keys,
            sign_type=DapSignType.COMPOSITE
        )
        
        assert signature.sign_type == DapSignType.COMPOSITE
        assert signature.verify(to_sign=TEST_DATA)
    
    def test_composite_verification_subset(self, composite_keys):
        """Test composite signature verification with subset of keys"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=composite_keys)
        
        # Should verify with all keys
        assert signature.verify(to_sign=TEST_DATA)
        
        # Should verify with subset of keys
        subset_keys = signature.keys[:2]  # First 2 public keys
        assert signature.verify(to_sign=TEST_DATA, pub_keys=subset_keys)
    
    def test_composite_sign_add(self, composite_keys):
        """Test adding signatures to existing composite"""
        # Create initial signature with first 2 keys
        initial_keys = composite_keys[:2]
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=initial_keys)
        
        # Add third signature
        additional_key = composite_keys[2]
        signature.sign_add(to_sign=TEST_DATA, pvt_key=additional_key)
        
        assert len(signature.keys) == 3
        assert signature.verify(to_sign=TEST_DATA)
    
    def test_composite_large_data(self, composite_keys):
        """Test composite signatures with large data"""
        signature = DapSign.sign(to_sign=TEST_LARGE_DATA, pvt_keys=composite_keys)
        assert signature.verify(to_sign=TEST_LARGE_DATA)

class TestDapSignAggregated:
    """Test aggregated (Chipmunk) signature operations"""
    
    def test_aggregated_creation_auto_detect(self, chipmunk_keys):
        """Test aggregated signature creation with auto-detection"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=chipmunk_keys)
        
        assert signature is not None
        assert signature.sign_type == DapSignType.CHIPMUNK
        assert signature.supports_multi_signature()
        assert signature.supports_aggregation()
        assert len(signature.keys) == len(chipmunk_keys)
    
    def test_aggregated_creation_explicit(self, chipmunk_keys):
        """Test aggregated signature creation with explicit type"""
        signature = DapSign.sign(
            to_sign=TEST_DATA,
            pvt_keys=chipmunk_keys,
            sign_type=DapSignType.CHIPMUNK
        )
        
        assert signature.sign_type == DapSignType.CHIPMUNK
        assert signature.verify(to_sign=TEST_DATA)
    
    def test_aggregated_verification_complete(self, chipmunk_keys):
        """Test aggregated signature verification (requires complete signature)"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=chipmunk_keys)
        
        # Should verify with stored keys
        assert signature.verify(to_sign=TEST_DATA)
        
        # Aggregated signatures verify as complete unit
        assert signature.verify(to_sign=TEST_DATA, pub_keys=signature.keys)
    
    def test_aggregated_sign_add(self, chipmunk_keys):
        """Test adding signatures to existing aggregated"""
        # Create initial signature with first 2 keys
        initial_keys = chipmunk_keys[:2]
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_keys=initial_keys)
        
        # Add third signature
        additional_key = chipmunk_keys[2]
        signature.sign_add(to_sign=TEST_DATA, pvt_key=additional_key)
        
        assert len(signature.keys) == 3
        assert signature.verify(to_sign=TEST_DATA)

class TestDapSignWrapMode:
    """Test signature handle wrapping mode"""
    
    def test_wrap_existing_handle(self, dilithium_key):
        """Test wrapping existing signature handle"""
        # Create signature and get handle
        original = DapSign.sign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        handle = original.handle
        
        # Create wrapper around existing handle
        wrapped = DapSign(
            handle=handle,
            sign_type=DapSignType.DILITHIUM,
            keys=original.keys
        )
        
        assert wrapped.handle == handle
        assert wrapped.sign_type == DapSignType.DILITHIUM
        assert wrapped.verify(to_sign=TEST_DATA)

class TestDapSignCapabilities:
    """Test signature capability checking"""
    
    def test_capability_methods(self):
        """Test signature capability checking methods"""
        # Test with different signature types
        test_cases = [
            (DapSignType.DILITHIUM, True, False, False, False),  # quantum_secure, deprecated, quantum_vulnerable, multi
            (DapSignType.COMPOSITE, True, False, False, True),   # quantum_secure, deprecated, quantum_vulnerable, multi
            (DapSignType.CHIPMUNK, True, False, False, True),    # quantum_secure, deprecated, quantum_vulnerable, multi
            (DapSignType.ECDSA, False, True, True, False),       # quantum_secure, deprecated, quantum_vulnerable, multi
        ]
        
        for sign_type, q_secure, deprecated, q_vulnerable, multi in test_cases:
            # Create dummy signature (handle mode)
            signature = DapSign(handle=1, sign_type=sign_type, keys=[])
            
            assert signature.is_quantum_secure() == q_secure
            assert signature.is_deprecated() == deprecated
            assert signature.is_quantum_vulnerable() == q_vulnerable
            assert signature.supports_multi_signature() == multi

class TestDapSignErrorHandling:
    """Test error handling and edge cases"""
    
    def test_invalid_parameters(self):
        """Test invalid parameter combinations"""
        key = DapCryptoKey(DapKeyType.DILITHIUM)
        
        # Cannot specify both pvt_key and pvt_keys
        with pytest.raises(ValueError):
            DapSign.sign(to_sign=TEST_DATA, pvt_key=key, pvt_keys=[key])
        
        # Must provide some way to sign
        with pytest.raises(ValueError):
            DapSign.sign(to_sign=TEST_DATA)
        
        # Must provide to_sign or handle
        with pytest.raises(ValueError):
            DapSign()
    
    def test_invalid_sign_add(self, dilithium_key):
        """Test sign_add on non-multi-signature"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        
        with pytest.raises(ValueError):
            signature.sign_add(to_sign=TEST_DATA, pvt_key=dilithium_key)
    
    def test_invalid_verification_key_count(self, dilithium_key):
        """Test verification with wrong number of keys"""
        signature = DapSign.sign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        
        # Single signature requires exactly one key
        with pytest.raises(ValueError):
            signature.verify(to_sign=TEST_DATA, pub_keys=[])
        
        with pytest.raises(ValueError):
            key1 = dilithium_key.get_public_key()
            key2 = DapCryptoKey(DapKeyType.FALCON).get_public_key()
            signature.verify(to_sign=TEST_DATA, pub_keys=[key1, key2])

class TestUtilityFunctions:
    """Test utility functions for signature types"""
    
    def test_get_signature_type_lists(self):
        """Test signature type classification functions"""
        recommended = get_recommended_signature_types()
        deprecated = get_deprecated_signature_types()
        quantum_vulnerable = get_quantum_vulnerable_signature_types()
        legacy_deprecated = get_legacy_deprecated_signature_types()
        
        # Should have non-empty lists
        assert len(recommended) > 0
        assert len(deprecated) > 0
        
        # Quantum vulnerable should be subset of deprecated
        for qv_type in quantum_vulnerable:
            assert qv_type in deprecated
        
        # Legacy deprecated should be subset of deprecated
        for ld_type in legacy_deprecated:
            assert ld_type in deprecated
        
        # No overlap between recommended and deprecated
        assert not set(recommended) & set(deprecated)
    
    def test_signature_compatibility(self):
        """Test signature compatibility checking"""
        # Same types should be compatible
        assert check_signature_compatibility(DapSignType.DILITHIUM, DapSignType.DILITHIUM)
        assert check_signature_compatibility(DapSignType.COMPOSITE, DapSignType.COMPOSITE)
        
        # Different types should not be compatible
        assert not check_signature_compatibility(DapSignType.DILITHIUM, DapSignType.FALCON)
        assert not check_signature_compatibility(DapSignType.COMPOSITE, DapSignType.CHIPMUNK)

class TestDeprecatedSignatures:
    """Test deprecated signature types"""
    
    def test_deprecated_types_classification(self):
        """Test that deprecated types are properly classified"""
        deprecated_types = get_deprecated_signature_types()
        
        for sign_type in deprecated_types:
            metadata = DapSignMetadata.get_metadata(sign_type)
            assert metadata['deprecated'] == True
            
            # Should also work through static method
            assert DapSignMetadata.is_deprecated(sign_type)
    
    def test_quantum_vulnerable_vs_legacy(self):
        """Test distinction between quantum vulnerable and legacy deprecated"""
        quantum_vulnerable = get_quantum_vulnerable_signature_types()
        legacy_deprecated = get_legacy_deprecated_signature_types()
        
        # Should be mutually exclusive
        assert not set(quantum_vulnerable) & set(legacy_deprecated)
        
        # Quantum vulnerable should not be quantum secure
        for qv_type in quantum_vulnerable:
            metadata = DapSignMetadata.get_metadata(qv_type)
            assert metadata['quantum_secure'] == False
            assert metadata['quantum_vulnerable'] == True
        
        # Legacy deprecated might still be quantum secure
        for ld_type in legacy_deprecated:
            metadata = DapSignMetadata.get_metadata(ld_type)
            assert metadata['quantum_vulnerable'] == False 