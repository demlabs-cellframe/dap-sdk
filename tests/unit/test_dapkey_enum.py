"""
Test DapKeyType unified enum functionality
"""
import pytest
from dap.crypto import DapKeyType

class TestDapKeyTypeEnum:
    """Test the unified DapKeyType enum"""
    
    def test_algorithm_count(self):
        """Test that we have all expected algorithms"""
        all_algs = list(DapKeyType)
        assert len(all_algs) == 35, f"Expected 35 algorithms, got {len(all_algs)}"
    
    def test_post_quantum_classification(self):
        """Test post-quantum algorithm classification"""
        post_quantum = [alg for alg in DapKeyType if alg.is_post_quantum]
        quantum_vulnerable = [alg for alg in DapKeyType if alg.is_quantum_vulnerable]
        
        # DILITHIUM should be post-quantum
        assert DapKeyType.DILITHIUM.is_post_quantum
        assert not DapKeyType.DILITHIUM.is_quantum_vulnerable
        
        # ECDSA should be quantum-vulnerable
        assert DapKeyType.ECDSA.is_quantum_vulnerable
        assert not DapKeyType.ECDSA.is_post_quantum
        
        assert len(post_quantum) > 0
        assert len(quantum_vulnerable) > 0
    
    def test_algorithm_types(self):
        """Test algorithm type classification"""
        # Signature algorithms
        assert DapKeyType.DILITHIUM.is_signature_algorithm
        assert DapKeyType.FALCON.is_signature_algorithm
        assert DapKeyType.ECDSA.is_signature_algorithm
        assert DapKeyType.CHIPMUNK.is_signature_algorithm
        
        # Symmetric algorithms
        assert DapKeyType.IAES.is_symmetric_algorithm
        assert DapKeyType.SALSA2012.is_symmetric_algorithm
        assert DapKeyType.GOST_OFB.is_symmetric_algorithm
        
        # Key exchange algorithms
        assert DapKeyType.KYBER.is_key_exchange_algorithm
        assert DapKeyType.NEWHOPE.is_key_exchange_algorithm
        assert DapKeyType.FRODO.is_key_exchange_algorithm
    
    def test_deprecated_algorithms(self):
        """Test deprecated algorithm marking"""
        assert DapKeyType.BLISS.is_deprecated
        assert not DapKeyType.DILITHIUM.is_deprecated
        assert not DapKeyType.ECDSA.is_deprecated  # ECDSA is quantum-vulnerable but not deprecated
    
    def test_default_algorithms(self):
        """Test that default algorithms are properly set"""
        # DILITHIUM should be the signature default
        assert DapKeyType.DILITHIUM.is_post_quantum
        assert DapKeyType.DILITHIUM.is_signature_algorithm
        
        # IAES should be a good symmetric default
        assert DapKeyType.IAES.is_post_quantum
        assert DapKeyType.IAES.is_symmetric_algorithm
    
    def test_algorithm_properties_mutually_exclusive(self):
        """Test that algorithm types are properly exclusive"""
        for alg in DapKeyType:
            type_count = sum([
                alg.is_signature_algorithm,
                alg.is_symmetric_algorithm, 
                alg.is_key_exchange_algorithm
            ])
            # Each algorithm should have exactly one primary type
            assert type_count <= 1, f"{alg.name} has multiple types"
    
    def test_from_string_conversion(self):
        """Test string to enum conversion"""
        assert DapKeyType.from_string("dilithium") == DapKeyType.DILITHIUM
        assert DapKeyType.from_string("FALCON") == DapKeyType.FALCON
        assert DapKeyType.from_string("unknown") == DapKeyType.DILITHIUM  # Default fallback 