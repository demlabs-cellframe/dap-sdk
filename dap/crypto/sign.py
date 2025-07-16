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
            raise DapSignError("Native crypto implementation not available")
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
            raise DapSignError("Native crypto implementation not available")
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
            raise DapSignError("Native crypto implementation not available")
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
            raise DapSignError("Native crypto implementation not available")
        except Exception as e:
            raise DapSignError(f"Failed to verify batch: {e}")


# Convenience functions
def quick_sign(key: DapCryptoKey, data: Union[bytes, str]) -> DapSign:
    """Quick signature creation."""
    return DapSign.create_from_key_and_data_simple(key, data)


def quick_verify(sign: DapSign, data: Union[bytes, str], 
                public_key: Optional[DapCryptoKey] = None) -> bool:
    """Quick signature verification."""
    return sign.verify(data, public_key) 