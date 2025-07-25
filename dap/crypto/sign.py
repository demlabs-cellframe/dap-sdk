"""
✍️ DAP Crypto Signatures Module

Unified signature system for DAP SDK with single constructor.
Supports all types of signatures through smart parameter detection.
"""

from enum import Enum
from typing import Optional, Union, List, Dict
import python_dap as _dap
from .keys import DapCryptoKey, DapKeyType

class DapSignType(Enum):
    """Real supported signature types from DAP SDK"""
    # Post-quantum signatures (recommended)
    DILITHIUM = "dilithium"
    FALCON = "falcon"
    PICNIC = "picnic"
    SPHINCSPLUS = "sphincsplus"
    
    # Multi-signature schemes
    COMPOSITE = "composite"    # Композиционная мультиподпись (DAP_PKEY_TYPE_SIG_MULTI)
    CHIPMUNK = "chipmunk"     # Бурундучья подпись (поддерживает агрегацию)
    
    # Deprecated (but still in DAP SDK)
    BLISS = "bliss"          # DEPRECATED - quantum vulnerable
    ECDSA = "ecdsa"          # DEPRECATED - quantum vulnerable
    TESLA = "tesla"          # Experimental
    SHIPOVNIK = "shipovnik"  # Experimental

class DapSignMetadata:
    """Metadata for signature types"""
    
    _METADATA = {
        # Post-quantum signatures (recommended)
        DapSignType.DILITHIUM: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 2592,
            'signature_size': 3293
        },
        DapSignType.FALCON: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 1793,
            'signature_size': 666
        },
        DapSignType.PICNIC: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 49,
            'signature_size': 34036
        },
        DapSignType.SPHINCSPLUS: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 64,
            'signature_size': 17088
        },
        
        # Multi-signature schemes
        DapSignType.COMPOSITE: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': True,
            'aggregated': False,
            'key_size': None,  # Depends on constituent signatures
            'signature_size': None
        },
        DapSignType.CHIPMUNK: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': True,
            'aggregated': True,
            'key_size': 1024,
            'signature_size': 512
        },
        
        # Deprecated signatures
        DapSignType.BLISS: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 7168,
            'signature_size': 5664
        },
        DapSignType.ECDSA: {
            'quantum_secure': False,
            'deprecated': True,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 256,
            'signature_size': 64
        },
        DapSignType.TESLA: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 1024,
            'signature_size': 2048
        },
        DapSignType.SHIPOVNIK: {
            'quantum_secure': True,
            'deprecated': False,
            'multi_signature': False,
            'aggregated': False,
            'key_size': 512,
            'signature_size': 1024
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

class DapSignError(Exception):
    """Base exception for signature operations"""
    pass

class DapSign:
    """Unified signature class with single constructor supporting all operations"""
    
    @staticmethod
    def sign(to_sign: Union[str, bytes],
             key: Optional[DapCryptoKey] = None,
             keys: Optional[List[DapCryptoKey]] = None,
             sign_type: Optional[DapSignType] = None) -> "DapSign":
        """Static signing function - creates and returns signature
        
        Args:
            to_sign: Binary object to sign
            key: Single key for single signature
            keys: Multiple keys for multi-signature
            sign_type: Explicit signature type (optional)
            
        Returns:
            New DapSign instance with the signature
            
        Raises:
            DapSignError: If signing fails
            ValueError: If invalid parameter combination
        """
        if isinstance(to_sign, str):
            to_sign = to_sign.encode()
        elif not isinstance(to_sign, bytes):
            raise TypeError("to_sign must be string or bytes")
        
        # Single signature
        if key is not None:
            if keys is not None:
                raise ValueError("Cannot specify both 'key' and 'keys'")
            return DapSign._create_single_signature_static(to_sign, key, sign_type)
        
        # Multi-signature
        if keys is not None:
            if not keys:
                raise ValueError("Keys list is empty")
            return DapSign._create_multi_signature_static(to_sign, keys, sign_type)
        
        raise ValueError("Either 'key' or 'keys' must be provided for signature creation")
    
    @staticmethod
    def _create_single_signature_static(to_sign: bytes, key: DapCryptoKey, sign_type: Optional[DapSignType]) -> "DapSign":
        """Create single signature (static version)"""
        # Determine signature type
        if sign_type is None:
            # Auto-detect from key type
            sign_type = DapSignType(key.key_type.value)
        
        # Create signature
        handle = key.sign(to_sign)
        if not handle:
            raise DapSignError("Failed to create single signature")
        
        # Create instance using handle wrapper mode
        return DapSign(handle=handle, sign_type=sign_type, keys=[key])
    
    @staticmethod
    def _create_multi_signature_static(to_sign: bytes, keys: List[DapCryptoKey], sign_type: Optional[DapSignType]) -> "DapSign":
        """Create multi-signature (static version)"""
        # Auto-detect signature type if not specified
        if sign_type is None:
            if all(k.key_type == DapKeyType.CHIPMUNK for k in keys):
                sign_type = DapSignType.CHIPMUNK  # Aggregated
            else:
                sign_type = DapSignType.COMPOSITE  # Composite
        
        # Validate multi-signature support
        if not DapSignMetadata.supports_multi_signature(sign_type):
            raise ValueError(f"Signature type {sign_type} does not support multi-signatures")
        
        # Create based on type
        if sign_type == DapSignType.COMPOSITE:
            handle = DapSign._create_composite_static(to_sign, keys)
        elif sign_type == DapSignType.CHIPMUNK:
            handle = DapSign._create_aggregated_static(to_sign, keys)
        else:
            raise ValueError(f"Unsupported multi-signature type: {sign_type}")
        
        # Create instance using handle wrapper mode
        return DapSign(handle=handle, sign_type=sign_type, keys=keys)
    
    @staticmethod
    def _create_composite_static(to_sign: bytes, keys: List[DapCryptoKey]) -> int:
        """Create composite multi-signature (static version)"""
        multi_sign_handle = _dap.py_dap_crypto_multi_sign_create()
        if not multi_sign_handle:
            raise DapSignError("Failed to create composite multi-signature")
        
        try:
            # Add individual signatures
            for key in keys:
                sig_handle = key.sign(to_sign)
                if not _dap.py_dap_crypto_multi_sign_add(multi_sign_handle, sig_handle):
                    raise DapSignError("Failed to add signature to composite multi-signature")
            
            # Combine signatures
            combined_handle = _dap.py_dap_crypto_multi_sign_combine(multi_sign_handle)
            if not combined_handle:
                raise DapSignError("Failed to combine composite signatures")
            
            return combined_handle
        finally:
            _dap.py_dap_crypto_multi_sign_delete(multi_sign_handle)
    
    @staticmethod
    def _create_aggregated_static(to_sign: bytes, keys: List[DapCryptoKey]) -> int:
        """Create aggregated signature (static version)"""
        agg_sign_handle = _dap.py_dap_crypto_aggregated_sign_create()
        if not agg_sign_handle:
            raise DapSignError("Failed to create aggregated signature")
        
        try:
            # Add individual signatures with keys
            for key in keys:
                sig_handle = key.sign(to_sign)
                if not _dap.py_dap_crypto_aggregated_sign_add(agg_sign_handle, sig_handle, key.handle):
                    raise DapSignError("Failed to add signature to aggregated signature")
            
            # Combine signatures
            combined_handle = _dap.py_dap_crypto_aggregated_sign_combine(agg_sign_handle)
            if not combined_handle:
                raise DapSignError("Failed to combine aggregated signatures")
            
            return combined_handle
        finally:
            _dap.py_dap_crypto_aggregated_sign_delete(agg_sign_handle)
    
    def __init__(self, 
                 to_sign: Optional[Union[str, bytes]] = None,
                 key: Optional[DapCryptoKey] = None,
                 keys: Optional[List[DapCryptoKey]] = None,
                 handle: Optional[int] = None,
                 sign_type: Optional[DapSignType] = None):
        """Universal signature constructor
        
        Creation modes:
        1. DapSign(to_sign, key=key)                     # Single signature
        2. DapSign(to_sign, keys=keys)                   # Multi-signature (auto-detect)
        3. DapSign(to_sign, keys=keys, sign_type=type)   # Multi-signature (explicit)
        4. DapSign(handle=handle, sign_type=type)        # Wrap existing handle
        
        Args:
            to_sign: Binary object to sign
            key: Single key for single signature
            keys: Multiple keys for multi-signature
            handle: Existing signature handle (for wrapping)
            sign_type: Explicit signature type (optional)
            
        Raises:
            DapSignError: If signature creation/wrapping fails
            ValueError: If invalid parameter combination
        """
        # Mode 4: Wrap existing handle
        if handle is not None:
            if not handle:
                raise DapSignError("Invalid signature handle")
            if sign_type is None:
                raise ValueError("sign_type required when wrapping handle")
            self._handle = handle
            self._type = sign_type
            self._keys = keys or []
            self._metadata = DapSignMetadata.get_metadata(sign_type)
            return
        
        # Use static sign function for creation modes
        if to_sign is not None:
            created_signature = DapSign.sign(to_sign=to_sign, key=key, keys=keys, sign_type=sign_type)
            # Copy attributes from created signature
            self._handle = created_signature._handle
            self._type = created_signature._type
            self._keys = created_signature._keys
            self._metadata = created_signature._metadata
            return
        
        raise ValueError("Either 'to_sign' or 'handle' must be provided")
    
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
        """Get associated keys"""
        return self._keys.copy()
    
    @property
    def metadata(self) -> Dict:
        """Get signature metadata"""
        return self._metadata.copy()
    
    def check_capability(self, capability: str) -> bool:
        """Universal capability checker"""
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
    
    def verify(self, to_sign: Union[str, bytes],
               key: Optional[DapCryptoKey] = None,
               keys: Optional[List[DapCryptoKey]] = None) -> bool:
        """Universal signature verification
        
        Args:
            to_sign: Binary object to verify
            key: Key for single signature verification
            keys: Keys for multi-signature verification
            
        Returns:
            True if signature is valid
        """
        if isinstance(to_sign, str):
            to_sign = to_sign.encode()
        elif not isinstance(to_sign, bytes):
            raise TypeError("to_sign must be string or bytes")
        
        # Single signature verification
        if not self.supports_multi_signature():
            verify_key = key or (self._keys[0] if self._keys else None)
            if not verify_key:
                raise ValueError("Key is required for single signature verification")
            return verify_key.verify(self._handle, to_sign)
        
        # Multi-signature verification
        verify_keys = keys or self._keys
        if not verify_keys:
            raise ValueError("Keys are required for multi-signature verification")
        
        if self._type == DapSignType.COMPOSITE:
            key_handles = [k.handle for k in verify_keys]
            return _dap.py_dap_crypto_multi_sign_verify(self._handle, key_handles, to_sign)
        elif self._type == DapSignType.CHIPMUNK:
            return _dap.py_dap_crypto_aggregated_sign_verify(self._handle, to_sign)
        else:
            raise ValueError(f"Unsupported multi-signature type: {self._type}")

# Helper functions for convenience
def quick_sign(to_sign: Union[str, bytes],
               key: Optional[DapCryptoKey] = None,
               keys: Optional[List[DapCryptoKey]] = None,
               **kwargs) -> DapSign:
    """Quick signature creation - uses static sign function"""
    return DapSign.sign(to_sign=to_sign, key=key, keys=keys, **kwargs)

def quick_verify(signature: DapSign, 
                to_sign: Union[str, bytes],
                key: Optional[DapCryptoKey] = None,
                keys: Optional[List[DapCryptoKey]] = None) -> bool:
    """Quick signature verification"""
    return signature.verify(to_sign=to_sign, key=key, keys=keys)

def quick_multi_sign(to_sign: Union[str, bytes],
                    keys: List[DapCryptoKey],
                    **kwargs) -> DapSign:
    """Quick multi-signature creation - uses static sign function"""
    return DapSign.sign(to_sign=to_sign, keys=keys, **kwargs)

def quick_composite_sign(to_sign: Union[str, bytes],
                        keys: List[DapCryptoKey]) -> DapSign:
    """Quick composite signature creation - uses static sign function"""
    return DapSign.sign(to_sign=to_sign, keys=keys, sign_type=DapSignType.COMPOSITE)

def quick_aggregated_sign(to_sign: Union[str, bytes],
                         keys: List[DapCryptoKey]) -> DapSign:
    """Quick aggregated signature creation - uses static sign function"""
    return DapSign.sign(to_sign=to_sign, keys=keys, sign_type=DapSignType.CHIPMUNK)

def get_recommended_signature_types() -> List[DapSignType]:
    """Get list of recommended signature types"""
    return [t for t in DapSignType if DapSignMetadata.is_quantum_secure(t) and not DapSignMetadata.is_deprecated(t)]

def get_deprecated_signature_types() -> List[DapSignType]:
    """Get list of deprecated signature types"""
    return [t for t in DapSignType if DapSignMetadata.is_deprecated(t)]

def check_signature_compatibility(sign_type: DapSignType) -> Dict[str, bool]:
    """Check signature type capabilities"""
    return {
        'quantum_secure': DapSignMetadata.is_quantum_secure(sign_type),
        'deprecated': DapSignMetadata.is_deprecated(sign_type),
        'multi_signature': DapSignMetadata.supports_multi_signature(sign_type),
        'aggregated': DapSignMetadata.supports_aggregation(sign_type)
    } 