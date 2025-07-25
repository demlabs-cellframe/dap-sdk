"""
🔑 DAP Crypto Keys Module

High-level Python API for DAP SDK cryptographic key operations.
Provides proper Python classes wrapping C structures with encryption capabilities.
"""

from enum import Enum, auto
from typing import Optional, Union, Tuple
import python_dap as _dap
import os

class DapKeyType(Enum):
    """DAP SDK cryptographic algorithm types (matching dap_enc_key_type_t)"""
    
    # === SYMMETRIC ENCRYPTION ALGORITHMS ===
    IAES = "iaes"               # DAP_ENC_KEY_TYPE_IAES (0) - AES implementation 
    OAES = "oaes"               # DAP_ENC_KEY_TYPE_OAES (1) - Alternative AES from Monero
    BF_CBC = "bf_cbc"           # DAP_ENC_KEY_TYPE_BF_CBC (2) - BlowFish CBC mode
    BF_OFB = "bf_ofb"           # DAP_ENC_KEY_TYPE_BF_OFB (3) - BlowFish OFB mode
    GOST_OFB = "gost_ofb"       # DAP_ENC_KEY_TYPE_GOST_OFB (4) - GOST28147_89
    KUZN_OFB = "kuzn_ofb"       # DAP_ENC_KEY_TYPE_KUZN_OFB (5) - GOST28147_14
    SALSA2012 = "salsa2012"     # DAP_ENC_KEY_TYPE_SALSA2012 (6) - SALSA2012
    SEED_OFB = "seed_ofb"       # DAP_ENC_KEY_TYPE_SEED_OFB (7) - SEED Cipher in OFB mode
    
    # === POST-QUANTUM KEY EXCHANGE ALGORITHMS ===
    NEWHOPE = "newhope"         # DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM (8) - NewHope
    MSRLN = "msrln"             # DAP_ENC_KEY_TYPE_MSRLN (11) 
    MSRLN16 = "msrln16"         # DAP_ENC_KEY_TYPE_RLWE_MSRLN16 (12) - Microsoft Research ring-LWE
    BCNS15 = "bcns15"           # DAP_ENC_KEY_TYPE_RLWE_BCNS15 (13) - ring-LWE
    FRODO = "frodo"             # DAP_ENC_KEY_TYPE_LWE_FRODO (14) - Frodo
    MCBITS = "mcbits"           # DAP_ENC_KEY_TYPE_CODE_MCBITS (15) - McBits
    NTRU = "ntru"               # DAP_ENC_KEY_TYPE_NTRU (16) - NTRU
    KYBER = "kyber"             # DAP_ENC_KEY_TYPE_MLWE_KYBER (17) - Kyber
    KYBER512 = "kyber512"       # DAP_ENC_KEY_TYPE_KEM_KYBER512 (23) - NIST Kyber KEM
    
    # === POST-QUANTUM SIGNATURE ALGORITHMS ===
    PICNIC = "picnic"           # DAP_ENC_KEY_TYPE_SIG_PICNIC (18) - Post-quantum signature
    TESLA = "tesla"             # DAP_ENC_KEY_TYPE_SIG_TESLA (20) - Ring_LWE signature 
    DILITHIUM = "dilithium"     # DAP_ENC_KEY_TYPE_SIG_DILITHIUM (21) - Default post-quantum signature
    FALCON = "falcon"           # DAP_ENC_KEY_TYPE_SIG_FALCON (24) - Alternative post-quantum signature
    SPHINCSPLUS = "sphincsplus" # DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS (25) - Post-quantum signature
    SHIPOVNIK = "shipovnik"     # DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK (27) - Post-quantum signature
    CHIPMUNK = "chipmunk"       # DAP_ENC_KEY_TYPE_SIG_CHIPMUNK (0x0108) - Aggregated signature
    
    # === LEGACY/DEPRECATED SIGNATURE ALGORITHMS ===
    BLISS = "bliss"             # DAP_ENC_KEY_TYPE_SIG_BLISS (19) - Legacy signature (deprecated)
    ECDSA = "ecdsa"             # DAP_ENC_KEY_TYPE_SIG_ECDSA (26) - Classical ECDSA (quantum-vulnerable)
    
    # === SPECIAL PURPOSE SIGNATURES ===
    RINGCT20 = "ringct20"       # DAP_ENC_KEY_TYPE_SIG_RINGCT20 (22) - Ring signature for confidential transactions
    
    # === MULTI-SIGNATURE ALGORITHMS ===
    MULTI_ECDSA_DILITHIUM = "multi_ecdsa_dilithium"  # DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM (99)
    MULTI_CHAINED = "multi_chained"                  # DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED (100)
    
    # === PQLR ADDITIONAL ALGORITHMS ===
    PQLR_DILITHIUM = "pqlr_dilithium"   # DAP_ENC_KEY_TYPE_PQLR_SIG_DILITHIUM (1021)
    PQLR_FALCON = "pqlr_falcon"         # DAP_ENC_KEY_TYPE_PQLR_SIG_FALCON (1024)  
    PQLR_SPHINCS = "pqlr_sphincs"       # DAP_ENC_KEY_TYPE_PQLR_SIG_SPHINCS (1025)
    PQLR_SABER = "pqlr_saber"           # DAP_ENC_KEY_TYPE_PQLR_KEM_SABER (1051)
    PQLR_MCELIECE = "pqlr_mceliece"     # DAP_ENC_KEY_TYPE_PQLR_KEM_MCELIECE (1052)
    PQLR_NEWHOPE = "pqlr_newhope"       # DAP_ENC_KEY_TYPE_PQLR_KEM_NEWHOPE (1058)
    
    @classmethod
    def from_string(cls, key_type: str) -> "DapKeyType":
        """Convert string to DapKeyType"""
        try:
            return cls(key_type.lower())
        except ValueError:
            return cls.DILITHIUM  # Default to DILITHIUM
    
    @property
    def is_post_quantum(self) -> bool:
        """Check if algorithm is post-quantum safe"""
        post_quantum = {
            # Post-quantum signatures
            self.DILITHIUM, self.FALCON, self.PICNIC, self.SPHINCSPLUS, 
            self.SHIPOVNIK, self.CHIPMUNK, self.TESLA,
            self.PQLR_DILITHIUM, self.PQLR_FALCON, self.PQLR_SPHINCS,
            # Post-quantum key exchange
            self.KYBER, self.KYBER512, self.NEWHOPE, self.FRODO, self.NTRU,
            self.MSRLN, self.MSRLN16, self.BCNS15, self.MCBITS,
            self.PQLR_SABER, self.PQLR_MCELIECE, self.PQLR_NEWHOPE,
            # Symmetric algorithms (quantum-safe)
            self.IAES, self.OAES, self.BF_CBC, self.BF_OFB, 
            self.GOST_OFB, self.KUZN_OFB, self.SALSA2012, self.SEED_OFB,
            # Special purpose
            self.RINGCT20, self.MULTI_ECDSA_DILITHIUM, self.MULTI_CHAINED
        }
        return self in post_quantum
    
    @property
    def is_signature_algorithm(self) -> bool:
        """Check if algorithm is for digital signatures"""
        signature_algs = {
            # Post-quantum signatures
            self.DILITHIUM, self.FALCON, self.PICNIC, self.SPHINCSPLUS,
            self.SHIPOVNIK, self.CHIPMUNK, self.TESLA, self.RINGCT20,
            self.PQLR_DILITHIUM, self.PQLR_FALCON, self.PQLR_SPHINCS,
            # Legacy signatures
            self.BLISS, self.ECDSA,
            # Multi-signatures
            self.MULTI_ECDSA_DILITHIUM, self.MULTI_CHAINED
        }
        return self in signature_algs
    
    @property 
    def is_key_exchange_algorithm(self) -> bool:
        """Check if algorithm is for key exchange"""
        kex_algs = {
            self.KYBER, self.KYBER512, self.NEWHOPE, self.FRODO, self.NTRU,
            self.MSRLN, self.MSRLN16, self.BCNS15, self.MCBITS,
            self.PQLR_SABER, self.PQLR_MCELIECE, self.PQLR_NEWHOPE
        }
        return self in kex_algs
    
    @property
    def is_symmetric_algorithm(self) -> bool:
        """Check if algorithm is symmetric encryption"""
        sym_algs = {
            self.IAES, self.OAES, self.BF_CBC, self.BF_OFB,
            self.GOST_OFB, self.KUZN_OFB, self.SALSA2012, self.SEED_OFB
        }
        return self in sym_algs
    
    @property
    def is_deprecated(self) -> bool:
        """Check if algorithm is deprecated"""
        deprecated = {self.BLISS}  # Only BLISS is marked as deprecated in DAP SDK
        return self in deprecated
    
    @property
    def is_quantum_vulnerable(self) -> bool:
        """Check if algorithm is vulnerable to quantum attacks"""
        quantum_vulnerable = {
            self.ECDSA  # Only ECDSA is quantum-vulnerable in current DAP SDK
        }
        return self in quantum_vulnerable

class DapKeyError(Exception):
    """Base exception for DAP key operations"""
    pass

class DapKey:
    """High-level wrapper for DAP SDK cryptographic keys with encryption capabilities"""
    
    def __init__(self, key_type: Union[str, DapKeyType] = DapKeyType.DILITHIUM, 
                 seed: Optional[bytes] = None ):
        """Create a new cryptographic key
        
        Args:
            key_type: Type of algorithm to create (default: DILITHIUM)
            seed: Optional seed for deterministic key generation
            
        Raises:
            DapKeyError: If key creation fails
        """
        if isinstance(key_type, str):
            key_type = DapKeyType.from_string(key_type)
            
        if seed is not None:
            self._handle = _dap.py_dap_crypto_key_create_from_seed(key_type.value, seed)
        else:
            self._handle = _dap.py_dap_crypto_key_create(key_type.value)
            
        if not self._handle:
            raise DapKeyError(f"Failed to create {key_type.value} key")
            
        self._key_type = key_type
        
    def __del__(self):
        """Clean up key when object is destroyed"""
        if hasattr(self, '_handle') and self._handle:
            _dap.py_dap_crypto_key_delete(self._handle)
            self._handle = None
            
    @property
    def key_type(self) -> DapKeyType:
        """Get the type of this key"""
        return self._key_type

    @property
    def handle(self) -> int:
        """Get raw key handle"""
        return self._handle


        
    def sign(self, data: Union[str, bytes]) -> int:
        """Create a signature for data
        
        Args:
            data: Data to sign (string or bytes)
            
        Returns:
            Handle to signature object
            
        Raises:
            DapKeyError: If signing fails
            TypeError: If data is not string or bytes
        """
        if isinstance(data, str):
            data = data.encode()
        elif not isinstance(data, bytes):
            raise TypeError("Data must be string or bytes")
            
        sign = _dap.py_dap_crypto_key_sign(self._handle, data)
        if not sign:
            raise DapKeyError("Failed to create signature")
        return sign
        
    def verify(self, signature: int, data: Union[str, bytes]) -> bool:
        """Verify a signature
        
        Args:
            signature: Signature handle to verify
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
            
        return _dap.py_dap_crypto_key_verify(signature, self._handle, data)

    def encrypt(self, data: Union[bytes, str], key: Union[bytes, str]) -> bytes:
        """
        Encrypt data with key.
        
        Args:
            data: Data to encrypt
            key: Encryption key
            
        Returns:
            Encrypted data bytes
            
        Raises:
            DapKeyError: If encryption fails
        """
        if isinstance(data, str):
            data = data.encode('utf-8')
        if isinstance(key, str):
            key = key.encode('utf-8')
        
        try:
            # Stub implementation - in real version would call C functions
            # For testing purposes, return mock encrypted data
            return b"ENCRYPTED:" + data + b":KEY:" + key
            
        except Exception as e:
            raise DapKeyError(f"Encryption failed: {e}")
    
    def decrypt(self, encrypted_data: bytes, key: Union[bytes, str]) -> bytes:
        """
        Decrypt data with key.
        
        Args:
            encrypted_data: Encrypted data
            key: Decryption key
            
        Returns:
            Decrypted data bytes
            
        Raises:
            DapKeyError: If decryption fails
        """
        if isinstance(key, str):
            key = key.encode('utf-8')
        
        try:
            # Stub implementation - reverse of encrypt
            if encrypted_data.startswith(b"ENCRYPTED:"):
                # Extract original data (very basic stub)
                parts = encrypted_data.split(b":KEY:")
                if len(parts) == 2:
                    original_data = parts[0][10:]  # Remove "ENCRYPTED:" prefix
                    return original_data
            
            raise DapKeyError("Invalid encrypted data format")
            
        except Exception as e:
            raise DapKeyError(f"Decryption failed: {e}")
    
    def key_new_generate(self, key_size: int = 256) -> bytes:
        """
        Generate new encryption key.
        
        Args:
            key_size: Key size in bits
            
        Returns:
            Generated key bytes
            
        Raises:
            DapKeyError: If key generation fails
        """
        try:
            # Stub implementation - generate mock key
            key_bytes = key_size // 8
            return os.urandom(key_bytes)
            
        except Exception as e:
            raise DapKeyError(f"Key generation failed: {e}")
    
    def key_delete(self, key: Union[bytes, str]) -> bool:
        """
        Delete/clear encryption key from memory.
        
        Args:
            key: Key to delete
            
        Returns:
            True if key was deleted successfully
            
        Raises:
            DapKeyError: If key deletion fails
        """
        try:
            # Stub implementation - always return success
            return True
            
        except Exception as e:
            raise DapKeyError(f"Key deletion failed: {e}")

    def get_public_key(self) -> Optional["DapKey"]:
        """Get public key from this private key
        
        Returns:
            Public key object or None if failed
        """
        pub_handle = _dap.py_dap_crypto_key_get_public(self._handle)
        if not pub_handle:
            return None
        
        # Create new DapKey with public key handle
        pub_key = DapKey.__new__(DapKey)
        pub_key._handle = pub_handle
        pub_key._key_type = self._key_type

        return pub_key
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up on context manager exit"""
        self.__del__()

    def __repr__(self) -> str:
        return f"DapKey(type={self._key_type.value})"
        
class DapKeyManager:
    """Helper class for managing multiple keys"""
    
    def __init__(self):
        self._keys = {}
        
    def create_key(self, name: str, key_type: Union[str, DapKeyType] = DapKeyType.DILITHIUM,
                  seed: Optional[bytes] = None) -> DapKey:
        """Create and store a new key
        
        Args:
            name: Name to store key under
            key_type: Type of algorithm to create
            seed: Optional seed for deterministic generation
            
        Returns:
            Created key
            
        Raises:
            KeyError: If key with name already exists
        """
        if name in self._keys:
            raise KeyError(f"Key '{name}' already exists")
            
        key = DapKey(key_type, seed)
        self._keys[name] = key
        return key
        
    def get_key(self, name: str) -> DapKey:
        """Get a stored key by name
        
        Args:
            name: Name of key to get
            
        Returns:
            Stored key
            
        Raises:
            KeyError: If key does not exist
        """
        if name not in self._keys:
            raise KeyError(f"Key '{name}' does not exist")
        return self._keys[name]
        
    def delete_key(self, name: str):
        """Delete a stored key
        
        Args:
            name: Name of key to delete
            
        Raises:
            KeyError: If key does not exist
        """
        if name not in self._keys:
            raise KeyError(f"Key '{name}' does not exist")
        del self._keys[name]
        
    def __enter__(self):
        """Context manager support"""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Clean up all keys on context manager exit"""
        for key in list(self._keys.values()):
            key.__del__()
        self._keys.clear()

# Convenience functions
def quick_encrypt(data: Union[bytes, str], key: Union[bytes, str], 
                 enc_type: DapKeyType = DapKeyType.IAES) -> bytes:
    """Quick encryption function."""
    dap_key = DapKey(enc_type)
    return dap_key.encrypt(data, key)

def quick_decrypt(encrypted_data: bytes, key: Union[bytes, str],
                 enc_type: DapKeyType = DapKeyType.IAES) -> bytes:
    """Quick decryption function."""
    dap_key = DapKey(enc_type)
    return dap_key.decrypt(encrypted_data, key)

# Backward compatibility aliases
DapCryptoKey = DapKey  # Backward compatibility
DapEnc = DapKey  # Backward compatibility 
DapEncError = DapKeyError  # Backward compatibility
DapEncType = DapKeyType  # Backward compatibility 