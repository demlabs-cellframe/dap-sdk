"""
🔐 DAP Crypto Module

Unified cryptographic operations for DAP SDK with single constructor pattern.
Only real DAP SDK signature types are supported.
"""

from .keys import DapCryptoKey, DapKeyType, DapKeyError
from .sign import (
    DapSign, DapSignType, DapSignMetadata, DapSignError,
    quick_sign, quick_verify, quick_multi_sign,
    quick_composite_sign, quick_aggregated_sign,
    get_recommended_signature_types, get_deprecated_signature_types,
    check_signature_compatibility
)

__all__ = [
    # Key management
    "DapCryptoKey",
    "DapKeyType", 
    "DapKeyError",
    
    # Unified signature system with single constructor
    "DapSign",
    "DapSignType",
    "DapSignMetadata", 
    "DapSignError",
    
    # Quick functions (all use single constructor internally)
    "quick_sign",
    "quick_verify", 
    "quick_multi_sign",
    "quick_composite_sign",
    "quick_aggregated_sign",
    
    # Utility functions
    "get_recommended_signature_types",
    "get_deprecated_signature_types",
    "check_signature_compatibility"
]

__version__ = "4.0.0"
__description__ = "DAP SDK Crypto Module - Single Constructor with Real DAP Types Only"

# Configuration
DEFAULT_SIGNATURE_TYPE = DapSignType.DILITHIUM  # Quantum-secure default
MULTI_SIGNATURE_DEFAULT = DapSignType.COMPOSITE  # Default for mixed keys
