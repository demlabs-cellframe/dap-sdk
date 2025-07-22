"""
Test initialization helper for python-dap tests
Provides custom DAP SDK initialization with test-specific paths
"""

import os
import tempfile
import shutil
from typing import Optional

class TestDAPInitializer:
    """Helper class for initializing DAP SDK with test-specific paths"""
    
    def __init__(self):
        self.test_dir: Optional[str] = None
        self.config_dir: Optional[str] = None
        self.temp_dir: Optional[str] = None
        self.log_file: Optional[str] = None
        self.initialized = False
    
    def setup_test_environment(self, test_name: str = "python_dap_test"):
        """Setup test directories and initialize DAP SDK"""
        # Create temporary test directory
        self.test_dir = tempfile.mkdtemp(prefix=f"{test_name}_")
        self.config_dir = os.path.join(self.test_dir, "etc")
        self.temp_dir = os.path.join(self.test_dir, "tmp") 
        log_dir = os.path.join(self.test_dir, "var", "log")
        self.log_file = os.path.join(log_dir, f"{test_name}.log")
        
        # Create directories
        os.makedirs(self.config_dir, exist_ok=True)
        os.makedirs(self.temp_dir, exist_ok=True)
        os.makedirs(log_dir, exist_ok=True)
        
        # Initialize DAP SDK with test paths
        import python_dap
        python_dap.dap_sdk_init_with_params(
            test_name,              # app_name
            self.test_dir,          # working_dir
            self.config_dir,        # config_dir
            self.temp_dir,          # temp_dir
            self.log_file,          # log_file
            2,                      # events_threads (increase for stability)
            5000,                   # events_timeout (shorter for tests)
            True                    # debug_mode
        )
        
        # Give events system time to fully start
        import time
        time.sleep(0.2)  # 200ms should be enough for events to start
        
        self.initialized = True
        print(f"✅ Test DAP SDK initialized with paths:")
        print(f"  Working dir: {self.test_dir}")
        print(f"  Config dir: {self.config_dir}")
        print(f"  Temp dir: {self.temp_dir}")
        print(f"  Log file: {self.log_file}")
    
    def cleanup(self):
        """Cleanup test environment"""
        if self.initialized:
            try:
                import python_dap
                python_dap.dap_sdk_deinit()
            except:
                pass
        
        if self.test_dir and os.path.exists(self.test_dir):
            try:
                shutil.rmtree(self.test_dir)
                print(f"🧹 Cleaned up test directory: {self.test_dir}")
            except Exception as e:
                print(f"⚠️ Failed to cleanup {self.test_dir}: {e}")
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()


def init_test_dap_sdk(test_name: str = "python_dap_test"):
    """
    Initialize DAP SDK for testing with safe paths
    
    Usage:
        # At the beginning of test file
        init_test_dap_sdk("my_test")
        import dap  # Now safe to import
    """
    initializer = TestDAPInitializer()
    initializer.setup_test_environment(test_name)
    return initializer


# Global test initializer for simple usage
_global_test_init = None

def setup_global_test_init():
    """Setup global test initialization - call once per test session"""
    global _global_test_init
    if _global_test_init is None:
        _global_test_init = init_test_dap_sdk("global_test")
    return _global_test_init

def cleanup_global_test_init():
    """Cleanup global test initialization"""
    global _global_test_init
    if _global_test_init:
        _global_test_init.cleanup()
        _global_test_init = None 