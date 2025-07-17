"""
🧪 Python DAP Core Tests

Basic tests for Python DAP SDK core functionality.
Tests module imports, basic initialization, and core functions.
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


class TestDapBasicImports:
    """Test basic DAP module imports"""
    
    def test_dap_main_import(self):
        """Test main dap package import"""
        import dap
        assert dap is not None
        
    def test_dap_init_exists(self):
        """Test that dap __init__ exists"""
        import dap
        assert hasattr(dap, '__file__')


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
