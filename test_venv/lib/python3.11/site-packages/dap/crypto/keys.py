"""
🔐 DAP Crypto Keys

Cryptographic key management for DAP operations.
Clean API without fallbacks or mocks.
"""

from typing import Optional, Union, Any
import logging
from enum import Enum

logger = logging.getLogger(__name__)


class DapKeyType(Enum):
    """Key type enumeration."""
    RSA = "rsa"
    ECDSA = "ecdsa"
    DILITHIUM = "dilithium"


class DapKeyError(Exception):
    """Key operation error."""
    pass


class DapKeyManager:
    """
    Key manager for DAP crypto operations.
    """
    
    def __init__(self, key_type: str = "sig_dilithium"):
        """
        Initialize key manager.
        
        Args:
            key_type: Type of cryptographic key
        """
        self.key_type = key_type
        self._keys = {}
    
    def generate_key(self, key_name: str) -> bool:
        """
        Generate a new cryptographic key.
        
        Args:
            key_name: Name for the key
            
        Returns:
            True if key was generated successfully
        """
        try:
            # Native key generation implementation
            from DAP.Crypto import generate_key_native
            key_data = generate_key_native(self.key_type)
            self._keys[key_name] = key_data
            return True
        except ImportError:
            raise DapKeyError("Native crypto implementation missing")
        except Exception as e:
            raise DapKeyError(f"Failed to generate key: {e}")
    
    def get_key(self, key_name: str) -> Optional[Any]:
        """
        Get key by name.
        
        Args:
            key_name: Name of the key
            
        Returns:
            Key data or None if not found
        """
        return self._keys.get(key_name)
    
    def delete_key(self, key_name: str) -> bool:
        """
        Delete key by name.
        
        Args:
            key_name: Name of the key to delete
            
        Returns:
            True if key was deleted successfully
        """
        if key_name in self._keys:
            del self._keys[key_name]
            return True
        return False
    
    def list_keys(self) -> list:
        """
        List all available keys.
        
        Returns:
            List of key names
        """
        return list(self._keys.keys())


class DapCryptoKey:
    """
    Individual crypto key class.
    """
    
    def __init__(self, key_handle: Any):
        """
        Initialize crypto key.
        
        Args:
            key_handle: Native key handle
        """
        self._key_handle = key_handle
        self.key_type = None
        
    @property
    def handle(self) -> Any:
        """Get key handle for compatibility with tests."""
        return self._key_handle
        
    def get_public_key(self) -> bytes:
        """Get public key bytes."""
        if not self._key_handle:
            raise DapKeyError("Key not initialized")
        
        try:
            return self._key_handle.get_public_key()
        except Exception as e:
            raise DapKeyError(f"Failed to get public key: {e}")
    
    def get_private_key(self) -> bytes:
        """Get private key bytes."""
        if not self._key_handle:
            raise DapKeyError("Key not initialized")
        
        try:
            return self._key_handle.get_private_key()
        except Exception as e:
            raise DapKeyError(f"Failed to get private key: {e}")
    
    def sign(self, data: bytes) -> bytes:
        """Sign data with this key."""
        if not self._key_handle:
            raise DapKeyError("Key not initialized")
        
        try:
            return self._key_handle.sign(data)
        except Exception as e:
            raise DapKeyError(f"Failed to sign data: {e}")
    
    def verify(self, data: bytes, signature: bytes) -> bool:
        """Verify signature with this key."""
        if not self._key_handle:
            raise DapKeyError("Key not initialized")
        
        try:
            return self._key_handle.verify(data, signature)
        except Exception as e:
            raise DapKeyError(f"Failed to verify signature: {e}")


# Clean API - no legacy aliases 