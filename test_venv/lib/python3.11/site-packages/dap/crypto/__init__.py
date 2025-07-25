"""
🔐 DAP Crypto Module

Comprehensive cryptographic operations for DAP SDK.
All classes now properly wrap corresponding C structures.
"""

from .keys import (
    DapCryptoKey,
    DapKeyType,
    DapKeyError,
    DapKeyManager
)

from .sign import (
    DapSign,
    DapSignError,
    DapHashType,
    DapSignType,
    DapSignatureAggregator,
    DapBatchVerifier,
    DapAggregatedSignature,
    DapMultiSignature,
    quick_sign,
    quick_verify
)

from .cert import (
    DapCert,
    DapCertError,
    DapCertType,
    DapCertChain,
    DapCertStore,
    create_self_signed_cert,
    load_cert_from_file,
    find_certificate
)

from .hash import (
    DapHash,
    DapHashError,
    DapHashType as DapHashingType,
    quick_hash_fast
)

from .enc import (
    DapEnc,
    DapEncError,
    DapEncType,
    quick_encrypt,
    quick_decrypt
)

__all__ = [
    # Keys
    'DapCryptoKey',
    'DapKeyType',
    'DapKeyError',
    'DapKeyManager',
    
    # Digital Signatures
    'DapSign',
    'DapSignError',
    'DapHashType',
    'DapSignType',
    'DapSignatureAggregator',
    'DapBatchVerifier',
    'DapAggregatedSignature',
    'DapMultiSignature',
    'quick_sign',
    'quick_verify',
    
    # Certificates
    'DapCert',
    'DapCertError',
    'DapCertType',
    'DapCertChain',
    'DapCertStore',
    'create_self_signed_cert',
    'load_cert_from_file',
    'find_certificate',
    
    # Hash functions
    'DapHash',
    'DapHashError',
    'DapHashingType',  # Alias for DapHashType
    'quick_hash_fast',
    
    # Encryption
    'DapEnc',
    'DapEncError', 
    'DapEncType',
    'quick_encrypt',
    'quick_decrypt',
]

# Version info
__version__ = "3.0.0"
__author__ = "Demlabs"
__description__ = "DAP SDK Crypto Module - Proper C structure wrapping"
