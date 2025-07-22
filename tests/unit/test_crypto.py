"""
Unit tests for DAP Crypto modules
Tests cryptographic functionality
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
sys.path.insert(0, str(Path(__file__).parent.parent))

# Import test helper for DAP SDK initialization
from test_init_helper import init_test_dap_sdk

class TestDapCryptoKey:
    """Test cases for DapCryptoKey class"""
    
    @classmethod
    def setup_class(cls):
        """Setup test environment with DAP SDK initialization"""
        cls._test_sdk = init_test_dap_sdk("test_crypto_key")
    
    @classmethod
    def teardown_class(cls):
        """Cleanup test environment"""
        if hasattr(cls, '_test_sdk'):
            cls._test_sdk.cleanup()
    
    def test_key_creation_with_handle(self):
        """Test DapCryptoKey creation with handle"""
        from dap.crypto.keys import DapCryptoKey
        
        key = DapCryptoKey("test_handle")
        assert key is not None
        assert key.handle == "test_handle"
    
    def test_key_methods(self):
        """Test DapCryptoKey methods"""
        from dap.crypto.keys import DapCryptoKey
        
        key = DapCryptoKey("test_handle")
        assert hasattr(key, 'handle')
        assert hasattr(key, '__repr__')


class TestDapHash:
    """Test cases for DapHash class"""
    
    @classmethod
    def setup_class(cls):
        """Setup test environment with DAP SDK initialization"""
        cls._test_sdk = init_test_dap_sdk("test_crypto_hash")
    
    @classmethod
    def teardown_class(cls):
        """Cleanup test environment"""
        if hasattr(cls, '_test_sdk'):
            cls._test_sdk.cleanup()
    
    def test_hash_creation(self):
        """Test DapHash creation"""
        from dap.crypto.hash import DapHash
        
        hasher = DapHash()
        assert hasher is not None
    
    def test_hash_methods(self):
        """Test DapHash methods"""
        from dap.crypto.hash import DapHash
        
        hasher = DapHash()
        assert hasattr(hasher, 'hash_fast')
        assert callable(hasher.hash_fast)
    
    def test_hash_algorithm_support(self):
        """Test hash algorithm constants"""
        from dap.crypto.hash import DapHashType
        
        # Test that common algorithms are defined
        assert hasattr(DapHashType, 'SHA256')
        assert hasattr(DapHashType, 'SHA512')
        assert hasattr(DapHashType, 'KECCAK')


class TestDapSign:
    """Test cases for DapSign class"""
    
    @classmethod
    def setup_class(cls):
        """Setup test environment with DAP SDK initialization"""
        cls._test_sdk = init_test_dap_sdk("test_crypto_sign")
    
    @classmethod
    def teardown_class(cls):
        """Cleanup test environment"""
        if hasattr(cls, '_test_sdk'):
            cls._test_sdk.cleanup()
    
    def test_sign_creation_with_handle(self):
        """Test DapSign creation with handle"""
        from dap.crypto.sign import DapSign
        
        sign = DapSign("test_handle")
        assert sign is not None
        assert sign.handle == "test_handle"
    
    def test_sign_methods(self):
        """Test DapSign methods"""
        from dap.crypto.sign import DapSign
        
        sign = DapSign("test_handle")
        assert hasattr(sign, 'handle')
        assert hasattr(sign, '__repr__')
    
    def test_sign_type_support(self):
        """Test signature type constants"""
        from dap.crypto.sign import DapSignType
        
        # Test that signature types are defined
        assert hasattr(DapSignType, 'DILITHIUM')
        assert hasattr(DapSignType, 'FALCON')
        assert hasattr(DapSignType, 'SPHINCS')


class TestDapEnc:
    """Test cases for DapEnc encryption class"""
    
    @classmethod
    def setup_class(cls):
        """Setup test environment with DAP SDK initialization"""
        cls._test_sdk = init_test_dap_sdk("test_crypto_enc")
    
    @classmethod
    def teardown_class(cls):
        """Cleanup test environment"""
        if hasattr(cls, '_test_sdk'):
            cls._test_sdk.cleanup()
    
    def test_enc_creation(self):
        """Test DapEnc creation"""
        from dap.crypto.enc import DapEnc
        
        enc = DapEnc()
        assert enc is not None
    
    def test_enc_key_methods(self):
        """Test encryption key methods"""
        from dap.crypto.enc import DapEnc
        
        enc = DapEnc()
        assert hasattr(enc, 'key_new_generate')
        assert hasattr(enc, 'key_delete')
        assert callable(enc.key_new_generate)
        assert callable(enc.key_delete)
    
    def test_enc_cipher_methods(self):
        """Test encryption cipher methods"""
        from dap.crypto.enc import DapEnc
        
        enc = DapEnc()
        assert hasattr(enc, 'encrypt')
        assert hasattr(enc, 'decrypt')
        assert callable(enc.encrypt)
        assert callable(enc.decrypt)


class TestDapCert:
    """Test cases for DapCert certificate class"""
    
    @classmethod
    def setup_class(cls):
        """Setup test environment with DAP SDK initialization"""
        cls._test_sdk = init_test_dap_sdk("test_crypto_cert")
    
    @classmethod
    def teardown_class(cls):
        """Cleanup test environment"""
        if hasattr(cls, '_test_sdk'):
            cls._test_sdk.cleanup()
    
    def test_cert_creation(self):
        """Test DapCert creation"""
        from dap.crypto.cert import DapCert
        
        cert = DapCert()
        assert cert is not None
    
    def test_cert_methods(self):
        """Test certificate methods"""
        from dap.crypto.cert import DapCert
        
        cert = DapCert()
        assert hasattr(cert, 'generate')
        assert hasattr(cert, 'load')
        assert hasattr(cert, 'save')
        assert hasattr(cert, 'verify')


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 