"""
📜 DAP Crypto Certificate Module

High-level Python API for DAP SDK certificate operations.
Provides proper Python classes wrapping C structures.
"""

from enum import Enum
from typing import Optional, Union, List, Dict
import python_dap as _dap
from .keys import DapKey, DapKeyType
from .sign import DapSign

class DapCertType(Enum):
    """Supported certificate types"""
    DILITHIUM = "dilithium"  # Post-quantum certificate (default)
    FALCON = "falcon"        # Alternative post-quantum
    PICNIC = "picnic"       # Post-quantum certificate
    BLISS = "bliss"         # Legacy certificate (deprecated)
    CHIPMUNK = "chipmunk"   # Multi-signature certificate
    
    @classmethod
    def from_key_type(cls, key_type: DapKeyType) -> "DapCertType":
        """Convert key type to certificate type"""
        return cls(key_type.value)

class DapCertError(Exception):
    """Base exception for certificate operations"""
    pass

class DapCert:
    """High-level wrapper for DAP SDK certificates"""
    
    def __init__(self, handle: int):
        """Create certificate wrapper from handle
        
        Args:
            handle: C-level certificate handle
            
        Raises:
            DapCertError: If handle is invalid
        """
        if not handle:
            raise DapCertError("Invalid certificate handle")
        self._handle = handle
        
    def __del__(self):
        """Clean up certificate when object is destroyed"""
        if hasattr(self, '_handle') and self._handle:
            _dap.py_dap_cert_delete(self._handle)
            self._handle = None
            
    @property
    def handle(self) -> int:
        """Get raw certificate handle"""
        return self._handle
        
    @classmethod
    def create(cls, name: str) -> "DapCert":
        """Create a new certificate
        
        Args:
            name: Name for the certificate
            
        Returns:
            New certificate object
            
        Raises:
            DapCertError: If certificate creation fails
        """
        handle = _dap.py_dap_cert_create(name)
        if not handle:
            raise DapCertError(f"Failed to create certificate '{name}'")
        return cls(handle)
        
    def sign(self, data: Union[str, bytes]) -> DapSign:
        """Create a signature using this certificate
        
        Args:
            data: Data to sign
            
        Returns:
            New signature object
            
        Raises:
            DapCertError: If signing fails
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        handle = _dap.py_dap_cert_sign(self._handle, data)
        if not handle:
            raise DapCertError("Failed to create signature")
        return DapSign(handle)
        
    def verify(self, signature: DapSign, data: Union[str, bytes]) -> bool:
        """Verify a signature using this certificate
        
        Args:
            signature: Signature to verify
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
            
        return _dap.py_dap_cert_verify(self._handle, signature.handle, data)
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        self.__del__()

class DapCertChain:
    """Helper class for managing certificate chains"""
    
    def __init__(self):
        self._certs: List[DapCert] = []
        
    def add_certificate(self, cert: DapCert):
        """Add certificate to chain
        
        Args:
            cert: Certificate to add
        """
        self._certs.append(cert)
        
    def verify_chain(self, data: Union[str, bytes], signature: DapSign) -> bool:
        """Verify signature using certificate chain
        
        Args:
            data: Original signed data
            signature: Signature to verify
            
        Returns:
            True if signature is valid for any certificate in chain
        """
        if not self._certs:
            return False
            
        return any(cert.verify(signature, data) for cert in self._certs)
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        for cert in self._certs:
            cert.__del__()
        self._certs.clear()

class DapCertStore:
    """Helper class for storing and managing certificates"""
    
    def __init__(self):
        self._certs: Dict[str, DapCert] = {}
        
    def add_certificate(self, name: str, cert: DapCert):
        """Add certificate to store
        
        Args:
            name: Name to store certificate under
            cert: Certificate to store
            
        Raises:
            KeyError: If certificate with name already exists
        """
        if name in self._certs:
            raise KeyError(f"Certificate '{name}' already exists")
        self._certs[name] = cert
        
    def get_certificate(self, name: str) -> DapCert:
        """Get certificate from store
        
        Args:
            name: Name of certificate to get
            
        Returns:
            Stored certificate
            
        Raises:
            KeyError: If certificate does not exist
        """
        if name not in self._certs:
            raise KeyError(f"Certificate '{name}' does not exist")
        return self._certs[name]
        
    def delete_certificate(self, name: str):
        """Delete certificate from store
        
        Args:
            name: Name of certificate to delete
            
        Raises:
            KeyError: If certificate does not exist
        """
        if name not in self._certs:
            raise KeyError(f"Certificate '{name}' does not exist")
        del self._certs[name]
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        for cert in list(self._certs.values()):
            cert.__del__()
        self._certs.clear()

def create_self_signed_cert(name: str, key: DapKey) -> DapCert:
    """Create a self-signed certificate
    
    Args:
        name: Name for the certificate
        key: Key to sign certificate with
        
    Returns:
        New self-signed certificate
    """
    cert = DapCert.create(name)
    # TODO: Add self-signing when DAP SDK adds support
    return cert

def load_cert_from_file(path: str) -> DapCert:
    """Load certificate from file
    
    Args:
        path: Path to certificate file
        
    Returns:
        Loaded certificate
        
    Raises:
        DapCertError: If loading fails
    """
    # TODO: Add file loading when DAP SDK adds support
    raise NotImplementedError("Certificate file loading not yet supported")

def find_certificate(store: DapCertStore, criteria: Dict) -> Optional[DapCert]:
    """Find certificate matching criteria
    
    Args:
        store: Certificate store to search
        criteria: Search criteria
        
    Returns:
        Matching certificate or None
    """
    # TODO: Add certificate search when DAP SDK adds support
    return None 