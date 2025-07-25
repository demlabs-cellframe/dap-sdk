"""
✍️ DAP Crypto Signatures Module

Unified signature system for DAP SDK with comprehensive metadata and capabilities.
Supports all types of signatures: single, multi-signature, ring signatures, zero-knowledge proofs.
"""

from enum import Enum, auto
from typing import Optional, Union, List, Dict, Set
import python_dap as _dap
from .keys import DapCryptoKey, DapKeyType

class DapSignType(Enum):
    """All supported signature types with metadata"""
    # Post-quantum signatures (recommended)
    DILITHIUM = "dilithium"
    FALCON = "falcon"
    PICNIC = "picnic"
    
    # Multi-signature schemes
    COMPOSITE = "composite"    # Композиционная мультиподпись
    CHIPMUNK = "chipmunk"     # Бурундучья подпись (поддерживает агрегацию)
    
    # Ring signatures (future)
    RING_DILITHIUM = "ring_dilithium"
    RING_FALCON = "ring_falcon"
    
    # Zero-knowledge signatures (future)
    ZK_DILITHIUM = "zk_dilithium"
    ZK_FALCON = "zk_falcon"
    
    # Deprecated (quantum-vulnerable)
    BLISS = "bliss"          # DEPRECATED
    RSA = "rsa"              # DEPRECATED
    ECDSA = "ecdsa"          # DEPRECATED
    ED25519 = "ed25519"      # DEPRECATED

class DapSignMetadata:
    """Metadata for signature types"""
    
    _METADATA = {
        # Post-quantum signatures
        DapSignType.DILITHIUM: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 2592,
            'signature_size': 3293
        },
        DapSignType.FALCON: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 1793,
            'signature_size': 666
        },
        DapSignType.PICNIC: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': True,
            'key_size': 49,
            'signature_size': 34036
        },
        
        # Multi-signature schemes
        DapSignType.COMPOSITE: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': True,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': None,  # Depends on constituent signatures
            'signature_size': None
        },
        DapSignType.CHIPMUNK: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': True,
            'aggregated': True,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 1024,
            'signature_size': 512
        },
        
        # Ring signatures (future implementations)
        DapSignType.RING_DILITHIUM: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': True,
            'zero_knowledge': True,
            'key_size': 2592,
            'signature_size': None  # Depends on ring size
        },
        DapSignType.RING_FALCON: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': True,
            'zero_knowledge': True,
            'key_size': 1793,
            'signature_size': None
        },
        
        # Zero-knowledge signatures (future)
        DapSignType.ZK_DILITHIUM: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': True,
            'key_size': 2592,
            'signature_size': None
        },
        DapSignType.ZK_FALCON: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': True,
            'key_size': 1793,
            'signature_size': None
        },
        
        # Deprecated signatures
        DapSignType.BLISS: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 7168,
            'signature_size': 5664
        },
        DapSignType.RSA: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 4096,
            'signature_size': 512
        },
        DapSignType.ECDSA: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 256,
            'signature_size': 64
        },
        DapSignType.ED25519: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'ring_signature': False,
            'zero_knowledge': False,
            'key_size': 256,
            'signature_size': 64
        }
    }
    
    @classmethod
    def get_metadata(cls, sign_type: DapSignType) -> Dict:
        """Get metadata for signature type"""
        return cls._METADATA.get(sign_type, {})
    
    @classmethod
    def is_quantum_secure(cls, sign_type: DapSignType) -> bool:
        """Check if signature type is quantum secure"""
        return cls._METADATA.get(sign_type, {}).get('quantum_secure', False)
    
    @classmethod
    def is_deprecated(cls, sign_type: DapSignType) -> bool:
        """Check if signature type is deprecated"""
        return cls._METADATA.get(sign_type, {}).get('deprecated', False)
    
    @classmethod
    def supports_multi_signature(cls, sign_type: DapSignType) -> bool:
        """Check if signature type supports multi-signatures"""
        return cls._METADATA.get(sign_type, {}).get('multi_signature', False)
    
    @classmethod
    def supports_aggregation(cls, sign_type: DapSignType) -> bool:
        """Check if signature type supports aggregation"""
        return cls._METADATA.get(sign_type, {}).get('aggregated', False)
    
    @classmethod
    def supports_ring_signature(cls, sign_type: DapSignType) -> bool:
        """Check if signature type supports ring signatures"""
        return cls._METADATA.get(sign_type, {}).get('ring_signature', False)
    
    @classmethod
    def supports_zero_knowledge(cls, sign_type: DapSignType) -> bool:
        """Check if signature type supports zero-knowledge proofs"""
        return cls._METADATA.get(sign_type, {}).get('zero_knowledge', False)
    
    @classmethod
    def get_recommended_types(cls) -> List[DapSignType]:
        """Get list of recommended (quantum-secure, non-deprecated) signature types"""
        return [t for t in DapSignType if cls.is_quantum_secure(t) and not cls.is_deprecated(t)]
    
    @classmethod
    def get_deprecated_types(cls) -> List[DapSignType]:
        """Get list of deprecated signature types"""
        return [t for t in DapSignType if cls.is_deprecated(t)]

class DapSignError(Exception):
    """Base exception for signature operations"""
    pass

class DapSign:
    """Unified signature class supporting all signature types and operations"""
    
    def __init__(self, handle: int, sign_type: DapSignType, keys: Optional[List[DapCryptoKey]] = None):
        """Create signature wrapper from handle
        
        Args:
            handle: C-level signature handle
            sign_type: Type of signature
            keys: List of keys (for multi-signatures)
            
        Raises:
            DapSignError: If handle is invalid
        """
        if not handle:
            raise DapSignError("Invalid signature handle")
        self._handle = handle
        self._type = sign_type
        self._keys = keys or []
        self._metadata = DapSignMetadata.get_metadata(sign_type)
        
    @property
    def handle(self) -> int:
        """Get raw signature handle"""
        return self._handle
    
    @property
    def sign_type(self) -> DapSignType:
        """Get signature type"""
        return self._type
    
    @property
    def keys(self) -> List[DapCryptoKey]:
        """Get associated keys (for multi-signatures)"""
        return self._keys.copy()
    
    @property
    def metadata(self) -> Dict:
        """Get signature metadata"""
        return self._metadata.copy()
    
    def is_quantum_secure(self) -> bool:
        """Check if this signature is quantum secure"""
        return DapSignMetadata.is_quantum_secure(self._type)
    
    def is_deprecated(self) -> bool:
        """Check if this signature type is deprecated"""
        return DapSignMetadata.is_deprecated(self._type)
    
    def supports_multi_signature(self) -> bool:
        """Check if this signature type supports multi-signatures"""
        return DapSignMetadata.supports_multi_signature(self._type)
    
    def supports_aggregation(self) -> bool:
        """Check if this signature type supports aggregation"""
        return DapSignMetadata.supports_aggregation(self._type)
    
    def supports_ring_signature(self) -> bool:
        """Check if this signature type supports ring signatures"""
        return DapSignMetadata.supports_ring_signature(self._type)
    
    def supports_zero_knowledge(self) -> bool:
        """Check if this signature type supports zero-knowledge proofs"""
        return DapSignMetadata.supports_zero_knowledge(self._type)
        
    @classmethod
    def create(cls, key: DapCryptoKey, data: Union[str, bytes], 
               sign_type: Optional[DapSignType] = None) -> "DapSign":
        """Create a new signature
        
        Args:
            key: Key to sign with
            data: Data to sign
            sign_type: Signature type (inferred from key if not provided)
            
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
        
        if sign_type is None:
            # Infer signature type from key type
            sign_type = DapSignType(key.key_type.value)
            
        handle = key.sign(data)
        return cls(handle, sign_type, [key])
        
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
    
    @classmethod
    def create_multi_signature(cls, keys: List[DapCryptoKey], data: Union[str, bytes],
                              sign_type: DapSignType = DapSignType.COMPOSITE,
                              aggregated: bool = False) -> "DapSign":
        """Create a multi-signature
        
        Args:
            keys: List of keys to sign with
            data: Data to sign
            sign_type: Multi-signature type
            aggregated: Whether to use aggregation (for supported types)
            
        Returns:
            Combined signature
            
        Raises:
            ValueError: If invalid parameters or unsupported operation
            DapSignError: If signing fails
        """
        if not keys:
            raise ValueError("Keys list is empty")
        
        if not DapSignMetadata.supports_multi_signature(sign_type):
            raise ValueError(f"Signature type {sign_type} does not support multi-signatures")
        
        if aggregated and not DapSignMetadata.supports_aggregation(sign_type):
            raise ValueError(f"Signature type {sign_type} does not support aggregation")
        
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
        
        # Create multi-signature based on type and aggregation
        if sign_type == DapSignType.COMPOSITE:
            multi_sign_handle = _dap.py_dap_crypto_multi_sign_create()
            if not multi_sign_handle:
                raise DapSignError("Failed to create composite multi-signature")
            
            try:
                # Add individual signatures
                for key in keys:
                    sig_handle = key.sign(data)
                    if not _dap.py_dap_crypto_multi_sign_add(multi_sign_handle, sig_handle):
                        raise DapSignError("Failed to add signature to composite multi-signature")
                
                # Combine signatures
                combined_handle = _dap.py_dap_crypto_multi_sign_combine(multi_sign_handle)
                if not combined_handle:
                    raise DapSignError("Failed to combine composite signatures")
                
                return cls(combined_handle, sign_type, keys)
            finally:
                _dap.py_dap_crypto_multi_sign_delete(multi_sign_handle)
        
        elif sign_type == DapSignType.CHIPMUNK and aggregated:
            agg_sign_handle = _dap.py_dap_crypto_aggregated_sign_create()
            if not agg_sign_handle:
                raise DapSignError("Failed to create aggregated signature")
            
            try:
                # Add individual signatures with keys
                for key in keys:
                    sig_handle = key.sign(data)
                    if not _dap.py_dap_crypto_aggregated_sign_add(agg_sign_handle, sig_handle, key.handle):
                        raise DapSignError("Failed to add signature to aggregated signature")
                
                # Combine signatures
                combined_handle = _dap.py_dap_crypto_aggregated_sign_combine(agg_sign_handle)
                if not combined_handle:
                    raise DapSignError("Failed to combine aggregated signatures")
                
                return cls(combined_handle, sign_type, keys)
            finally:
                _dap.py_dap_crypto_aggregated_sign_delete(agg_sign_handle)
        
        else:
            raise ValueError(f"Unsupported multi-signature configuration: {sign_type}, aggregated={aggregated}")
    
    def verify_multi_signature(self, data: Union[str, bytes]) -> bool:
        """Verify multi-signature
        
        Args:
            data: Original signed data
            
        Returns:
            True if signature is valid
            
        Raises:
            ValueError: If not a multi-signature
            TypeError: If data is not string or bytes
        """
        if not self.supports_multi_signature():
            raise ValueError("This is not a multi-signature")
        
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
        
        if self._type == DapSignType.COMPOSITE:
            if not self._keys:
                raise ValueError("Keys list is required for composite signature verification")
            key_handles = [key.handle for key in self._keys]
            return _dap.py_dap_crypto_multi_sign_verify(self._handle, key_handles, data)
        elif self._type == DapSignType.CHIPMUNK and self.supports_aggregation():
            return _dap.py_dap_crypto_aggregated_sign_verify(self._handle, data)
        else:
            raise ValueError(f"Unsupported multi-signature type: {self._type}")

def quick_sign(key: DapCryptoKey, data: Union[str, bytes], 
               sign_type: Optional[DapSignType] = None) -> DapSign:
    """Quick helper to create a signature
    
    Args:
        key: Key to sign with
        data: Data to sign
        sign_type: Signature type (inferred from key if not provided)
        
    Returns:
        New signature
    """
    return DapSign.create(key, data, sign_type)

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
                    sign_type: DapSignType = DapSignType.COMPOSITE,
                    aggregated: bool = False) -> DapSign:
    """Quick helper to create a multi-signature
    
    Args:
        keys: List of keys to sign with
        data: Data to sign
        sign_type: Multi-signature type
        aggregated: Whether to use aggregation (for supported types)
        
    Returns:
        Combined signature
        
    Raises:
        ValueError: If invalid parameters
    """
    return DapSign.create_multi_signature(keys, data, sign_type, aggregated)

def get_recommended_signature_types() -> List[DapSignType]:
    """Get list of recommended signature types (quantum-secure, non-deprecated)"""
    return DapSignMetadata.get_recommended_types()

def get_deprecated_signature_types() -> List[DapSignType]:
    """Get list of deprecated signature types"""
    return DapSignMetadata.get_deprecated_types()

def check_signature_compatibility(sign_type: DapSignType) -> Dict[str, bool]:
    """Check signature type capabilities
    
    Args:
        sign_type: Signature type to check
        
    Returns:
        Dictionary with capability flags
    """
    return {
        'quantum_secure': DapSignMetadata.is_quantum_secure(sign_type),
        'deprecated': DapSignMetadata.is_deprecated(sign_type),
        'multi_signature': DapSignMetadata.supports_multi_signature(sign_type),
        'aggregated': DapSignMetadata.supports_aggregation(sign_type),
        'ring_signature': DapSignMetadata.supports_ring_signature(sign_type),
        'zero_knowledge': DapSignMetadata.supports_zero_knowledge(sign_type)
    } 