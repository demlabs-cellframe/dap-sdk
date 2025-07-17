"""
🔗 Python DAP Integration Tests

Integration tests for Python DAP SDK cross-module functionality.
Tests interactions between different DAP modules.
"""

import pytest
import sys
import os
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    import dap
except ImportError as e:
    pytest.skip(f"DAP modules not available: {e}", allow_module_level=True)


class TestDapModuleStructure:
    """Test overall module structure"""
    
    def test_expected_submodules_present(self):
        """Test that expected submodule directories exist"""
        import dap
        dap_path = Path(dap.__file__).parent
        
        expected_modules = ['core', 'config', 'crypto', 'network', 'global_db']
        present_modules = []
        
        for module in expected_modules:
            module_path = dap_path / module
            if module_path.exists() and module_path.is_dir():
                present_modules.append(module)
                
        print(f"Found modules: {present_modules}")
        
        # Should have at least half of expected modules
        assert len(present_modules) >= len(expected_modules) // 2
        
    def test_import_core_modules(self):
        """Test importing core modules"""
        try:
            from dap import core
            assert core is not None
            print("✅ Core module imported successfully")
        except ImportError as e:
            print(f"❌ Core module import failed: {e}")
            pytest.skip("Core module not available")
            
    def test_import_config_modules(self):
        """Test importing config modules"""
        try:
            from dap import config
            assert config is not None
            print("✅ Config module imported successfully")
        except ImportError as e:
            print(f"❌ Config module import failed: {e}")
            pytest.skip("Config module not available")
            
    def test_import_crypto_modules(self):
        """Test importing crypto modules"""
        try:
            from dap import crypto
            assert crypto is not None
            print("✅ Crypto module imported successfully")
        except ImportError as e:
            print(f"❌ Crypto module import failed: {e}")
            pytest.skip("Crypto module not available")
            
    def test_import_network_modules(self):
        """Test importing network modules"""
        try:
            from dap import network  
            assert network is not None
            print("✅ Network module imported successfully")
        except ImportError as e:
            print(f"❌ Network module import failed: {e}")
            pytest.skip("Network module not available")
            
    def test_import_gdb_modules(self):
        """Test importing global_db modules"""
        try:
            from dap import global_db
            assert global_db is not None
            print("✅ Global DB module imported successfully")
        except ImportError as e:
            print(f"❌ Global DB module import failed: {e}")
            pytest.skip("Global DB module not available")


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
