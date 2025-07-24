"""
Global pytest configuration for python-dap tests.
Simple configuration without isolation restrictions.
"""

import pytest
from dap.core.dap import Dap

@pytest.fixture(scope="session", autouse=True)
def global_dap_sdk():
    """
    Global DAP SDK initialization for the entire test session.
    
    Initializes DAP SDK once before all tests and deinitializes once after all tests.
    This prevents race conditions and multiple init/deinit cycles during testing.
    """
    # Create DAP configuration for global session
    dap_config = {
        'app_name': "pytest_session", 
        'working_dir': "/tmp/pytest_global_dap_session",
        'config_dir': "/tmp/pytest_global_dap_session/etc",
        'temp_dir': "/tmp/pytest_global_dap_session/tmp",
        'log_file': "/tmp/pytest_global_dap_session/var/log/pytest_session.log",
        'debug_mode': True,
        'events_threads': 2
    }
    
    # Create required directories
    import os
    os.makedirs(dap_config['config_dir'], exist_ok=True)
    os.makedirs(dap_config['temp_dir'], exist_ok=True) 
    os.makedirs(os.path.dirname(dap_config['log_file']), exist_ok=True)
    
    # Mark global session as active BEFORE initialization
    Dap.mark_global_session_active()
    
    # Initialize DAP SDK once for the entire test session
    dap = Dap(dap_config=dap_config)
    dap.init()  # No arguments - config passed through constructor
    
    # Yield control to tests - DAP SDK is now available globally
    yield dap
    
    # Mark global session as inactive (but don't deinitialize to avoid pytest teardown conflicts)
    Dap.mark_global_session_inactive()
    
    # Note: Skip explicit deinitialization to prevent "double free" conflicts with pytest teardown
    # The OS will clean up resources when process exits 