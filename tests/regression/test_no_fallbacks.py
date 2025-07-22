"""
Regression tests to ensure no fallback implementations
Tests that all modules use real DAP SDK bindings
"""

import pytest
import sys
from pathlib import Path
import importlib
import inspect

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

# Import and setup test initialization BEFORE importing dap modules
sys.path.insert(0, str(Path(__file__).parent.parent))
from test_init_helper import init_test_dap_sdk

# Initialize DAP SDK with test paths before any dap imports
_test_init = init_test_dap_sdk("regression_test")


class TestNoFallbacks:
    """Regression tests to ensure no fallback implementations exist"""
    
    def test_no_fallback_warnings(self):
        """Test that no modules show fallback warnings"""
        # List of modules to check
        modules_to_check = [
            'dap.core.dap',
            'dap.core.logging', 
            'dap.core.time',
            'dap.core.system',
            'dap.config.config',
            'dap.crypto.keys',
            'dap.crypto.hash',
            'dap.crypto.sign',
            'dap.network.client',
            'dap.network.server',
            'dap.network.stream',
            'dap.events.events',
            'dap.global_db.gdb'
        ]
        
        for module_name in modules_to_check:
            try:
                # Import module
                module = importlib.import_module(module_name)
                
                # Check module source for fallback indicators
                if hasattr(module, '__file__') and module.__file__:
                    source_file = Path(module.__file__)
                    if source_file.exists():
                        content = source_file.read_text()
                        
                        # Check for fallback indicators
                        assert "fallback implementation" not in content.lower(), \
                            f"Fallback found in {module_name}"
                        assert "mock_" not in content, \
                            f"Mock implementation found in {module_name}"
                        assert "not available" not in content, \
                            f"'Not available' message found in {module_name}"
                        
            except ImportError:
                # Module not found is OK for this test
                pass
    
    def test_python_dap_imported(self):
        """Test that python_dap C extension is properly imported"""
        try:
            import python_dap
            
            # Check that it's not a fallback module
            assert hasattr(python_dap, 'dap_common_init')
            assert hasattr(python_dap, 'dap_config_init')
            assert hasattr(python_dap, 'dap_set_log_level')
            assert hasattr(python_dap, 'dap_time_now')
            
            # Check for full implementation flag
            assert hasattr(python_dap, 'FULL_IMPLEMENTATION')
            assert python_dap.FULL_IMPLEMENTATION == 1
            
        except ImportError:
            pytest.fail("python_dap C extension not available - build it first")
    
    def test_no_critical_exit_on_import(self):
        """Test that importing modules doesn't cause critical exit"""
        modules_to_test = [
            'dap.core.logging',
            'dap.core.time',
            'dap.network.server',
            'dap.network.client',
            'dap.network.stream',
            'dap.network.http',
            'dap.events.events'
        ]
        
        for module_name in modules_to_test:
            try:
                # This should not exit the program
                importlib.import_module(module_name)
            except SystemExit:
                pytest.fail(f"Module {module_name} called sys.exit on import!")
            except ImportError:
                # ImportError is OK if module doesn't exist
                pass
    
    def test_all_c_functions_available(self):
        """Test that all expected C functions are available"""
        import python_dap
        
        # Core functions
        core_functions = [
            'dap_common_init', 'dap_common_deinit',
            'dap_config_init', 'dap_config_deinit',
            'py_dap_config_open', 'py_dap_config_close',
            'py_dap_config_get_item_str', 'py_dap_config_get_item_int',
            'py_dap_config_get_item_bool', 'py_dap_config_set_item_str',
            'py_dap_config_set_item_int', 'py_dap_config_set_item_bool'
        ]
        
        # Logging functions
        logging_functions = [
            'dap_set_log_level', 'dap_log_level_set',
            'dap_get_log_level', 'dap_log_set_external_output'
        ]
        
        # Time functions
        time_functions = [
            'dap_time_now', 'py_dap_time_now',
            'dap_time_to_str_rfc822'
        ]
        
        # Memory functions
        memory_functions = [
            'dap_malloc', 'dap_free', 'dap_calloc', 'dap_realloc',
            'py_dap_malloc', 'py_dap_free', 'py_dap_calloc', 'py_dap_realloc'
        ]
        
        # Network functions
        network_functions = [
            'server_new', 'server_delete', 'server_listen',
            'dap_server_start', 'dap_server_stop',
            'dap_client_new', 'dap_client_delete', 'dap_client_connect_to',
            'dap_client_disconnect'
        ]
        
        # Events functions
        events_functions = [
            'dap_events_init', 'dap_events_deinit',
            'dap_events_start', 'dap_events_stop'
        ]
        
        # Check all functions exist
        all_functions = (core_functions + logging_functions + time_functions + 
                        memory_functions + network_functions + events_functions)
        
        for func_name in all_functions:
            assert hasattr(python_dap, func_name), \
                f"Function {func_name} not found in python_dap"


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 