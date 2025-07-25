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
    DapSignType,
    DapSignMetadata,
    quick_sign,
    quick_verify,
    quick_multi_sign,
    get_recommended_signature_types,
    get_deprecated_signature_types,
    check_signature_compatibility
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
    DapHashType,
    quick_hash_fast
)

__all__ = [
    # Keys
    'DapCryptoKey',
    'DapKeyType',
    'DapKeyError',
    'DapKeyManager',
    
    # Digital Signatures (Unified System)
    'DapSign',
    'DapSignError',
    'DapSignType',
    'DapSignMetadata',
    'quick_sign',
    'quick_verify',
    'quick_multi_sign',
    'get_recommended_signature_types',
    'get_deprecated_signature_types',
    'check_signature_compatibility',
    
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
    'DapHashType',
    'quick_hash_fast',
]

# Version info
__version__ = "3.0.0"
__author__ = "Demlabs"
__description__ = "DAP SDK Crypto Module - Unified Signature System"
