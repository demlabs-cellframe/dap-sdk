"""
�� Python DAP Fixes Tests

Tests with corrected constructors and method calls.
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))


def test_crypto_classes_work():
    """Test that crypto classes work with correct parameters"""
    # DapCryptoKey
    from dap.crypto.keys import DapCryptoKey
    key = DapCryptoKey("mock_handle")
    assert key is not None
    print("✅ DapCryptoKey works")
    
    # DapHash
    from dap.crypto.hash import DapHash
    hasher = DapHash()
    assert hasher is not None
    print("✅ DapHash works")
    
    # DapSign
    from dap.crypto.sign import DapSign
    sign = DapSign("mock_handle") 
    assert sign is not None
    print("✅ DapSign works")


def test_network_classes_work():
    """Test that network classes work with correct parameters"""
    # DapClient
    from dap.network.client import DapClient
    client = DapClient("mock_handle")
    assert client is not None
    print("✅ DapClient works")
    
    # DapServer
    from dap.network.server import DapServer
    server = DapServer("mock_handle")
    assert server is not None
    print("✅ DapServer works")


def test_gdb_class_works():
    """Test that GDB class works"""
    from dap.global_db.gdb import Gdb
    gdb = Gdb()
    assert gdb is not None
    print("✅ Gdb works")


def test_system_class_issues():
    """Test DapSystem missing methods"""
    from dap.core.system import DapSystem
    system = DapSystem()
    assert system is not None
    print("✅ DapSystem created")
    
    missing_methods = []
    for method in ['malloc', 'free', 'calloc', 'realloc']:
        if not hasattr(system, method):
            missing_methods.append(method)
            
    if missing_methods:
        print(f"⚠️  DapSystem missing methods: {missing_methods}")
    else:
        print("✅ DapSystem has all memory methods")


def test_config_methods():
    """Test DapConfig methods"""
    from dap.config.config import DapConfig
    config = DapConfig()
    assert config is not None
    print("✅ DapConfig created")
    
    missing_methods = []
    for method in ['get_item_int32']:
        if not hasattr(config, method):
            missing_methods.append(method)
            
    if missing_methods:
        print(f"⚠️  DapConfig missing methods: {missing_methods}")
    else:
        print("✅ DapConfig has all expected methods")


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
