"""
🔑 DAP Crypto Keys Module

High-level Python API for DAP SDK cryptographic key operations.
Provides proper Python classes wrapping C structures.
"""

from enum import Enum, auto
from typing import Optional, Union, Tuple
import python_dap as _dap

class DapKeyType(Enum):
    """Supported cryptographic key types"""
    DILITHIUM = "dilithium"  # Post-quantum signature scheme (default)
    FALCON = "falcon"        # Alternative post-quantum signature
    PICNIC = "picnic"       # Post-quantum signature scheme
    BLISS = "bliss"         # Legacy signature scheme (deprecated)
    CHIPMUNK = "chipmunk"   # Multi-signature scheme
    
    @classmethod
    def from_string(cls, key_type: str) -> "DapKeyType":
        """Convert string to DapKeyType"""
        try:
            return cls(key_type.lower())
        except ValueError:
            return cls.DILITHIUM  # Default to DILITHIUM

class DapKeyError(Exception):
    """Base exception for DAP key operations"""
    pass

class DapCryptoKey:
    """High-level wrapper for DAP SDK cryptographic keys"""
    
    def __init__(self, key_type: Union[str, DapKeyType] = DapKeyType.DILITHIUM, seed: Optional[bytes] = None):
        """Create a new cryptographic key
        
        Args:
            key_type: Type of key to create (default: DILITHIUM)
            seed: Optional seed for deterministic key generation
            
        Raises:
            DapKeyError: If key creation fails
        """
        if isinstance(key_type, str):
            key_type = DapKeyType.from_string(key_type)
            
        if seed is not None:
            self._handle = _dap.py_dap_crypto_key_create_from_seed(key_type.value, seed)
        else:
            self._handle = _dap.py_dap_crypto_key_create(key_type.value)
            
        if not self._handle:
            raise DapKeyError(f"Failed to create {key_type.value} key")
            
        self._key_type = key_type
        
    def __del__(self):
        """Clean up key when object is destroyed"""
        if hasattr(self, '_handle') and self._handle:
            _dap.py_dap_crypto_key_delete(self._handle)
            self._handle = None
            
    @property
    def key_type(self) -> DapKeyType:
        """Get the type of this key"""
        return self._key_type
        
    def sign(self, data: Union[str, bytes]) -> int:
        """Create a signature for data
        
        Args:
            data: Data to sign (string or bytes)
            
        Returns:
            Handle to signature object
            
        Raises:
            DapKeyError: If signing fails
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        sign = _dap.py_dap_crypto_key_sign(self._handle, data)
        if not sign:
            raise DapKeyError("Failed to create signature")
        return sign
        
    def verify(self, signature: int, data: Union[str, bytes]) -> bool:
        """Verify a signature
        
        Args:
            signature: Signature handle to verify
            data: Original signed data
            
        Returns:
            True if signature is valid
            
        Raises:
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        return _dap.py_dap_crypto_key_verify(signature, self._handle, data)
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        self.__del__()
        
class DapKeyManager:
    """Helper class for managing multiple keys"""
    
    def __init__(self):
        self._keys = {}
        
    def create_key(self, name: str, key_type: Union[str, DapKeyType] = DapKeyType.DILITHIUM,
                  seed: Optional[bytes] = None) -> DapCryptoKey:
        """Create and store a new key
        
        Args:
            name: Name to store key under
            key_type: Type of key to create
            seed: Optional seed for deterministic generation
            
        Returns:
            Created key
            
        Raises:
            KeyError: If key with name already exists
        """
        if name in self._keys:
            raise KeyError(f"Key '{name}' already exists")
            
        key = DapCryptoKey(key_type, seed)
        self._keys[name] = key
        return key
        
    def get_key(self, name: str) -> DapCryptoKey:
        """Get a stored key by name
        
        Args:
            name: Name of key to get
            
        Returns:
            Stored key
            
        Raises:
            KeyError: If key does not exist
        """
        if name not in self._keys:
            raise KeyError(f"Key '{name}' does not exist")
        return self._keys[name]
        
    def delete_key(self, name: str):
        """Delete a stored key
        
        Args:
            name: Name of key to delete
            
        Raises:
            KeyError: If key does not exist
        """
        if name not in self._keys:
            raise KeyError(f"Key '{name}' does not exist")
        del self._keys[name]
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up all keys on context manager exit"""
        for key in list(self._keys.values()):
            key.__del__()
        self._keys.clear() 