"""
Key management classes for DAP crypto operations
"""
from typing import Optional, Union
import logging

logger = logging.getLogger(__name__)


class DapKeyManager:
    """
    Key manager for DAP crypto operations
    This is a fallback implementation when native module is not available
    """
    
    def __init__(self, key_type: str = "sig_dilithium"):
        """
        Initialize key manager
        
        Args:
            key_type: Type of cryptographic key (default: sig_dilithium)
        """
        self.key_type = key_type
        self._keys = {}
        logger.warning("Using fallback DapKeyManager - native crypto not available")
    
    def generate_key(self, key_name: str) -> bool:
        """
        Generate a new cryptographic key
        
        Args:
            key_name: Name for the key
            
        Returns:
            True if key was generated successfully
        """
        try:
            # Fallback key generation (mock)
            self._keys[key_name] = {
                "type": self.key_type,
                "public_key": f"mock_public_key_{key_name}",
                "private_key": f"mock_private_key_{key_name}",
                "created_at": "2024-01-01T00:00:00Z"
            }
            logger.info(f"Generated fallback key: {key_name}")
            return True
        except Exception as e:
            logger.error(f"Failed to generate key {key_name}: {e}")
            return False
    
    def get_key(self, key_name: str) -> Optional[dict]:
        """
        Get key by name
        
        Args:
            key_name: Name of the key
            
        Returns:
            Key information or None if not found
        """
        return self._keys.get(key_name)
    
    def delete_key(self, key_name: str) -> bool:
        """
        Delete key by name
        
        Args:
            key_name: Name of the key to delete
            
        Returns:
            True if key was deleted successfully
        """
        if key_name in self._keys:
            del self._keys[key_name]
            logger.info(f"Deleted key: {key_name}")
            return True
        return False
    
    def list_keys(self) -> list:
        """
        List all available keys
        
        Returns:
            List of key names
        """
        return list(self._keys.keys())
    
    def sign_data(self, key_name: str, data: Union[str, bytes]) -> Optional[str]:
        """
        Sign data with specified key
        
        Args:
            key_name: Name of the key to use for signing
            data: Data to sign
            
        Returns:
            Signature string or None if signing failed
        """
        if key_name not in self._keys:
            logger.error(f"Key not found: {key_name}")
            return None
        
        try:
            # Mock signature generation
            data_str = data if isinstance(data, str) else data.decode('utf-8')
            signature = f"mock_signature_{key_name}_{hash(data_str)}"
            logger.info(f"Signed data with key: {key_name}")
            return signature
        except Exception as e:
            logger.error(f"Failed to sign data with key {key_name}: {e}")
            return None
    
    def verify_signature(self, key_name: str, data: Union[str, bytes], signature: str) -> bool:
        """
        Verify signature with specified key
        
        Args:
            key_name: Name of the key to use for verification
            data: Original data
            signature: Signature to verify
            
        Returns:
            True if signature is valid
        """
        if key_name not in self._keys:
            logger.error(f"Key not found: {key_name}")
            return False
        
        try:
            # Mock signature verification
            data_str = data if isinstance(data, str) else data.decode('utf-8')
            expected_signature = f"mock_signature_{key_name}_{hash(data_str)}"
            is_valid = signature == expected_signature
            logger.info(f"Signature verification for key {key_name}: {is_valid}")
            return is_valid
        except Exception as e:
            logger.error(f"Failed to verify signature with key {key_name}: {e}")
            return False
    
    def export_key(self, key_name: str, export_private: bool = False) -> Optional[str]:
        """
        Export key to string format
        
        Args:
            key_name: Name of the key to export
            export_private: Whether to include private key
            
        Returns:
            Key data as string or None if export failed
        """
        key_info = self.get_key(key_name)
        if not key_info:
            return None
        
        export_data = {
            "name": key_name,
            "type": key_info["type"],
            "public_key": key_info["public_key"],
            "created_at": key_info["created_at"]
        }
        
        if export_private:
            export_data["private_key"] = key_info["private_key"]
        
        return str(export_data)
    
    def import_key(self, key_name: str, key_data: str) -> bool:
        """
        Import key from string format
        
        Args:
            key_name: Name for the imported key
            key_data: Key data as string
            
        Returns:
            True if key was imported successfully
        """
        try:
            # Mock key import
            self._keys[key_name] = {
                "type": self.key_type,
                "public_key": f"imported_public_key_{key_name}",
                "private_key": f"imported_private_key_{key_name}",
                "created_at": "2024-01-01T00:00:00Z",
                "imported": True
            }
            logger.info(f"Imported key: {key_name}")
            return True
        except Exception as e:
            logger.error(f"Failed to import key {key_name}: {e}")
            return False


class DapCryptoKey:
    """
    Individual crypto key class
    """
    
    def __init__(self, key_name: str, key_type: str = "sig_dilithium"):
        """
        Initialize crypto key
        
        Args:
            key_name: Name of the key
            key_type: Type of cryptographic key
        """
        self.name = key_name
        self.key_type = key_type
        self.is_valid = False
        logger.warning("Using fallback DapCryptoKey - native crypto not available")
    
    def generate(self) -> bool:
        """Generate the key"""
        try:
            self.is_valid = True
            logger.info(f"Generated fallback crypto key: {self.name}")
            return True
        except Exception as e:
            logger.error(f"Failed to generate crypto key {self.name}: {e}")
            return False
    
    def get_public_key(self) -> Optional[str]:
        """Get public key"""
        if not self.is_valid:
            return None
        return f"mock_public_key_{self.name}"
    
    def get_private_key(self) -> Optional[str]:
        """Get private key"""
        if not self.is_valid:
            return None
        return f"mock_private_key_{self.name}"


# Compatibility aliases
DapKey = DapCryptoKey
KeyManager = DapKeyManager 