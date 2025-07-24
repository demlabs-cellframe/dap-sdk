"""
✍️ DAP Crypto Signatures Module

High-level Python API for DAP SDK digital signature operations.
Provides proper Python classes wrapping C structures.
"""

from enum import Enum
from typing import Optional, Union, List, Dict
import python_dap as _dap
from .keys import DapCryptoKey, DapKeyType

class DapSignType(Enum):
    """Supported signature types"""
    DILITHIUM = "dilithium"  # Post-quantum signature (default)
    FALCON = "falcon"        # Alternative post-quantum
    PICNIC = "picnic"       # Post-quantum signature
    BLISS = "bliss"         # Legacy signature (deprecated)
    CHIPMUNK = "chipmunk"   # Multi-signature scheme
    
    @classmethod
    def from_key_type(cls, key_type: DapKeyType) -> "DapSignType":
        """Convert key type to signature type"""
        return cls(key_type.value)

class DapSignError(Exception):
    """Base exception for signature operations"""
    pass

class DapSign:
    """High-level wrapper for DAP SDK signatures"""
    
    def __init__(self, handle: int):
        """Create signature wrapper from handle
        
        Args:
            handle: C-level signature handle
            
        Raises:
            DapSignError: If handle is invalid
        """
        if not handle:
            raise DapSignError("Invalid signature handle")
        self._handle = handle
        
    @property
    def handle(self) -> int:
        """Get raw signature handle"""
        return self._handle
        
    @classmethod
    def create(cls, key: DapCryptoKey, data: Union[str, bytes]) -> "DapSign":
        """Create a new signature
        
        Args:
            key: Key to sign with
            data: Data to sign
            
        Returns:
            New signature object
            
        Raises:
            DapSignError: If signing fails
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        handle = key.sign(data)
        return cls(handle)
        
    def verify(self, key: DapCryptoKey, data: Union[str, bytes]) -> bool:
        """Verify this signature
        
        Args:
            key: Key to verify with
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
            
        return key.verify(self._handle, data)

class DapSignatureAggregator:
    """Helper class for aggregating multiple signatures"""
    
    def __init__(self):
        self._signatures: List[DapSign] = []
        self._keys: List[DapCryptoKey] = []
        self._data: Optional[bytes] = None
        
    def add_signature(self, signature: DapSign, key: DapCryptoKey, data: Union[str, bytes]):
        """Add a signature to aggregate
        
        Args:
            signature: Signature to add
            key: Key that created signature
            data: Original signed data
            
        Raises:
            ValueError: If data doesn't match previous signatures
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        if self._data is None:
            self._data = data
        elif data != self._data:
            raise ValueError("All signatures must be for same data")
            
        self._signatures.append(signature)
        self._keys.append(key)
        
    def verify_all(self) -> bool:
        """Verify all signatures
        
        Returns:
            True if all signatures are valid
        """
        if not self._signatures or not self._data:
            return False
            
        return all(sig.verify(key, self._data) 
                  for sig, key in zip(self._signatures, self._keys))

class DapBatchVerifier:
    """Helper class for batch signature verification"""
    
    def __init__(self):
        self._verify_tasks: List[Dict] = []
        
    def add_signature(self, signature: DapSign, key: DapCryptoKey, data: Union[str, bytes]):
        """Add signature to batch verify
        
        Args:
            signature: Signature to verify
            key: Key to verify with
            data: Original signed data
            
        Raises:
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        self._verify_tasks.append({
            'signature': signature,
            'key': key,
            'data': data
        })
        
    def verify_all(self) -> bool:
        """Verify all signatures in batch
        
        Returns:
            True if all signatures are valid
        """
        if not self._verify_tasks:
            return False
            
        return all(task['signature'].verify(task['key'], task['data'])
                  for task in self._verify_tasks)

def quick_sign(key: DapCryptoKey, data: Union[str, bytes]) -> DapSign:
    """Quick helper to create a signature
    
    Args:
        key: Key to sign with
        data: Data to sign
        
    Returns:
        New signature
    """
    return DapSign.create(key, data)

def quick_verify(signature: DapSign, key: DapCryptoKey, data: Union[str, bytes]) -> bool:
    """Quick helper to verify a signature
    
    Args:
        signature: Signature to verify
        key: Key to verify with
        data: Original signed data
        
    Returns:
        True if signature is valid
    """
    return signature.verify(key, data) 