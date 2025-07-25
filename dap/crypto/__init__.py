"""DAP SDK Crypto Module - Unified Signature System with Clean API

This module provides a unified interface for all DAP SDK cryptographic operations
including post-quantum signatures, multi-signatures, and key management.
"""

from .keys import DapCryptoKey, DapKeyType
from .sign import (
    DapSign, 
    DapSignType, 
    DapSignMetadata, 
    DapSignError,
    get_recommended_signature_types,
    get_deprecated_signature_types,
    get_quantum_vulnerable_signature_types,
    get_legacy_deprecated_signature_types,
    check_signature_compatibility
)
from .hash import DapHashType, DapHash
from .cert import DapCert

__all__ = [
    # Key management
    'DapCryptoKey',
    'DapKeyType',
    
    # Unified signature system
    'DapSign',
    'DapSignType', 
    'DapSignMetadata',
    'DapSignError',
    
    # Hash functions
    'DapHashType',
    'DapHash',
    
    # Certificates
    'DapCert',
    
    # Utility functions
    'get_recommended_signature_types',
    'get_deprecated_signature_types',
    'get_quantum_vulnerable_signature_types',
    'get_legacy_deprecated_signature_types',
    'check_signature_compatibility'
]

__version__ = "5.1.0"
__description__ = "DAP SDK Crypto Module - Clean API with Deprecated Signatures Support"

# Configuration
DEFAULT_SIGNATURE_TYPE = DapSignType.DILITHIUM  # Quantum-secure default
MULTI_SIGNATURE_DEFAULT = DapSignType.COMPOSITE  # Default for mixed keys
