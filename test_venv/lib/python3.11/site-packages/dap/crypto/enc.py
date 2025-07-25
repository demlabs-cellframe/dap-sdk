"""
🔐 DAP Encryption Operations

Encryption and decryption operations for DAP.
Provides symmetric and asymmetric encryption capabilities.
"""

import logging
from typing import Union, Optional, Any
from enum import Enum

from ..core.exceptions import DapException

logger = logging.getLogger(__name__)


class DapEncError(DapException):
    """Encryption operation error."""
    pass


class DapEncType(Enum):
    """Encryption algorithm types."""
    AES256 = "aes256"
    RSA = "rsa"
    ChaCha20 = "chacha20"
    DEFAULT = "aes256"


class DapEnc:
    """
    Encryption and decryption operations.
    
    Example:
        # Create encryptor
        enc = DapEnc(DapEncType.AES256)
        
        # Encrypt data
        encrypted = enc.encrypt(b"secret data", b"password")
        
        # Decrypt data
        decrypted = enc.decrypt(encrypted, b"password")
    """
    
    def __init__(self, enc_type: DapEncType = DapEncType.DEFAULT):
        """
        Initialize encryption handler.
        
        Args:
            enc_type: Encryption algorithm type
        """
        self._enc_type = enc_type
        self._logger = logging.getLogger(__name__)
    
    def encrypt(self, data: Union[bytes, str], key: Union[bytes, str]) -> bytes:
        """
        Encrypt data with key.
        
        Args:
            data: Data to encrypt
            key: Encryption key
            
        Returns:
            Encrypted data bytes
            
        Raises:
            DapEncError: If encryption fails
        """
        if isinstance(data, str):
            data = data.encode('utf-8')
        if isinstance(key, str):
            key = key.encode('utf-8')
        
        try:
            # Stub implementation - in real version would call C functions
            # For testing purposes, return mock encrypted data
            return b"ENCRYPTED:" + data + b":KEY:" + key
            
        except Exception as e:
            raise DapEncError(f"Encryption failed: {e}")
    
    def decrypt(self, encrypted_data: bytes, key: Union[bytes, str]) -> bytes:
        """
        Decrypt data with key.
        
        Args:
            encrypted_data: Encrypted data
            key: Decryption key
            
        Returns:
            Decrypted data bytes
            
        Raises:
            DapEncError: If decryption fails
        """
        if isinstance(key, str):
            key = key.encode('utf-8')
        
        try:
            # Stub implementation - reverse of encrypt
            if encrypted_data.startswith(b"ENCRYPTED:"):
                # Extract original data (very basic stub)
                parts = encrypted_data.split(b":KEY:")
                if len(parts) == 2:
                    original_data = parts[0][10:]  # Remove "ENCRYPTED:" prefix
                    return original_data
            
            raise DapEncError("Invalid encrypted data format")
            
        except Exception as e:
            raise DapEncError(f"Decryption failed: {e}")
    
    def key_new_generate(self, key_size: int = 256) -> bytes:
        """
        Generate new encryption key.
        
        Args:
            key_size: Key size in bits
            
        Returns:
            Generated key bytes
            
        Raises:
            DapEncError: If key generation fails
        """
        try:
            # Stub implementation - generate mock key
            import os
            key_bytes = key_size // 8
            return os.urandom(key_bytes)
            
        except Exception as e:
            raise DapEncError(f"Key generation failed: {e}")
    
    def key_delete(self, key: Union[bytes, str]) -> bool:
        """
        Delete/clear encryption key from memory.
        
        Args:
            key: Key to delete
            
        Returns:
            True if key was deleted successfully
            
        Raises:
            DapEncError: If key deletion fails
        """
        try:
            # Stub implementation - always return success
            return True
            
        except Exception as e:
            raise DapEncError(f"Key deletion failed: {e}")
    
    @property
    def enc_type(self) -> DapEncType:
        """Get encryption type."""
        return self._enc_type
    
    @enc_type.setter
    def enc_type(self, value: DapEncType):
        """Set encryption type."""
        self._enc_type = value
    
    def __repr__(self) -> str:
        return f"DapEnc(type={self._enc_type.value})"


# Convenience functions
def quick_encrypt(data: Union[bytes, str], key: Union[bytes, str], 
                 enc_type: DapEncType = DapEncType.DEFAULT) -> bytes:
    """Quick encryption function."""
    enc = DapEnc(enc_type)
    return enc.encrypt(data, key)


def quick_decrypt(encrypted_data: bytes, key: Union[bytes, str],
                 enc_type: DapEncType = DapEncType.DEFAULT) -> bytes:
    """Quick decryption function."""
    enc = DapEnc(enc_type)
    return enc.decrypt(encrypted_data, key)


__all__ = [
    'DapEnc',
    'DapEncType', 
    'DapEncError',
    'quick_encrypt',
    'quick_decrypt'
] 