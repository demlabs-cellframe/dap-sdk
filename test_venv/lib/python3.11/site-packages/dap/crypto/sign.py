"""
🔏 DAP Digital Signatures

Digital signature operations for DAP.
Clean API without fallbacks or mocks.
"""

import logging
from typing import Union, Optional, List, Any
from enum import Enum

from ..core.exceptions import DapException
from .keys import DapCryptoKey

logger = logging.getLogger(__name__)


class DapSignError(DapException):
    """Digital signature operation error."""
    pass


class DapHashType(Enum):
    """Hash algorithm types."""
    SHA256 = "sha256"
    SHA3_256 = "sha3_256"
    BLAKE2B = "blake2b"
    DEFAULT = "sha256"


class DapSignType(Enum):
    """Digital signature algorithm types."""
    # ⚠️  DEPRECATED: Quantum-vulnerable algorithms
    RSA = "rsa"                    # DEPRECATED: Quantum-vulnerable
    ECDSA = "ecdsa"               # DEPRECATED: Quantum-vulnerable  
    ED25519 = "ed25519"           # DEPRECATED: Quantum-vulnerable
    
    # ✅ RECOMMENDED: Post-quantum secure algorithms
    DILITHIUM = "dilithium"       # Recommended default (lattice-based)
    FALCON = "falcon"             # NIST PQC finalist (lattice-based)
    SPHINCS = "sphincs"           # NIST PQC finalist (hash-based)
    CHIPMUNK = "chipmunk"         # Multi-signature lattice-based
    
    # Legacy post-quantum (older implementations)
    BLISS = "bliss"               # Legacy lattice-based
    TESLA = "tesla"               # Legacy lattice-based
    PICNIC = "picnic"             # Legacy zero-knowledge
    SHIPOVNIK = "shipovnik"       # Russian standard
    
    # Multi-signature schemes
    MULTI_ECDSA_DILITHIUM = "multi_ecdsa_dilithium"  # Hybrid
    MULTI_CHAINED = "multi_chained"                  # Composite
    MULTI_COMBINED = "multi_combined"                # Combined keys
    
    DEFAULT = "dilithium"         # Post-quantum secure default


class DapSign:
    """
    Digital signature management.
    
    Example:
        # Create signature from key and data
        key = DapCryptoKey(key_handle)
        sign = DapSign.create_from_key_and_data(key, b"data to sign")
        
        # Verify signature
        is_valid = sign.verify(b"data to sign", key)
    """
    
    def __init__(self, sign_handle: Any):
        """
        Initialize signature instance.
        
        Args:
            sign_handle: Native signature handle
        """
        self._sign_handle = sign_handle
    
    @property
    def handle(self) -> Any:
        """Get signature handle for compatibility with tests."""
        return self._sign_handle
        
    @classmethod
    def create_from_key_and_data(cls, key: DapCryptoKey, data: Union[bytes, str], 
                                hash_type: DapHashType = DapHashType.DEFAULT) -> 'DapSign':
        """
        Create signature from key and data.
        
        Args:
            key: DapCryptoKey instance for signing
            data: Data to sign
            hash_type: Hash algorithm to use
            
        Returns:
            DapSign instance
        """
        try:
            from DAP.Crypto import sign_data_native
            
            data_bytes = data.encode() if isinstance(data, str) else data
            sign_handle = sign_data_native(key._key_handle, data_bytes, hash_type.value)
            
            return cls(sign_handle)
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to create signature: {e}")
    
    @classmethod
    def create_from_key_and_data_simple(cls, key: DapCryptoKey, data: Union[bytes, str]) -> 'DapSign':
        """
        Create signature with default hash type.
        
        Args:
            key: DapCryptoKey instance for signing
            data: Data to sign
            
        Returns:
            DapSign instance
        """
        return cls.create_from_key_and_data(key, data, DapHashType.DEFAULT)
    
    def verify(self, data: Union[bytes, str], pkey: Optional[DapCryptoKey] = None) -> bool:
        """
        Verify signature against data.
        
        Args:
            data: Original data
            pkey: Public key for verification
            
        Returns:
            True if signature is valid
        """
        try:
            from DAP.Crypto import verify_signature_native
            
            data_bytes = data.encode() if isinstance(data, str) else data
            key_handle = pkey._key_handle if pkey else None
            
            return verify_signature_native(self._sign_handle, data_bytes, key_handle)
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to verify signature: {e}")
    
    def get_signature_bytes(self) -> bytes:
        """Get signature as bytes."""
        try:
            return self._sign_handle.to_bytes()
        except Exception as e:
            raise DapSignError(f"Failed to get signature bytes: {e}")
    
    def get_size(self) -> int:
        """Get signature size in bytes."""
        try:
            return len(self.get_signature_bytes())
        except Exception as e:
            raise DapSignError(f"Failed to get signature size: {e}")


class DapSignatureAggregator:
    """
    Signature aggregation for multiple signatures.
    """
    
    def __init__(self):
        """Initialize signature aggregator."""
        self._signatures: List[DapSign] = []
    
    def add_signature(self, sign: DapSign) -> bool:
        """
        Add signature to aggregator.
        
        Args:
            sign: Signature to add
            
        Returns:
            True if added successfully
        """
        try:
            self._signatures.append(sign)
            return True
        except Exception as e:
            raise DapSignError(f"Failed to add signature: {e}")
    
    def aggregate(self) -> DapSign:
        """
        Aggregate all signatures into one.
        
        Returns:
            Aggregated signature
        """
        try:
            from DAP.Crypto import aggregate_signatures_native
            
            sign_handles = [sig._sign_handle for sig in self._signatures]
            aggregated_handle = aggregate_signatures_native(sign_handles)
            
            return DapSign(aggregated_handle)
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to aggregate signatures: {e}")


class DapBatchVerifier:
    """
    Batch signature verification for efficiency.
    """
    
    def __init__(self):
        """Initialize batch verifier."""
        self._verifications: List[tuple] = []
    
    def add_verification(self, sign: DapSign, data: Union[bytes, str], key: DapCryptoKey) -> bool:
        """
        Add verification task.
        
        Args:
            sign: Signature to verify
            data: Original data
            key: Public key
            
        Returns:
            True if added successfully
        """
        try:
            data_bytes = data.encode() if isinstance(data, str) else data
            self._verifications.append((sign, data_bytes, key))
            return True
        except Exception as e:
            raise DapSignError(f"Failed to add verification: {e}")
    
    def verify_all(self) -> bool:
        """
        Verify all signatures in batch.
        
        Returns:
            True if all signatures are valid
        """
        try:
            from DAP.Crypto import batch_verify_native
            
            verification_data = [
                (sig._sign_handle, data, key._key_handle)
                for sig, data, key in self._verifications
            ]
            
            return batch_verify_native(verification_data)
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to verify batch: {e}")


class DapAggregatedSignature:
    """
    Aggregated signature for multiple signers.
    Based on DAP SDK aggregated signature API.
    """
    
    def __init__(self, aggregated_handle: Any):
        """Initialize aggregated signature."""
        self._aggregated_handle = aggregated_handle
        self._signature_type = None
    
    @classmethod
    def aggregate_signatures(cls, signatures: List[DapSign], 
                           aggregation_type: str = "tree_based") -> 'DapAggregatedSignature':
        """
        Aggregate multiple signatures into one.
        
        Args:
            signatures: List of signatures to aggregate
            aggregation_type: Type of aggregation ("tree_based", "linear", etc.)
            
        Returns:
            DapAggregatedSignature instance
        """
        try:
            from DAP.Crypto import aggregate_signatures_native
            
            if not signatures:
                raise DapSignError("No signatures to aggregate")
            
            # Validate all signatures are compatible
            sig_type = signatures[0]._sign_handle.type if hasattr(signatures[0]._sign_handle, 'type') else None
            for sig in signatures[1:]:
                if hasattr(sig._sign_handle, 'type') and sig._sign_handle.type != sig_type:
                    raise DapSignError("All signatures must be the same type for aggregation")
            
            sign_handles = [sig._sign_handle for sig in signatures]
            aggregated_handle = aggregate_signatures_native(sign_handles, aggregation_type)
            
            result = cls(aggregated_handle)
            result._signature_type = sig_type
            return result
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to aggregate signatures: {e}")
    
    def verify_aggregated(self, messages: List[Union[bytes, str]], 
                         public_keys: List[DapCryptoKey]) -> bool:
        """
        Verify aggregated signature against multiple messages and keys.
        
        Args:
            messages: List of original messages
            public_keys: List of public keys for verification
            
        Returns:
            True if aggregated signature is valid
        """
        try:
            from DAP.Crypto import verify_aggregated_native
            
            if len(messages) != len(public_keys):
                raise DapSignError("Number of messages must match number of public keys")
            
            message_bytes = [
                msg.encode() if isinstance(msg, str) else msg 
                for msg in messages
            ]
            key_handles = [key._key_handle for key in public_keys]
            
            return verify_aggregated_native(
                self._aggregated_handle, 
                message_bytes, 
                key_handles
            )
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to verify aggregated signature: {e}")
    
    @property
    def signature_type(self):
        """Get signature type of aggregated signature."""
        return self._signature_type
    
    def get_aggregated_bytes(self) -> bytes:
        """Get aggregated signature as bytes."""
        try:
            return self._aggregated_handle.to_bytes()
        except Exception as e:
            raise DapSignError(f"Failed to get aggregated signature bytes: {e}")


class DapMultiSignature:
    """
    Multi-signature operations for multiple algorithms.
    Supports chained and combined multi-signature schemes.
    """
    
    def __init__(self, multi_type: str = "chained"):
        """
        Initialize multi-signature.
        
        Args:
            multi_type: Type of multi-signature ("chained", "combined")
        """
        self._multi_type = multi_type
        self._signatures: List[DapSign] = []
    
    def add_signature(self, signature: DapSign) -> bool:
        """Add signature to multi-signature."""
        try:
            self._signatures.append(signature)
            return True
        except Exception as e:
            raise DapSignError(f"Failed to add signature: {e}")
    
    def verify_multi(self, data: Union[bytes, str], 
                    public_keys: List[DapCryptoKey]) -> bool:
        """
        Verify multi-signature.
        
        Args:
            data: Original data
            public_keys: List of public keys
            
        Returns:
            True if multi-signature is valid
        """
        try:
            from DAP.Crypto import verify_multi_signature_native
            
            data_bytes = data.encode() if isinstance(data, str) else data
            sign_handles = [sig._sign_handle for sig in self._signatures]
            key_handles = [key._key_handle for key in public_keys]
            
            return verify_multi_signature_native(
                sign_handles, 
                data_bytes, 
                key_handles, 
                self._multi_type
            )
            
        except ImportError:
            raise DapSignError("Native crypto implementation missing")
        except Exception as e:
            raise DapSignError(f"Failed to verify multi-signature: {e}")


# Convenience functions
def quick_sign(key: DapCryptoKey, data: Union[bytes, str]) -> DapSign:
    """Quick signature creation."""
    return DapSign.create_from_key_and_data_simple(key, data)


def quick_verify(sign: DapSign, data: Union[bytes, str], 
                public_key: Optional[DapCryptoKey] = None) -> bool:
    """Quick signature verification."""
    return sign.verify(data, public_key) 


__all__ = [
    'DapSign',
    'DapSignType', 
    'DapSignError',
    'DapHashType',
    'DapSignatureAggregator',    # Legacy
    'DapBatchVerifier',
    'DapAggregatedSignature',    # New aggregated signature API
    'DapMultiSignature',         # New multi-signature API
    'quick_sign',
    'quick_verify'
] 