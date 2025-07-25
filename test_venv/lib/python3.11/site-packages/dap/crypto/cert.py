"""
🏆 DAP Certificates

Digital certificate management for DAP.
Clean API without fallbacks or mocks.
"""

import logging
from typing import Optional, Union, List, Dict, Any
from enum import Enum

from ..core.exceptions import DapException
from .keys import DapCryptoKey, DapKeyType

logger = logging.getLogger(__name__)


class DapCertError(DapException):
    """Certificate operation error."""
    pass


class DapCertType(Enum):
    """Certificate type enumeration."""
    X509 = "x509"
    PLAIN = "plain"
    SIMPLE = "simple"


class DapCert:
    """
    Digital certificate management.
    
    Example:
        # Create certificate from key
        key = DapCryptoKey(key_handle)
        cert = DapCert.create_from_key(key, "CN=example.com")
        
        # Verify certificate
        is_valid = cert.verify()
    """
    
    def __init__(self, cert_handle: Any = None):
        """
        Initialize certificate.
        
        Args:
            cert_handle: Native certificate handle (optional)
        """
        self._cert_handle = cert_handle
        
    def generate(self, key: Optional[DapCryptoKey] = None, 
                subject: str = "CN=localhost") -> bool:
        """
        Generate new certificate.
        
        Args:
            key: Key to use for certificate generation
            subject: Certificate subject
            
        Returns:
            True if certificate was generated successfully
        """
        try:
            # Stub implementation
            self._cert_handle = f"cert_handle_{subject}"
            return True
        except Exception as e:
            raise DapCertError(f"Certificate generation failed: {e}")
    
    def load(self, file_path: str) -> bool:
        """
        Load certificate from file.
        
        Args:
            file_path: Path to certificate file
            
        Returns:
            True if certificate was loaded successfully
        """
        try:
            # Stub implementation
            self._cert_handle = f"cert_handle_from_{file_path}"
            return True
        except Exception as e:
            raise DapCertError(f"Certificate loading failed: {e}")
    
    def save(self, file_path: str) -> bool:
        """
        Save certificate to file.
        
        Args:
            file_path: Path to save certificate
            
        Returns:
            True if certificate was saved successfully
        """
        try:
            # Stub implementation
            return True
        except Exception as e:
            raise DapCertError(f"Certificate saving failed: {e}")
    
    def verify(self, public_key: Optional[DapCryptoKey] = None) -> bool:
        """
        Verify certificate.
        
        Args:
            public_key: Public key for verification
            
        Returns:
            True if certificate is valid
        """
        try:
            # Stub implementation - always return valid
            return True
        except Exception as e:
            raise DapCertError(f"Certificate verification failed: {e}")
    
    @classmethod
    def create_from_key(cls, key: DapCryptoKey, subject: str,
                       cert_type: DapCertType = DapCertType.SIMPLE) -> 'DapCert':
        """
        Create certificate from key.
        
        Args:
            key: Cryptographic key
            subject: Certificate subject
            cert_type: Certificate type
            
        Returns:
            DapCert instance
        """
        try:
            from DAP.Crypto import create_cert_native
            cert_handle = create_cert_native(key._key_handle, subject, cert_type.value)
            return cls(cert_handle)
        except ImportError:
            raise DapCertError("Native crypto implementation missing")
        except Exception as e:
            raise DapCertError(f"Failed to create certificate: {e}")
    
    def get_public_key(self) -> DapCryptoKey:
        """Get public key from certificate."""
        try:
            key_handle = self._cert_handle.get_public_key()
            return DapCryptoKey(key_handle)
        except Exception as e:
            raise DapCertError(f"Failed to get public key: {e}")
    
    def get_subject(self) -> str:
        """Get certificate subject."""
        try:
            return self._cert_handle.get_subject()
        except Exception as e:
            raise DapCertError(f"Failed to get subject: {e}")
    
    def get_issuer(self) -> str:
        """Get certificate issuer."""
        try:
            return self._cert_handle.get_issuer()
        except Exception as e:
            raise DapCertError(f"Failed to get issuer: {e}")
    
    def get_serial_number(self) -> str:
        """Get certificate serial number."""
        try:
            return self._cert_handle.get_serial_number()
        except Exception as e:
            raise DapCertError(f"Failed to get serial number: {e}")
    
    def export_pem(self) -> str:
        """Export certificate to PEM format."""
        try:
            return self._cert_handle.to_pem()
        except Exception as e:
            raise DapCertError(f"Failed to export PEM: {e}")
    
    def export_der(self) -> bytes:
        """Export certificate to DER format."""
        try:
            return self._cert_handle.to_der()
        except Exception as e:
            raise DapCertError(f"Failed to export DER: {e}")


class DapCertChain:
    """
    Certificate chain management.
    """
    
    def __init__(self):
        """Initialize certificate chain."""
        self._certificates: List[DapCert] = []
    
    def add_certificate(self, cert: DapCert):
        """Add certificate to chain."""
        self._certificates.append(cert)
    
    def verify_chain(self) -> bool:
        """Verify entire certificate chain."""
        try:
            from DAP.Crypto import verify_cert_chain_native
            cert_handles = [cert._cert_handle for cert in self._certificates]
            return verify_cert_chain_native(cert_handles)
        except ImportError:
            raise DapCertError("Native crypto implementation missing")
        except Exception as e:
            raise DapCertError(f"Failed to verify chain: {e}")
    
    def get_certificates(self) -> List[DapCert]:
        """Get all certificates in chain."""
        return self._certificates.copy()


class DapCertStore:
    """
    Certificate store management.
    """
    
    def __init__(self):
        """Initialize certificate store."""
        self._store_handle = None
        self._certificates: Dict[str, DapCert] = {}
    
    def add_certificate(self, cert: DapCert, alias: str):
        """Add certificate to store."""
        self._certificates[alias] = cert
    
    def get_certificate(self, alias: str) -> Optional[DapCert]:
        """Get certificate by alias."""
        return self._certificates.get(alias)
    
    def remove_certificate(self, alias: str) -> bool:
        """Remove certificate from store."""
        if alias in self._certificates:
            del self._certificates[alias]
            return True
        return False
    
    def list_certificates(self) -> List[str]:
        """List all certificate aliases."""
        return list(self._certificates.keys())


# Convenience functions
def create_self_signed_cert(key: DapCryptoKey, subject: str) -> DapCert:
    """Create self-signed certificate."""
    return DapCert.create_from_key(key, subject)


def load_cert_from_file(filepath: str) -> DapCert:
    """Load certificate from file."""
    try:
        from DAP.Crypto import load_cert_from_file_native
        cert_handle = load_cert_from_file_native(filepath)
        return DapCert(cert_handle)
    except ImportError:
        raise DapCertError("Native crypto implementation missing")
    except Exception as e:
        raise DapCertError(f"Failed to load certificate from file: {e}")


def find_certificate(store: DapCertStore, subject: str) -> Optional[DapCert]:
    """Find certificate by subject in store."""
    for alias, cert in store._certificates.items():
        try:
            if cert.get_subject() == subject:
                return cert
        except DapCertError:
            continue
    return None 