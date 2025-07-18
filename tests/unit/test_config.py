"""
Unit tests for DAP Config module
Tests configuration management functionality
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))


class TestDapConfig:
    """Test cases for DapConfig class"""
    
    def test_config_creation(self):
        """Test DapConfig creation"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert config is not None
    
    def test_config_init_deinit(self):
        """Test config initialization and deinitialization"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'init')
        assert hasattr(config, 'deinit')
        assert callable(config.init)
        assert callable(config.deinit)
    
    def test_config_open_close(self):
        """Test config open and close methods"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'open')
        assert hasattr(config, 'close')
        assert callable(config.open)
        assert callable(config.close)
    
    def test_config_getter_methods(self):
        """Test config getter methods"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'get_item_str')
        assert hasattr(config, 'get_item_int')
        assert hasattr(config, 'get_item_bool')
        assert callable(config.get_item_str)
        assert callable(config.get_item_int)
        assert callable(config.get_item_bool)
    
    def test_config_setter_methods(self):
        """Test config setter methods"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'set_item_str')
        assert hasattr(config, 'set_item_int')
        assert hasattr(config, 'set_item_bool')
        assert callable(config.set_item_str)
        assert callable(config.set_item_int)
        assert callable(config.set_item_bool)
    
    def test_config_sys_dir(self):
        """Test config system directory method"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'get_sys_dir')
        assert callable(config.get_sys_dir)
    
    def test_config_handle_property(self):
        """Test config handle property"""
        from dap.config.config import DapConfig
        
        config = DapConfig()
        assert hasattr(config, 'handle')
    
    def test_config_singleton(self):
        """Test that DapConfig can be instantiated multiple times"""
        from dap.config.config import DapConfig
        
        config1 = DapConfig()
        config2 = DapConfig()
        
        # Unlike Dap core, configs can be multiple instances
        assert config1 is not config2


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 