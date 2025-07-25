"""
DAP Config File Implementation
Clean API without fallbacks or mocks.
"""

import logging
from pathlib import Path
from typing import Optional, Dict, Any

# Import native DAP file functions - Fail Fast principle
from ..python_dap import (
    py_dap_config_open as dap_config_open,
    py_dap_config_close as dap_config_close,
    py_dap_config_get_item_str as dap_config_get_item_str
)

logger = logging.getLogger(__name__)


class DapConfigFile:
    """DAP Configuration File Management
    
    Clean implementation without fallbacks - native bindings required.
    """
    
    def __init__(self, config_path: str):
        """Initialize config file instance"""
        self.config_path = Path(config_path)
        self._config_handle = None
        
    def open(self) -> bool:
        """Open configuration file"""
        if not self.config_path.exists():
            logger.error(f"Config file not found: {self.config_path}")
            return False
            
        self._config_handle = dap_config_open(str(self.config_path))
        return self._config_handle is not None
        
    def close(self):
        """Close configuration file"""
        if self._config_handle:
            dap_config_close(self._config_handle)
            self._config_handle = None
            
    def get_item(self, section: str, key: str, default: str = "") -> str:
        """Get configuration item"""
        if not self._config_handle:
            raise RuntimeError("Config file not open")
            
        return dap_config_get_item_str(self._config_handle, section, key, default)
        
    def __enter__(self):
        """Context manager entry"""
        if not self.open():
            raise RuntimeError(f"Failed to open config file: {self.config_path}")
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.close() 