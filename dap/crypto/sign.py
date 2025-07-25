"""
✍️ DAP Crypto Signatures Module

Unified signature system for DAP SDK with single constructor.
Supports all types of signatures through smart parameter detection.
"""

from enum import Enum
from typing import Optional, Union, List, Dict
import python_dap as _dap
from .keys import DapKey, DapKeyType

class DapSignType(Enum):
    """Real DAP SDK signature types based on dap_pkey.h and dap_sign.h"""
    # Post-quantum secure (recommended)
    DILITHIUM = 1
    FALCON = 2
    SPHINCSPLUS = 3
    SHIPOVNIK = 4
    
    # Multi-signature types
    COMPOSITE = 100    # DAP composite multi-signature (SIG_TYPE_MULTI_COMBINED)
    CHIPMUNK = 101     # Aggregated signature (Chipmunk)
    
    # Deprecated post-quantum (still quantum secure but deprecated)
    BLISS = 200        # Deprecated post-quantum signature
    TESLA = 201        # Deprecated post-quantum signature
    PICNIC = 202       # Deprecated post-quantum signature
    
    # Deprecated quantum vulnerable
    ECDSA = 300        # Deprecated - quantum vulnerable

class DapSignMetadata:
    """Metadata for signature types"""
    
    _metadata = {
        DapSignType.DILITHIUM: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'DILITHIUM post-quantum signature'
        },
        DapSignType.FALCON: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'FALCON post-quantum signature'
        },
        DapSignType.SPHINCSPLUS: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'SPHINCS+ post-quantum signature'
        },
        DapSignType.SHIPOVNIK: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'SHIPOVNIK post-quantum signature'
        },
        DapSignType.COMPOSITE: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': True,
            'aggregated': False,
            'description': 'DAP composite multi-signature'
        },
        DapSignType.CHIPMUNK: {
            'quantum_secure': True,
            'deprecated': False,
            'quantum_vulnerable': False,
            'multi_signature': True,
            'aggregated': True,
            'description': 'Chipmunk aggregated signature'
        },
        DapSignType.BLISS: {
            'quantum_secure': True,
            'deprecated': True,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'BLISS post-quantum signature (deprecated)'
        },
        DapSignType.TESLA: {
            'quantum_secure': True,
            'deprecated': True,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'TESLA post-quantum signature (deprecated)'
        },
        DapSignType.PICNIC: {
            'quantum_secure': True,
            'deprecated': True,
            'quantum_vulnerable': False,
            'multi_signature': False,
            'aggregated': False,
            'description': 'PICNIC post-quantum signature (deprecated)'
        },
        DapSignType.ECDSA: {
            'quantum_secure': False,
            'deprecated': True,
            'quantum_vulnerable': True,
            'multi_signature': False,
            'aggregated': False,
            'description': 'ECDSA signature (deprecated - quantum vulnerable)'
        }
    }
    
    @classmethod
    def get_metadata(cls, sign_type: DapSignType) -> Dict:
        """Get metadata for signature type"""
        return cls._metadata.get(sign_type, {}).copy()
    
    @classmethod
    def supports_multi_signature(cls, sign_type: DapSignType) -> bool:
        """Check if signature type supports multi-signatures"""
        return cls._metadata.get(sign_type, {}).get('multi_signature', False)
    
    @classmethod
    def is_quantum_vulnerable(cls, sign_type: DapSignType) -> bool:
        """Check if signature type is quantum vulnerable"""
        return cls._metadata.get(sign_type, {}).get('quantum_vulnerable', False)
    
    @classmethod  
    def is_deprecated(cls, sign_type: DapSignType) -> bool:
        """Check if signature type is deprecated"""
        return cls._metadata.get(sign_type, {}).get('deprecated', False)

class DapSignError(Exception):
    """DAP signature operation error"""
    pass

class DapSign:
    """Unified signature class with single constructor supporting all operations"""
    
    @staticmethod
    def sign(to_sign: Union[str, bytes],
             pvt_key: Optional[DapKey] = None,
             pvt_keys: Optional[List[DapKey]] = None,
             pvt_cert: Optional['DapCert'] = None,
             sign_type: Optional[DapSignType] = None) -> "DapSign":
        """Static signing function - creates and returns signature
        
        Args:
            to_sign: Binary object to sign
            pvt_key: Single private key for single signature
            pvt_keys: Multiple private keys for multi-signature
            pvt_cert: Certificate with private key for signing
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
        
        # Certificate signing
        if pvt_cert is not None:
            if pvt_key is not None or pvt_keys is not None:
                raise ValueError("Cannot specify pvt_cert with pvt_key/pvt_keys")
            return DapSign._create_cert_signature_static(to_sign, pvt_cert, sign_type)
        
        # Single signature
        if pvt_key is not None:
            if pvt_keys is not None:
                raise ValueError("Cannot specify both 'pvt_key' and 'pvt_keys'")
            return DapSign._create_single_signature_static(to_sign, pvt_key, sign_type)
        
        # Multi-signature
        if pvt_keys is not None:
            if not pvt_keys:
                raise ValueError("Private keys list is empty")
            return DapSign._create_multi_signature_static(to_sign, pvt_keys, sign_type)
        
        raise ValueError("Either 'pvt_key', 'pvt_keys', or 'pvt_cert' must be provided for signature creation")
    
    @staticmethod
    def _create_cert_signature_static(to_sign: bytes, pvt_cert: 'DapCert', sign_type: Optional[DapSignType]) -> "DapSign":
        """Create signature using certificate (static version)"""
        # Extract private key from certificate
        pvt_key = pvt_cert.get_private_key()
        if not pvt_key:
            raise DapSignError("Certificate does not contain private key")
        
        # Use single signature creation
        return DapSign._create_single_signature_static(to_sign, pvt_key, sign_type)
    
    @staticmethod
    def _create_single_signature_static(to_sign: bytes, pvt_key: DapKey, sign_type: Optional[DapSignType]) -> "DapSign":
        """Create single signature (static version)"""
        # Determine signature type
        if sign_type is None:
            # Auto-detect from key type
            sign_type = DapSignType(pvt_key.key_type.value)
        
        # Create signature
        handle = pvt_key.sign(to_sign)
        if not handle:
            raise DapSignError("Failed to create single signature")
        
        # Extract public key from signature for storage
        pub_key = pvt_key.get_public_key()
        if not pub_key:
            raise DapSignError("Failed to extract public key from signature")
        
        # Create instance using handle wrapper mode
        return DapSign(handle=handle, sign_type=sign_type, keys=[pub_key])
    
    @staticmethod
    def _create_multi_signature_static(to_sign: bytes, pvt_keys: List[DapKey], sign_type: Optional[DapSignType]) -> "DapSign":
        """Create multi-signature (static version)"""
        # Auto-detect signature type if not specified
        if sign_type is None:
            if all(k.key_type == DapKeyType.CHIPMUNK for k in pvt_keys):
                sign_type = DapSignType.CHIPMUNK  # Aggregated
            else:
                sign_type = DapSignType.COMPOSITE  # Composite
        
        # Validate multi-signature support
        if not DapSignMetadata.supports_multi_signature(sign_type):
            raise ValueError(f"Signature type {sign_type} does not support multi-signatures")
        
        # Extract public keys for storage
        pub_keys = []
        for pvt_key in pvt_keys:
            pub_key = pvt_key.get_public_key()
            if not pub_key:
                raise DapSignError("Failed to extract public key from private key")
            pub_keys.append(pub_key)
        
        # Create based on type
        if sign_type == DapSignType.COMPOSITE:
            handle = DapSign._create_composite_static(to_sign, pvt_keys)
        elif sign_type == DapSignType.CHIPMUNK:
            handle = DapSign._create_aggregated_static(to_sign, pvt_keys)
        else:
            raise ValueError(f"Unsupported multi-signature type: {sign_type}")
        
        # Create instance using handle wrapper mode
        return DapSign(handle=handle, sign_type=sign_type, keys=pub_keys)
    
    @staticmethod
    def _create_composite_static(to_sign: bytes, pvt_keys: List[DapKey]) -> int:
        """Create composite multi-signature (static version)"""
        multi_sign_handle = _dap.py_dap_crypto_multi_sign_create()
        if not multi_sign_handle:
            raise DapSignError("Failed to create composite multi-signature")
        
        try:
            # Add individual signatures
            for pvt_key in pvt_keys:
                sig_handle = pvt_key.sign(to_sign)
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
    def _create_aggregated_static(to_sign: bytes, pvt_keys: List[DapKey]) -> int:
        """Create aggregated signature (static version)"""
        agg_sign_handle = _dap.py_dap_crypto_aggregated_sign_create()
        if not agg_sign_handle:
            raise DapSignError("Failed to create aggregated signature")
        
        try:
            # Add individual signatures with keys
            for pvt_key in pvt_keys:
                sig_handle = pvt_key.sign(to_sign)
                if not _dap.py_dap_crypto_aggregated_sign_add(agg_sign_handle, sig_handle, pvt_key.handle):
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
                 pvt_key: Optional[DapKey] = None,
                 pvt_keys: Optional[List[DapKey]] = None,
                 pvt_cert: Optional['DapCert'] = None,
                 handle: Optional[int] = None,
                 sign_type: Optional[DapSignType] = None,
                 keys: Optional[List[DapKey]] = None):
        """Universal signature constructor
        
        Creation modes:
        1. DapSign(to_sign, pvt_key=key)                     # Single signature
        2. DapSign(to_sign, pvt_keys=keys)                   # Multi-signature (auto-detect)
        3. DapSign(to_sign, pvt_cert=cert)                   # Certificate signature
        4. DapSign(handle=handle, sign_type=type, keys=keys) # Wrap existing handle
        
        Args:
            to_sign: Binary object to sign
            pvt_key: Single private key for single signature
            pvt_keys: Multiple private keys for multi-signature
            pvt_cert: Certificate with private key for signing
            handle: Existing signature handle (for wrapping)
            sign_type: Explicit signature type (optional)
            keys: Public keys (for handle wrapping mode)
            
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
            created_signature = DapSign.sign(to_sign=to_sign, pvt_key=pvt_key, pvt_keys=pvt_keys, 
                                           pvt_cert=pvt_cert, sign_type=sign_type)
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
    def keys(self) -> List[DapKey]:
        """Get public keys from signature"""
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
    
    def is_quantum_vulnerable(self) -> bool:
        """Check if this signature type is quantum vulnerable"""
        return self.check_capability('quantum_vulnerable')
    
    def supports_multi_signature(self) -> bool:
        """Check if this signature type supports multi-signatures"""
        return self.check_capability('multi_signature')
    
    def supports_aggregation(self) -> bool:
        """Check if this signature type supports aggregation"""
        return self.check_capability('aggregated')
    
    def sign_add(self, to_sign: Union[str, bytes], pvt_key: DapKey) -> None:
        """Add signature to existing multi-signature
        
        Args:
            to_sign: Binary object to sign (same as original)
            pvt_key: Private key to add signature for
            
        Raises:
            DapSignError: If adding signature fails
            ValueError: If not a multi-signature
        """
        if not self.supports_multi_signature():
            raise ValueError("Cannot add signature to non-multi-signature")
        
        if isinstance(to_sign, str):
            to_sign = to_sign.encode()
        elif not isinstance(to_sign, bytes):
            raise TypeError("to_sign must be string or bytes")
        
        # Create individual signature
        sig_handle = pvt_key.sign(to_sign)
        if not sig_handle:
            raise DapSignError("Failed to create signature for addition")
        
        # Add to multi-signature based on type
        if self._type == DapSignType.COMPOSITE:
            if not _dap.py_dap_crypto_multi_sign_add(self._handle, sig_handle):
                raise DapSignError("Failed to add signature to composite multi-signature")
        elif self._type == DapSignType.CHIPMUNK:
            if not _dap.py_dap_crypto_aggregated_sign_add(self._handle, sig_handle, pvt_key.handle):
                raise DapSignError("Failed to add signature to aggregated signature")
        else:
            raise ValueError(f"Unsupported multi-signature type: {self._type}")
        
        # Add public key to stored keys
        pub_key = pvt_key.get_public_key()
        if pub_key:
            self._keys.append(pub_key)
    
    def verify(self, to_sign: Union[str, bytes],
               pub_keys: Optional[List[DapKey]] = None) -> bool:
        """Universal signature verification
        
        Args:
            to_sign: Binary object to verify
            pub_keys: Public keys for verification (optional)
                     - For single signatures: must contain exactly one key
                     - For multi-signatures: can contain subset of keys to check
                     - If None: uses all keys stored in signature
            
        Returns:
            True if signature is valid for provided keys
        """
        if isinstance(to_sign, str):
            to_sign = to_sign.encode()
        elif not isinstance(to_sign, bytes):
            raise TypeError("to_sign must be string or bytes")
        
        # Use provided keys or stored keys
        verify_keys = pub_keys or self._keys
        if not verify_keys:
            raise ValueError("No keys available for verification")
        
        # Single signature verification
        if not self.supports_multi_signature():
            if len(verify_keys) != 1:
                raise ValueError("Single signature requires exactly one key for verification")
            return verify_keys[0].verify(self._handle, to_sign)
        
        # Multi-signature verification
        if self._type == DapSignType.COMPOSITE:
            # For composite: verify against subset of keys
            key_handles = [k.handle for k in verify_keys]
            return _dap.py_dap_crypto_multi_sign_verify(self._handle, key_handles, to_sign)
        elif self._type == DapSignType.CHIPMUNK:
            # For aggregated: verify complete signature
            return _dap.py_dap_crypto_aggregated_sign_verify(self._handle, to_sign)
        else:
            raise ValueError(f"Unsupported multi-signature type: {self._type}")

def get_recommended_signature_types() -> List[DapSignType]:
    """Get list of recommended (quantum-secure, non-deprecated) signature types"""
    recommended = []
    for sign_type in DapSignType:
        metadata = DapSignMetadata.get_metadata(sign_type)
        if metadata.get('quantum_secure', False) and not metadata.get('deprecated', False):
            recommended.append(sign_type)
    return recommended

def get_deprecated_signature_types() -> List[DapSignType]:
    """Get list of all deprecated signature types (both quantum vulnerable and legacy)"""
    deprecated = []
    for sign_type in DapSignType:
        metadata = DapSignMetadata.get_metadata(sign_type)
        if metadata.get('deprecated', False):
            deprecated.append(sign_type)
    return deprecated

def get_quantum_vulnerable_signature_types() -> List[DapSignType]:
    """Get list of quantum vulnerable signature types (subset of deprecated)"""
    quantum_vulnerable = []
    for sign_type in DapSignType:
        metadata = DapSignMetadata.get_metadata(sign_type)
        if metadata.get('quantum_vulnerable', False):
            quantum_vulnerable.append(sign_type)
    return quantum_vulnerable

def get_legacy_deprecated_signature_types() -> List[DapSignType]:
    """Get list of legacy deprecated signature types (deprecated but not quantum vulnerable)"""
    legacy_deprecated = []
    for sign_type in DapSignType:
        metadata = DapSignMetadata.get_metadata(sign_type)
        if metadata.get('deprecated', False) and not metadata.get('quantum_vulnerable', False):
            legacy_deprecated.append(sign_type)
    return legacy_deprecated

def check_signature_compatibility(sign_type1: DapSignType, sign_type2: DapSignType) -> bool:
    """Check if two signature types are compatible for multi-signature operations"""
    # For now, only same types are compatible
    return sign_type1 == sign_type2 