"""
✍️ DAP Crypto Signatures Module

High-level Python API for DAP SDK digital signature operations.
Provides proper Python classes wrapping C structures.
"""

from enum import Enum, auto
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

class DapMultiSignType(Enum):
    """Types of multi-signature schemes"""
    COMPOSITE = "composite"  # Композиционная мультиподпись (каждая подпись проверяется отдельно)
    AGGREGATED_CHIPMUNK = "aggregated_chipmunk"  # Агрегированная подпись на основе схемы Chipmunk

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

class DapMultiSign:
    """Helper class for creating multi-signatures of different types"""
    
    def __init__(self, sign_type: DapMultiSignType = DapMultiSignType.COMPOSITE):
        """Create new multi-signature object
        
        Args:
            sign_type: Type of multi-signature scheme to use
        """
        self._type = sign_type
        if sign_type == DapMultiSignType.COMPOSITE:
            self._handle = _dap.py_dap_crypto_multi_sign_create()
            if not self._handle:
                raise DapSignError("Failed to create composite multi-signature object")
        elif sign_type == DapMultiSignType.AGGREGATED_CHIPMUNK:
            self._handle = _dap.py_dap_crypto_aggregated_sign_create()
            if not self._handle:
                raise DapSignError("Failed to create aggregated signature object")
        else:
            raise ValueError(f"Unsupported multi-signature type: {sign_type}")
            
    def __del__(self):
        """Clean up when object is destroyed"""
        if not hasattr(self, '_handle') or not self._handle:
            return
            
        if self._type == DapMultiSignType.COMPOSITE:
            _dap.py_dap_crypto_multi_sign_delete(self._handle)
        else:  # AGGREGATED_CHIPMUNK
            _dap.py_dap_crypto_aggregated_sign_delete(self._handle)
        self._handle = None
            
    def add_signature(self, signature: DapSign, key: Optional[DapCryptoKey] = None) -> bool:
        """Add signature to multi-signature
        
        Args:
            signature: Signature to add
            key: Key that created the signature (required for aggregated signatures)
            
        Returns:
            True if signature was added successfully
            
        Raises:
            DapSignError: If signature could not be added
            ValueError: If key is not provided for aggregated signature
        """
        if self._type == DapMultiSignType.COMPOSITE:
            if not _dap.py_dap_crypto_multi_sign_add(self._handle, signature.handle):
                raise DapSignError("Failed to add signature to composite multi-signature")
        else:  # AGGREGATED_CHIPMUNK
            if not key:
                raise ValueError("Key is required for aggregated signatures")
            if not _dap.py_dap_crypto_aggregated_sign_add(self._handle, signature.handle, key.handle):
                raise DapSignError("Failed to add signature to aggregated signature")
        return True
        
    def combine(self) -> DapSign:
        """Combine all signatures into one
        
        Returns:
            Combined signature
            
        Raises:
            DapSignError: If signatures could not be combined
        """
        if self._type == DapMultiSignType.COMPOSITE:
            handle = _dap.py_dap_crypto_multi_sign_combine(self._handle)
        else:  # AGGREGATED_CHIPMUNK
            handle = _dap.py_dap_crypto_aggregated_sign_combine(self._handle)
            
        if not handle:
            raise DapSignError("Failed to combine signatures")
        return DapSign(handle)
        
    @staticmethod
    def verify(combined_sign: DapSign, data: Union[str, bytes], 
               keys: Optional[List[DapCryptoKey]] = None,
               sign_type: DapMultiSignType = DapMultiSignType.COMPOSITE) -> bool:
        """Verify combined signature
        
        Args:
            combined_sign: Combined signature to verify
            data: Original signed data
            keys: List of keys that created the signatures (required for composite signatures)
            sign_type: Type of multi-signature scheme used
            
        Returns:
            True if signature is valid
            
        Raises:
            TypeError: If data is not string or bytes
            ValueError: If keys are not provided for composite signature
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        if sign_type == DapMultiSignType.COMPOSITE:
            if not keys:
                raise ValueError("Keys list is required for composite signatures")
            key_handles = [key.handle for key in keys]
            return _dap.py_dap_crypto_multi_sign_verify(combined_sign.handle, key_handles, data)
        else:  # AGGREGATED_CHIPMUNK
            return _dap.py_dap_crypto_aggregated_sign_verify(combined_sign.handle, data)
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        self.__del__()

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

def quick_multi_sign(keys: List[DapCryptoKey], data: Union[str, bytes], 
                    sign_type: DapMultiSignType = DapMultiSignType.COMPOSITE) -> DapSign:
    """Quick helper to create a multi-signature
    
    Args:
        keys: List of keys to sign with
        data: Data to sign
        sign_type: Type of multi-signature scheme to use
        
    Returns:
        Combined signature
        
    Raises:
        ValueError: If keys list is empty
    """
    if not keys:
        raise ValueError("Keys list is empty")
        
    with DapMultiSign(sign_type) as multi_sign:
        for key in keys:
            signature = quick_sign(key, data)
            multi_sign.add_signature(signature, key if sign_type == DapMultiSignType.AGGREGATED_CHIPMUNK else None)
        return multi_sign.combine() 