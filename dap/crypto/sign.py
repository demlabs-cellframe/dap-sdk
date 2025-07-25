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
        """Create signature wrapper from existing handle (internal use only)
        
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
    
    def check_capability(self, capability: str) -> bool:
        """Universal capability checker
        
        Args:
            capability: Capability to check ('quantum_secure', 'deprecated', 
                       'multi_signature', 'aggregated', 'ring_signature', 'zero_knowledge')
            
        Returns:
            True if capability is supported
        """
        return self._metadata.get(capability, False)
    
    def is_quantum_secure(self) -> bool:
        """Check if this signature is quantum secure"""
        return self.check_capability('quantum_secure')
    
    def is_deprecated(self) -> bool:
        """Check if this signature type is deprecated"""
        return self.check_capability('deprecated')
    
    def supports_multi_signature(self) -> bool:
        """Check if this signature type supports multi-signatures"""
        return self.check_capability('multi_signature')
    
    def supports_aggregation(self) -> bool:
        """Check if this signature type supports aggregation"""
        return self.check_capability('aggregated')
    
    def supports_ring_signature(self) -> bool:
        """Check if this signature type supports ring signatures"""
        return self.check_capability('ring_signature')
    
    def supports_zero_knowledge(self) -> bool:
        """Check if this signature type supports zero-knowledge proofs"""
        return self.check_capability('zero_knowledge')
    
    @classmethod
    def _detect_signature_type(cls, key: Optional[DapCryptoKey] = None, 
                              keys: Optional[List[DapCryptoKey]] = None) -> DapSignType:
        """Automatically detect signature type based on parameters
        
        Args:
            key: Single key
            keys: Multiple keys
            
        Returns:
            Detected signature type
        """
        if keys and len(keys) > 1:
            # Multi-signature: default to COMPOSITE, unless all keys are CHIPMUNK
            if all(k.key_type == DapKeyType.CHIPMUNK for k in keys):
                return DapSignType.CHIPMUNK  # Aggregated signature
            else:
                return DapSignType.COMPOSITE  # Composite signature
        elif key:
            # Single signature: infer from key type
            return DapSignType(key.key_type.value)
        elif keys and len(keys) == 1:
            # Single key in list
            return DapSignType(keys[0].key_type.value)
        else:
            raise ValueError("Cannot detect signature type without key information")
        
    # Universal constructor with automatic type detection
    @classmethod
    def create(cls, 
               data: Union[str, bytes],
               key: Optional[DapCryptoKey] = None,
               keys: Optional[List[DapCryptoKey]] = None,
               **kwargs) -> "DapSign":
        """Universal signature constructor with automatic type detection
        
        Args:
            data: Data to sign
            key: Single key (for single signatures)
            keys: Multiple keys (for multi-signatures)
            **kwargs: Additional parameters for future extensions
            
        Returns:
            New signature object
            
        Raises:
            DapSignError: If signing fails
            ValueError: If invalid parameter combination
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
        
        # Auto-detect signature type
        sign_type = cls._detect_signature_type(key, keys)
        
        # Determine signature mode and create
        if keys and len(keys) > 1:
            # Multi-signature mode
            return cls._create_multi_signature(data, keys, sign_type, **kwargs)
        elif key:
            # Single signature mode
            return cls._create_single_signature(data, key, sign_type, **kwargs)
        elif keys and len(keys) == 1:
            # Single key in list
            return cls._create_single_signature(data, keys[0], sign_type, **kwargs)
        else:
            raise ValueError("Either 'key' or 'keys' must be provided")
    
    @classmethod
    def _create_single_signature(cls, 
                                data: bytes,
                                key: DapCryptoKey,
                                sign_type: DapSignType,
                                **kwargs) -> "DapSign":
        """Create single signature (internal)"""
        handle = key.sign(data)
        return cls(handle, sign_type, [key])
    
    @classmethod
    def _create_multi_signature(cls,
                               data: bytes,
                               keys: List[DapCryptoKey],
                               sign_type: DapSignType,
                               **kwargs) -> "DapSign":
        """Create multi-signature (internal)"""
        if not keys:
            raise ValueError("Keys list is empty")
        
        if not DapSignMetadata.supports_multi_signature(sign_type):
            raise ValueError(f"Signature type {sign_type} does not support multi-signatures")
        
        # Create multi-signature based on type
        if sign_type == DapSignType.COMPOSITE:
            return cls._create_composite_signature(data, keys, **kwargs)
        elif sign_type == DapSignType.CHIPMUNK:
            return cls._create_aggregated_signature(data, keys, **kwargs)
        else:
            raise ValueError(f"Unsupported multi-signature type: {sign_type}")
    
    @classmethod
    def _create_composite_signature(cls, data: bytes, keys: List[DapCryptoKey], **kwargs) -> "DapSign":
        """Create composite multi-signature (internal)"""
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
            
            return cls(combined_handle, DapSignType.COMPOSITE, keys)
        finally:
            _dap.py_dap_crypto_multi_sign_delete(multi_sign_handle)
    
    @classmethod
    def _create_aggregated_signature(cls, data: bytes, keys: List[DapCryptoKey], **kwargs) -> "DapSign":
        """Create aggregated signature (internal)"""
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
            
            return cls(combined_handle, DapSignType.CHIPMUNK, keys)
        finally:
            _dap.py_dap_crypto_aggregated_sign_delete(agg_sign_handle)
    
    # Alias constructors for convenience and explicit type control
    @classmethod
    def create_composite(cls, data: Union[str, bytes], keys: List[DapCryptoKey], **kwargs) -> "DapSign":
        """Create composite multi-signature explicitly"""
        if isinstance(data, str):
            data = data.encode()
        return cls._create_composite_signature(data, keys, **kwargs)
    
    @classmethod
    def create_aggregated(cls, data: Union[str, bytes], keys: List[DapCryptoKey], **kwargs) -> "DapSign":
        """Create aggregated multi-signature explicitly"""
        if isinstance(data, str):
            data = data.encode()
        return cls._create_aggregated_signature(data, keys, **kwargs)
    
    # Universal verification method
    def verify(self, data: Union[str, bytes], 
               key: Optional[DapCryptoKey] = None,
               keys: Optional[List[DapCryptoKey]] = None) -> bool:
        """Universal signature verification
        
        Args:
            data: Original signed data
            key: Key for single signature verification
            keys: Keys for multi-signature verification (uses stored keys if not provided)
            
        Returns:
            True if signature is valid
            
        Raises:
            ValueError: If verification parameters are invalid
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
        
        # Single signature verification
        if not self.supports_multi_signature():
            if not key:
                if self._keys:
                    key = self._keys[0]
                else:
                    raise ValueError("Key is required for single signature verification")
            return key.verify(self._handle, data)
        
        # Multi-signature verification
        verify_keys = keys or self._keys
        if not verify_keys:
            raise ValueError("Keys are required for multi-signature verification")
        
        if self._type == DapSignType.COMPOSITE:
            key_handles = [k.handle for k in verify_keys]
            return _dap.py_dap_crypto_multi_sign_verify(self._handle, key_handles, data)
        elif self._type == DapSignType.CHIPMUNK:
            return _dap.py_dap_crypto_aggregated_sign_verify(self._handle, data)
        else:
            raise ValueError(f"Unsupported multi-signature type: {self._type}")

# Unified helper functions with overloading support
def quick_sign(data: Union[str, bytes],
               key: Optional[DapCryptoKey] = None,
               keys: Optional[List[DapCryptoKey]] = None,
               **kwargs) -> DapSign:
    """Universal quick signature creation
    
    Args:
        data: Data to sign
        key: Single key (for single signatures)
        keys: Multiple keys (for multi-signatures)
        **kwargs: Additional parameters
        
    Returns:
        New signature
    """
    return DapSign.create(data, key, keys, **kwargs)

def quick_verify(signature: DapSign, 
                data: Union[str, bytes],
                key: Optional[DapCryptoKey] = None,
                keys: Optional[List[DapCryptoKey]] = None) -> bool:
    """Universal quick signature verification
    
    Args:
        signature: Signature to verify
        data: Original signed data
        key: Key for single signature verification
        keys: Keys for multi-signature verification
        
    Returns:
        True if signature is valid
    """
    return signature.verify(data, key, keys)

# Specific convenience functions
def quick_multi_sign(data: Union[str, bytes],
                    keys: List[DapCryptoKey],
                    **kwargs) -> DapSign:
    """Quick multi-signature creation (auto-detects type)"""
    return DapSign.create(data, keys=keys, **kwargs)

def quick_composite_sign(data: Union[str, bytes], keys: List[DapCryptoKey], **kwargs) -> DapSign:
    """Quick composite multi-signature creation"""
    return DapSign.create_composite(data, keys, **kwargs)

def quick_aggregated_sign(data: Union[str, bytes], keys: List[DapCryptoKey], **kwargs) -> DapSign:
    """Quick aggregated signature creation"""
    return DapSign.create_aggregated(data, keys, **kwargs)

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