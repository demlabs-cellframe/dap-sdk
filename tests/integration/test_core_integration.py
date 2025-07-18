"""
Integration tests for DAP Core functionality
Tests interaction between different core subsystems
"""

import pytest
import sys
from pathlib import Path
import time

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))


class TestDapCoreIntegration:
    """Integration tests for DAP core system"""
    
    @pytest.mark.timeout(30)
    def test_full_initialization_cycle(self):
        """Test complete initialization and deinitialization cycle"""
        from dap.core.dap import Dap
        
        # Test init/deinit cycle
        dap = Dap()
        
        # Initialize
        result = dap.init()
        assert result is True
        assert dap.is_initialized
        
        # Check all subsystems are available
        assert dap.logging is not None
        assert dap.time is not None
        assert dap.system is not None
        assert dap.type is not None
        
        # Deinitialize
        dap.deinit()
        assert not dap.is_initialized
    
    @pytest.mark.timeout(30)
    def test_context_manager_integration(self):
        """Test DAP context manager with subsystem usage"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            # Inside context, DAP should be initialized
            assert dap.is_initialized
            
            # Test subsystem interaction
            current_time = dap.time.now()
            assert current_time > 0
            
            # Test logging subsystem
            dap.logging.info("Integration test message")
            
        # After context, DAP should be deinitialized
        assert not dap.is_initialized
    
    @pytest.mark.timeout(30)
    def test_multiple_init_attempts(self):
        """Test that multiple init attempts are handled correctly"""
        from dap.core.dap import Dap
        
        dap = Dap()
        
        # First init
        result1 = dap.init()
        assert result1 is True
        
        # Second init should be safe
        result2 = dap.init()
        assert result2 is True
        
        # Cleanup
        dap.deinit()
    
    @pytest.mark.timeout(30)
    def test_subsystem_interaction(self):
        """Test interaction between different subsystems"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            # Test time and logging interaction
            timestamp = dap.time.now()
            dap.logging.debug(f"Current timestamp: {timestamp}")
            
            # Test system memory functions exist
            assert dap.system.malloc is not None
            assert dap.system.free is not None


class TestLoggingIntegration:
    """Integration tests for logging system"""
    
    @pytest.mark.timeout(10)
    def test_logging_levels(self):
        """Test logging with different levels"""
        from dap.core.dap import Dap
        from dap.core.logging import DapLogLevel
        
        with Dap() as dap:
            # Set different log levels
            dap.logging.set_level(DapLogLevel.DEBUG)
            current_level = dap.logging.get_level()
            assert current_level == DapLogLevel.DEBUG
            
            # Test logging at different levels
            dap.logging.debug("Debug message")
            dap.logging.info("Info message")
            dap.logging.warning("Warning message")
            dap.logging.error("Error message")
            dap.logging.critical("Critical message")
    
    @pytest.mark.timeout(10)
    def test_logging_with_time(self):
        """Test logging with timestamp formatting"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            # Get current time
            current_time = dap.time.now()
            time_str = dap.time.to_str_rfc822(current_time // 1_000_000_000)  # Convert to seconds
            
            # Log with timestamp
            dap.logging.info(f"Test at {time_str}")


class TestMemoryIntegration:
    """Integration tests for memory management"""
    
    @pytest.mark.timeout(10)
    def test_memory_allocation_cycle(self):
        """Test memory allocation and deallocation"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            # Allocate memory
            size = 1024
            ptr = dap.system.malloc(size)
            assert ptr is not None
            
            # Free memory
            dap.system.free(ptr)
    
    @pytest.mark.timeout(10)
    def test_memory_reallocation(self):
        """Test memory reallocation"""
        from dap.core.dap import Dap
        
        with Dap() as dap:
            # Initial allocation
            initial_size = 512
            ptr = dap.system.malloc(initial_size)
            assert ptr is not None
            
            # Reallocate to larger size
            new_size = 2048
            new_ptr = dap.system.realloc(ptr, new_size)
            assert new_ptr is not None
            
            # Free memory
            dap.system.free(new_ptr)


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 