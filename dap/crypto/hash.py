"""
🔒 DAP Crypto Hash Module

High-level Python API for DAP SDK hash functions.
Provides proper Python classes wrapping C structures.
"""

from enum import Enum
from typing import Union, Optional
import python_dap as _dap

class DapHashType(Enum):
    """Supported hash types"""
    KECCAK = "keccak"  # Default hash function
    FAST = "keccak"    # Alias for KECCAK
    
    @classmethod
    def default(cls) -> "DapHashType":
        """Get default hash type"""
        return cls.KECCAK

class DapHashError(Exception):
    """Base exception for hash operations"""
    pass

class DapHash:
    """High-level wrapper for DAP SDK hash functions"""
    
    def __init__(self, handle: int):
        """Create hash wrapper from handle
        
        Args:
            handle: C-level hash handle
            
        Raises:
            DapHashError: If handle is invalid
        """
        if not handle:
            raise DapHashError("Invalid hash handle")
        self._handle = handle
        
    @property
    def handle(self) -> int:
        """Get raw hash handle"""
        return self._handle
        
    def __del__(self):
        """Clean up hash when object is destroyed"""
        if hasattr(self, '_handle') and self._handle:
            _dap.py_dap_hash_fast_delete(self._handle)
            self._handle = None
        
    @classmethod
    def create(cls, data: Union[str, bytes], hash_type: DapHashType = DapHashType.default()) -> "DapHash":
        """Create a new hash
        
        Args:
            data: Data to hash
            hash_type: Type of hash to create (default: KECCAK)
            
        Returns:
            New hash object
            
        Raises:
            DapHashError: If hashing fails
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        handle = _dap.py_dap_hash_fast_create(data)
        if not handle:
            raise DapHashError("Failed to create hash")
        return cls(handle)
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        self.__del__()

def quick_hash_fast(data: Union[str, bytes]) -> DapHash:
    """Quick helper to create a hash using default algorithm
    
    Args:
        data: Data to hash
        
    Returns:
        New hash object
    """
    return DapHash.create(data) 