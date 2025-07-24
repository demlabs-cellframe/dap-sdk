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
        """Setup test directories ONLY (DAP SDK initialized globally in conftest.py)"""
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
        
        # NOTE: DAP SDK is initialized globally in conftest.py
        # No individual init/deinit needed - prevents race conditions
        
        self.initialized = True
        print(f"✅ Test directories created (DAP SDK globally initialized):")
        print(f"  Working dir: {self.test_dir}")
        print(f"  Config dir: {self.config_dir}")
        print(f"  Temp dir: {self.temp_dir}")
        print(f"  Log file: {self.log_file}")
    
    def cleanup(self):
        """Cleanup test environment (NO DAP SDK deinit - handled globally)"""
        # NOTE: DAP SDK deinitialized globally in conftest.py
        # No individual deinit needed - prevents race conditions
        
        if self.test_dir and os.path.exists(self.test_dir):
            try:
                # CRITICAL: DAP SDK cleanup causes race condition with background threads
                # SKIP cleanup for unit tests to prevent SegFault/Abort  
                print(f"🔄 Skipping cleanup for unit test environment to prevent race condition: {self.test_dir}")
                print(f"   (Temporary files will be cleaned up by OS or manually)")
                return
                
            except Exception as e:
                print(f"⚠️ Failed to cleanup {self.test_dir}: {e}")
    
    def _safe_cleanup_directory(self, dir_path):
        """
        Safely cleanup directory with retry logic for file descriptor issues
        """
        import time
        max_retries = 3
        retry_delay = 0.05  # 50ms between retries
        
        for attempt in range(max_retries):
            try:
                shutil.rmtree(dir_path)
                return  # Success
            except OSError as e:
                if attempt < max_retries - 1:
                    # Wait a bit longer for file descriptors to close
                    time.sleep(retry_delay * (attempt + 1))
                    continue
                else:
                    # Final attempt failed, re-raise
                    raise e
    
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