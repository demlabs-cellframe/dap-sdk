"""
Unit tests for DAP Core module
Tests basic functionality of core DAP systems
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))


class TestDapCore:
    """Test cases for Dap core class"""
    
    def test_dap_singleton(self):
        """Test that Dap follows singleton pattern"""
        from dap.core.dap import Dap
        
        dap1 = Dap()
        dap2 = Dap()
        
        assert dap1 is dap2, "Dap should be a singleton"
    
    def test_dap_initialization(self):
        """Test Dap initialization"""
        from dap.core.dap import Dap
        
        dap = Dap()
        assert dap is not None
        assert hasattr(dap, 'init')
        assert hasattr(dap, 'deinit')
        assert hasattr(dap, 'is_initialized')
    
    def test_dap_context_manager(self):
        """Test Dap as context manager"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            assert dap is not None
            assert dap.is_initialized
    
    def test_dap_subsystems(self):
        """Test Dap subsystem access"""
        from dap.core.dap import Dap
        
        dap = Dap()
        assert hasattr(dap, 'type')
        assert hasattr(dap, 'logging')
        assert hasattr(dap, 'time')
        assert hasattr(dap, 'system')


class TestDapSystem:
    """Test cases for DapSystem class"""
    
    def test_system_creation(self):
        """Test DapSystem creation"""
        from dap.core.system import DapSystem
        
        system = DapSystem()
        assert system is not None
    
    def test_memory_functions_exist(self):
        """Test that all memory functions exist"""
        from dap.core.system import DapSystem
        
        system = DapSystem()
        assert hasattr(system, 'malloc')
        assert hasattr(system, 'free')
        assert hasattr(system, 'calloc')
        assert hasattr(system, 'realloc')
    
    def test_memory_functions_callable(self):
        """Test that memory functions are callable"""
        from dap.core.system import DapSystem
        
        system = DapSystem()
        assert callable(system.malloc)
        assert callable(system.free)
        assert callable(system.calloc)
        assert callable(system.realloc)
    
    def test_exec_function(self):
        """Test system exec function"""
        from dap.core.system import DapSystem
        
        system = DapSystem()
        assert hasattr(system, 'py_exec_with_ret_multistring')
        assert callable(system.py_exec_with_ret_multistring)


class TestDapLogging:
    """Test cases for DapLogging class"""
    
    def test_logging_creation(self):
        """Test DapLogging creation"""
        from dap.core.logging import DapLogging
        
        logger = DapLogging()
        assert logger is not None
    
    def test_logging_levels(self):
        """Test logging level methods"""
        from dap.core.logging import DapLogging
        
        logger = DapLogging()
        assert hasattr(logger, 'set_level')
        assert hasattr(logger, 'get_level')
    
    def test_logging_methods(self):
        """Test logging methods exist"""
        from dap.core.logging import DapLogging
        
        logger = DapLogging()
        assert hasattr(logger, 'debug')
        assert hasattr(logger, 'info')
        assert hasattr(logger, 'warning')
        assert hasattr(logger, 'error')
        assert hasattr(logger, 'critical')


class TestDapTime:
    """Test cases for DapTime class"""
    
    def test_time_creation(self):
        """Test DapTime creation"""
        from dap.core.time import DapTime
        
        time_helper = DapTime()
        assert time_helper is not None
    
    def test_time_now_methods(self):
        """Test time now methods"""
        from dap.core.time import DapTime
        
        time_helper = DapTime()
        assert hasattr(time_helper, 'now')
        assert hasattr(time_helper, 'now_sec')
        assert hasattr(time_helper, 'now_usec')
        assert hasattr(time_helper, 'now_dap')
    
    def test_time_conversion_methods(self):
        """Test time conversion methods"""
        from dap.core.time import DapTime
        
        time_helper = DapTime()
        assert hasattr(time_helper, 'to_str_rfc822')
        assert hasattr(time_helper, 'from_str_rfc822')


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 