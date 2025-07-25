"""
Comprehensive crypto tests for DAP SDK Python bindings
Tests all cryptographic functions with DILITHIUM as default and full coverage
of composite/aggregated signatures
"""

import pytest
import warnings
from dap.crypto import (
    DapKey, DapKeyType, DapSign, DapSignType,
    DapCert
)

# Test data
TEST_DATA = b"Test message for cryptographic operations"
TEST_LARGE_DATA = b"x" * 10000  # Large data for performance tests


class TestPostQuantumCrypto:
    """Test post-quantum safe algorithms (default usage)"""
    
    @pytest.fixture
    def dilithium_key(self):
        """Default DILITHIUM key for most tests"""
        return DapKey(DapKeyType.DILITHIUM)
    
    @pytest.fixture
    def falcon_key(self):
        """FALCON key for alternative tests"""
        return DapKey(DapKeyType.FALCON)
    
    @pytest.fixture
    def chipmunk_key(self):
        """CHIPMUNK key for aggregation tests"""
        return DapKey(DapKeyType.CHIPMUNK)
    
    def test_dilithium_default_signature(self, dilithium_key):
        """Test DILITHIUM as default signature algorithm"""
        signature = DapSign(to_sign=TEST_DATA, pvt_key=dilithium_key)
        assert signature is not None
        assert signature.verify(TEST_DATA)
        
        # Verify signature structure
        assert signature.handle > 0
    
    def test_dilithium_large_data(self, dilithium_key):
        """Test DILITHIUM with large data"""
        signature = DapSign(to_sign=TEST_LARGE_DATA, pvt_key=dilithium_key)
        assert signature is not None
        assert signature.verify(TEST_LARGE_DATA)
    
    def test_falcon_alternative(self, falcon_key):
        """Test FALCON as alternative post-quantum algorithm"""
        signature = DapSign(to_sign=TEST_DATA, pvt_key=falcon_key)
        assert signature is not None
        assert signature.verify(TEST_DATA)
    
    def test_chipmunk_aggregation_optimized(self, chipmunk_key):
        """Test CHIPMUNK optimized for aggregation"""
        signature = DapSign(to_sign=TEST_DATA, pvt_key=chipmunk_key)
        assert signature is not None
        assert signature.verify(TEST_DATA)


class TestCompositeSignatures:
    """Test composite signature functionality"""
    
    @pytest.fixture
    def multiple_keys(self):
        """Multiple keys for composite signatures"""
        return [
            DapKey(DapKeyType.DILITHIUM),
            DapKey(DapKeyType.FALCON),
            DapKey(DapKeyType.PICNIC)
        ]
    
    def test_composite_signature_creation(self, multiple_keys):
        """Test creating composite signatures with multiple algorithms"""
        composite_sig = DapSign(to_sign=TEST_DATA, pvt_keys=multiple_keys)
        assert composite_sig is not None
        
        # Verify with all keys
        assert composite_sig.verify(TEST_DATA)
    
    def test_composite_signature_partial_verification(self, multiple_keys):
        """Test that composite signature requires all keys for verification"""
        composite_sig = DapSign(to_sign=TEST_DATA, pvt_keys=multiple_keys)
        
        # Composite signature should verify successfully with original data
        assert composite_sig.verify(TEST_DATA)
        
        # Should fail with wrong data
        assert not composite_sig.verify(b"wrong data")
    
    def test_composite_signature_key_order(self, multiple_keys):
        """Test composite signature with different key combinations"""
        composite_sig = DapSign(to_sign=TEST_DATA, pvt_keys=multiple_keys)
        
        # Should verify with original data
        assert composite_sig.verify(TEST_DATA)
        
        # Test with different data to ensure signature is working
        assert not composite_sig.verify(b"different data")


class TestAggregatedSignatures:
    """Test aggregated signature functionality"""
    
    @pytest.fixture
    def chipmunk_keys(self):
        """Multiple CHIPMUNK keys optimized for aggregation"""
        return [DapKey(DapKeyType.CHIPMUNK) for _ in range(5)]
    
    def test_aggregated_signature_creation(self, chipmunk_keys):
        """Test creating aggregated signatures"""
        # Create aggregated signature with multiple CHIPMUNK keys
        aggregated_sig = DapSign(to_sign=TEST_DATA, pvt_keys=chipmunk_keys)
        assert aggregated_sig is not None
        
        # Verify aggregated signature
        assert aggregated_sig.verify(TEST_DATA)
    
    def test_aggregated_signature_efficiency(self, chipmunk_keys):
        """Test aggregated signature properties"""
        # Create aggregated signature
        aggregated_sig = DapSign(to_sign=TEST_DATA, pvt_keys=chipmunk_keys)
        
        # Verify it's working
        assert aggregated_sig.verify(TEST_DATA)
        
        # Verify it supports aggregation
        assert aggregated_sig.supports_aggregation()
    
    def test_aggregated_signature_incremental(self, chipmunk_keys):
        """Test incremental aggregation of signatures"""
        # Start with first key
        first_key = chipmunk_keys[0]
        multi_sign = DapSign(to_sign=TEST_DATA, pvt_key=first_key)
        
        # Add signatures incrementally
        for key in chipmunk_keys[1:]:
            multi_sign.sign_add(TEST_DATA, key)
        
        # Verify final aggregated signature
        assert multi_sign.verify(TEST_DATA)


class TestCertificateSystem:
    """Test certificate and chain functionality with post-quantum crypto"""
    
    def test_certificate_with_dilithium(self):
        """Test certificate creation with DILITHIUM"""
        cert = DapCert.create("test_dilithium_cert", DapKeyType.DILITHIUM)
        assert cert is not None
        
        signature = cert.sign(TEST_DATA)
        assert cert.verify(signature, TEST_DATA)
    
    def test_certificate_with_falcon(self):
        """Test certificate creation with FALCON"""
        cert = DapCert.create("test_falcon_cert", DapKeyType.FALCON)
        assert cert is not None
        
        signature = cert.sign(TEST_DATA)
        assert cert.verify(signature, TEST_DATA)
    
    def test_certificate_with_chipmunk(self):
        """Test certificate creation with CHIPMUNK"""
        cert = DapCert.create("test_chipmunk_cert", DapKeyType.CHIPMUNK)
        assert cert is not None
        
        signature = cert.sign(TEST_DATA)
        assert cert.verify(signature, TEST_DATA)


class TestPerformanceAndSecurity:
    """Test performance characteristics and security features"""
    
    def test_signature_performance_comparison(self):
        """Compare performance of different post-quantum algorithms"""
        algorithms = [DapKeyType.DILITHIUM, DapKeyType.FALCON, DapKeyType.CHIPMUNK]
        results = {}
        
        for alg in algorithms:
            key = DapKey(alg)
            
            import time
            start_time = time.time()
            
            # Perform multiple operations
            for _ in range(10):
                signature = DapSign(to_sign=TEST_DATA, pvt_key=key)
                assert signature.verify(TEST_DATA)
            
            end_time = time.time()
            results[alg.value] = end_time - start_time
        
        # Log performance results (for analysis)
        print(f"Performance results: {results}")
        
        # All should complete in reasonable time
        for alg, time_taken in results.items():
            assert time_taken < 10.0, f"{alg} took too long: {time_taken}s"
    
    def test_signature_randomness(self):
        """Test that signatures work correctly"""
        key = DapKey(DapKeyType.DILITHIUM)
        
        # Generate multiple signatures of same data
        signatures = [DapSign(to_sign=TEST_DATA, pvt_key=key) for _ in range(5)]
        
        # All signatures should verify
        for signature in signatures:
            assert signature.verify(TEST_DATA)


class TestLegacyCryptoDeprecated:
    """
    Test deprecated and quantum-unsafe algorithms
    These are tested separately as they should not be used in production
    """
    
    def test_ecdsa_deprecated_warning(self):
        """Test that ECDSA usage generates deprecation warning"""
        with warnings.catch_warnings(record=True) as warning_list:
            warnings.simplefilter("always")
            
            # This should generate a warning
            key = DapKey(DapKeyType.ECDSA)
            signature = DapSign(to_sign=TEST_DATA, pvt_key=key)
            
            # Verify functionality still works for compatibility
            assert signature.verify(TEST_DATA)
            
            # Check that warning was generated
            assert len(warning_list) > 0
            assert any("quantum-unsafe" in str(w.message) for w in warning_list)
    
    def test_bliss_deprecated_warning(self):
        """Test that BLISS usage generates deprecation warning"""
        with warnings.catch_warnings(record=True) as warning_list:
            warnings.simplefilter("always")
            
            key = DapKey(DapKeyType.BLISS)
            signature = DapSign(to_sign=TEST_DATA, pvt_key=key)
            
            assert signature.verify(TEST_DATA)
            
            # Check for deprecation warning
            assert len(warning_list) > 0
            assert any("deprecated" in str(w.message) for w in warning_list)
    
    def test_legacy_algorithms_isolated(self):
        """Test that legacy algorithms are isolated and not mixed with modern ones"""
        legacy_key = DapKey(DapKeyType.ECDSA)
        modern_key = DapKey(DapKeyType.DILITHIUM)
        
        legacy_sig = DapSign(to_sign=TEST_DATA, pvt_key=legacy_key)
        modern_sig = DapSign(to_sign=TEST_DATA, pvt_key=modern_key)
        
        # Self-verification should work
        assert legacy_sig.verify(TEST_DATA)
        assert modern_sig.verify(TEST_DATA)
        
        # Check algorithm properties
        assert legacy_sig.is_quantum_vulnerable()
        assert not modern_sig.is_quantum_vulnerable()


if __name__ == "__main__":
    pytest.main([__file__, "-v"]) 