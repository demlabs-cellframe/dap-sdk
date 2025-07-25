#!/usr/bin/env python3
"""
Demo script for testing the new unified DapSign API.
Demonstrates all signature types: single, composite, and aggregated.
"""

import sys
import traceback
from dap.crypto import (
    DapCryptoKey, DapKeyType,
    DapSign, DapSignError, DapSignType, DapSignMetadata,
    get_recommended_signature_types,
    get_deprecated_signature_types,
    get_quantum_vulnerable_signature_types,
    get_legacy_deprecated_signature_types
)

def test_signature_metadata():
    """Test signature metadata and classification"""
    print("\n=== Testing Signature Metadata ===")
    
    print("📊 Recommended signature types:")
    for sign_type in get_recommended_signature_types():
        metadata = DapSignMetadata.get_metadata(sign_type)
        print(f"  ✅ {sign_type.name}: {metadata['description']}")
    
    print("\n🟡 Deprecated signature types:")
    for sign_type in get_deprecated_signature_types():
        metadata = DapSignMetadata.get_metadata(sign_type)
        qv_status = "quantum vulnerable" if metadata.get('quantum_vulnerable') else "legacy deprecated"
        print(f"  ⚠️  {sign_type.name}: {metadata['description']} ({qv_status})")

def test_single_signatures():
    """Test single signature operations"""
    print("\n=== Testing Single Signatures ===")
    
    test_data = b"Hello, DAP SDK!"
    
    # Test all recommended signature types
    for sign_type in get_recommended_signature_types():
        try:
            # Skip multi-signature types for single signature test
            if DapSignMetadata.supports_multi_signature(sign_type):
                continue
                
            print(f"\n🔑 Testing {sign_type.name} signature...")
            
            # Create private key
            key_type = DapKeyType(sign_type.value)
            pvt_key = DapCryptoKey(key_type)
            
            # Create signature using static method
            signature = DapSign.sign(to_sign=test_data, pvt_key=pvt_key)
            
            print(f"  ✅ Created {signature.sign_type.name} signature")
            print(f"  🔒 Quantum secure: {signature.is_quantum_secure()}")
            print(f"  📦 Public keys stored: {len(signature.keys)}")
            
            # Verify signature
            is_valid = signature.verify(to_sign=test_data)
            print(f"  ✅ Verification: {'PASSED' if is_valid else 'FAILED'}")
            
            # Test with wrong data
            is_invalid = signature.verify(to_sign=b"wrong data")
            print(f"  ❌ Wrong data verification: {'FAILED' if not is_invalid else 'UNEXPECTED PASS'}")
            
        except Exception as e:
            print(f"  ❌ Error with {sign_type.name}: {e}")

def test_composite_signatures():
    """Test composite multi-signature operations"""
    print("\n=== Testing Composite Multi-Signatures ===")
    
    test_data = b"Composite multi-signature test data"
    
    try:
        # Create multiple private keys for different algorithms
        pvt_keys = [
            DapCryptoKey(DapKeyType.DILITHIUM),
            DapCryptoKey(DapKeyType.FALCON),
            DapCryptoKey(DapKeyType.SPHINCSPLUS)
        ]
        
        print(f"🔑 Created {len(pvt_keys)} private keys for composite signature")
        
        # Create composite signature (auto-detect)
        signature = DapSign.sign(to_sign=test_data, pvt_keys=pvt_keys)
        
        print(f"  ✅ Created {signature.sign_type.name} signature")
        print(f"  🤝 Multi-signature support: {signature.supports_multi_signature()}")
        print(f"  🔒 Quantum secure: {signature.is_quantum_secure()}")
        print(f"  📦 Public keys stored: {len(signature.keys)}")
        
        # Verify with all keys
        is_valid = signature.verify(to_sign=test_data)
        print(f"  ✅ Full verification: {'PASSED' if is_valid else 'FAILED'}")
        
        # Verify with subset of keys
        subset_keys = signature.keys[:2]  # First 2 keys
        is_subset_valid = signature.verify(to_sign=test_data, pub_keys=subset_keys)
        print(f"  ✅ Subset verification: {'PASSED' if is_subset_valid else 'FAILED'}")
        
        # Test sign_add functionality
        additional_key = DapCryptoKey(DapKeyType.SHIPOVNIK)
        signature.sign_add(to_sign=test_data, pvt_key=additional_key)
        print(f"  ➕ Added signature, now {len(signature.keys)} keys total")
        
        # Verify after addition
        is_expanded_valid = signature.verify(to_sign=test_data)
        print(f"  ✅ Expanded verification: {'PASSED' if is_expanded_valid else 'FAILED'}")
        
    except Exception as e:
        print(f"  ❌ Composite signature error: {e}")
        traceback.print_exc()

def test_aggregated_signatures():
    """Test aggregated (Chipmunk) signature operations"""
    print("\n=== Testing Aggregated Signatures ===")
    
    test_data = b"Aggregated signature test data"
    
    try:
        # Create multiple CHIPMUNK keys for aggregated signature
        pvt_keys = [
            DapCryptoKey(DapKeyType.CHIPMUNK),
            DapCryptoKey(DapKeyType.CHIPMUNK),
            DapCryptoKey(DapKeyType.CHIPMUNK)
        ]
        
        print(f"🔑 Created {len(pvt_keys)} CHIPMUNK keys for aggregated signature")
        
        # Create aggregated signature (auto-detect)
        signature = DapSign.sign(to_sign=test_data, pvt_keys=pvt_keys)
        
        print(f"  ✅ Created {signature.sign_type.name} signature")
        print(f"  🤝 Multi-signature support: {signature.supports_multi_signature()}")
        print(f"  🔗 Aggregated support: {signature.supports_aggregation()}")
        print(f"  🔒 Quantum secure: {signature.is_quantum_secure()}")
        print(f"  📦 Public keys stored: {len(signature.keys)}")
        
        # Verify signature
        is_valid = signature.verify(to_sign=test_data)
        print(f"  ✅ Verification: {'PASSED' if is_valid else 'FAILED'}")
        
        # Test sign_add functionality
        additional_key = DapCryptoKey(DapKeyType.CHIPMUNK)
        signature.sign_add(to_sign=test_data, pvt_key=additional_key)
        print(f"  ➕ Added signature, now {len(signature.keys)} keys total")
        
        # Verify after addition
        is_expanded_valid = signature.verify(to_sign=test_data)
        print(f"  ✅ Expanded verification: {'PASSED' if is_expanded_valid else 'FAILED'}")
        
    except Exception as e:
        print(f"  ❌ Aggregated signature error: {e}")
        traceback.print_exc()

def test_constructor_modes():
    """Test different DapSign constructor modes"""
    print("\n=== Testing Constructor Modes ===")
    
    test_data = b"Constructor modes test"
    
    try:
        # Mode 1: Static method + single key
        pvt_key = DapCryptoKey(DapKeyType.DILITHIUM)
        sig1 = DapSign.sign(to_sign=test_data, pvt_key=pvt_key)
        print(f"  ✅ Mode 1 (static + single): {sig1.sign_type.name}")
        
        # Mode 2: Constructor + single key
        sig2 = DapSign(to_sign=test_data, pvt_key=pvt_key)
        print(f"  ✅ Mode 2 (constructor + single): {sig2.sign_type.name}")
        
        # Mode 3: Multi-signature auto-detect
        pvt_keys = [DapCryptoKey(DapKeyType.CHIPMUNK), DapCryptoKey(DapKeyType.CHIPMUNK)]
        sig3 = DapSign(to_sign=test_data, pvt_keys=pvt_keys)
        print(f"  ✅ Mode 3 (multi auto-detect): {sig3.sign_type.name}")
        
        # Mode 4: Handle wrapping
        sig4 = DapSign(handle=sig1.handle, sign_type=sig1.sign_type, keys=sig1.keys)
        print(f"  ✅ Mode 4 (handle wrap): {sig4.sign_type.name}")
        
        # Verify all work
        assert sig1.verify(to_sign=test_data)
        assert sig2.verify(to_sign=test_data)
        assert sig3.verify(to_sign=test_data)
        assert sig4.verify(to_sign=test_data)
        print(f"  ✅ All constructor modes verified successfully")
        
    except Exception as e:
        print(f"  ❌ Constructor modes error: {e}")
        traceback.print_exc()

def test_deprecated_signatures():
    """Test deprecated signature types"""
    print("\n=== Testing Deprecated Signatures ===")
    
    deprecated_types = get_deprecated_signature_types()
    
    for sign_type in deprecated_types:
        try:
            # Skip multi-signature types
            if DapSignMetadata.supports_multi_signature(sign_type):
                continue
                
            metadata = DapSignMetadata.get_metadata(sign_type)
            qv_status = "⚠️ Quantum Vulnerable" if metadata.get('quantum_vulnerable') else "🟡 Legacy Deprecated"
            
            print(f"\n{qv_status}: {sign_type.name}")
            print(f"  📝 {metadata['description']}")
            
            # Note: We're not actually testing deprecated signatures here
            # since they may not be fully implemented in test environment
            
        except Exception as e:
            print(f"  ❌ Error with {sign_type.name}: {e}")

def main():
    """Main demo function"""
    print("🚀 DAP SDK New Crypto API Demo")
    print("=" * 50)
    
    try:
        test_signature_metadata()
        test_single_signatures()
        test_composite_signatures()
        test_aggregated_signatures()
        test_constructor_modes()
        test_deprecated_signatures()
        
        print("\n" + "=" * 50)
        print("✅ Demo completed successfully!")
        
    except Exception as e:
        print(f"\n❌ Demo failed with error: {e}")
        traceback.print_exc()
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 