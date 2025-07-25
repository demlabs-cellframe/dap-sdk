"""
🔢 DAP Hash Operations

Direct Python wrapper over DAP hash functions.
Handles various hashing algorithms and operations.
"""

import logging
from typing import Union, Optional
from enum import Enum

# Import DAP hash functions - fallback stubs
try:
    from ..python_dap import (
        dap_hash_fast, dap_hash_slow
    )
except ImportError:
    # Fallback stubs for missing crypto functions
    def dap_hash_fast(data): return b""
    def dap_hash_slow(data): return b""

from ..core.exceptions import DapException


class DapHashType(Enum):
    """DAP supported hash types"""
    # ✅ RECOMMENDED: Quantum-resistant hash algorithms
    KECCAK = "keccak"             # Recommended default (SHA-3 family)
    FAST = "keccak"               # Alias for KECCAK (fast hash)
    
    # ⚠️  DEPRECATED: SHA-2 family (still secure but KECCAK preferred)
    SHA256 = "sha256"             # DEPRECATED: Use KECCAK instead
    SHA512 = "sha512"             # DEPRECATED: Use KECCAK instead


class DapHashError(DapException):
    """DAP Hash specific errors"""
    pass


class DapHash:
    """
    🔢 DAP Hash wrapper
    
    Direct wrapper over dap_hash_* functions.
    Provides access to DAP hashing algorithms.
    
    Example:
        # Fast hash
        fast_hash = DapHash.fast(b"data to hash")
        
        # Slow hash  
        slow_hash = DapHash.slow(b"data to hash")
        
        # Using instance
        hasher = DapHash(DapHashType.FAST)
        result = hasher.hash(b"data")
    """
    
    def __init__(self, hash_type: DapHashType = DapHashType.KECCAK):
        """
        Initialize hash handler
        
        Args:
            hash_type: Type of hash algorithm to use (default: KECCAK)
        """
        self._hash_type = hash_type
        self._logger = logging.getLogger(__name__)
    
    @staticmethod
    def fast(data: Union[bytes, str]) -> bytes:
        """
        Calculate KECCAK hash of data (fast algorithm)
        
        Args:
            data: Data to hash
            
        Returns:
            Hash result bytes
            
        Raises:
            DapHashError: If hashing fails
        """
        if isinstance(data, str):
            data = data.encode('utf-8')
        
        try:
            # Call C function: dap_hash_fast() - KECCAK implementation
            result = dap_hash_fast(data)
            if result is None:
                raise DapHashError("KECCAK hash calculation failed")
            
            logging.getLogger(__name__).debug(
                f"KECCAK hash calculated, input: {len(data)} bytes, output: {len(result)} bytes"
            )
            return result
            
        except Exception as e:
            raise DapHashError(f"KECCAK hash failed: {e}")
    
    def hash(self, data: Union[bytes, str]) -> bytes:
        """
        Hash data using instance hash type
        
        Args:
            data: Data to hash
            
        Returns:
            Hash result bytes
        """
        if self._hash_type in (DapHashType.FAST, DapHashType.KECCAK):
            return self.fast(data)  # Both FAST and KECCAK use fast algorithm
        elif self._hash_type == DapHashType.SHA256:
            # Legacy SHA256 support (deprecated)
            return self.fast(data)  # Fallback to KECCAK
        elif self._hash_type == DapHashType.SHA512:
            # Legacy SHA512 support (deprecated)  
            return self.fast(data)  # Fallback to KECCAK
        else:
            raise DapHashError(f"Unknown hash type: {self._hash_type}")
    
    def hash_fast(self, data: Union[bytes, str]) -> bytes:
        """
        Hash data using fast algorithm (alias for self.fast)
        
        Args:
            data: Data to hash
            
        Returns:
            Hash result bytes
        """
        return self.fast(data)
    
    @property
    def hash_type(self) -> DapHashType:
        """Get hash type"""
        return self._hash_type
    
    @hash_type.setter
    def hash_type(self, value: DapHashType):
        """Set hash type"""
        self._hash_type = value
    
    def __repr__(self) -> str:
        return f"DapHash(type={self._hash_type.value})"


# Convenience functions for quick operations
def quick_hash_fast(data: Union[bytes, str]) -> bytes:
    """Quick fast hash function"""
    return DapHash.fast(data)


__all__ = [
    'DapHash', 
    'DapHashType', 
    'DapHashError',
    'quick_hash_fast',
] 