"""
Python DAP SDK - Top-level module wrapper
Re-exports all functionality from dap.python_dap for backwards compatibility
"""

# Import all symbols from the actual C extension
from dap.python_dap import *

# Make the module look like the real python_dap
__name__ = 'python_dap'
__file__ = __file__
