"""
📝 DAP Logging Management

Direct Python wrapper over DAP logging functions.
Handles log level management and logging configuration.
"""

import logging
import sys
from typing import Union
from enum import Enum

# Import existing DAP functions - CREATE MINIMAL STUBS FOR MISSING FUNCTIONS
try:
    from python_dap import (
        dap_common_init, dap_common_deinit  # These are available
    )
    
    # Create minimal stubs for missing logging functions
    def dap_set_log_level(level):
        """Stub for missing dap_set_log_level - uses Python logging instead"""
        python_levels = {0: logging.CRITICAL, 1: logging.ERROR, 2: logging.WARNING, 
                        3: logging.INFO, 4: logging.DEBUG}
        if level in python_levels:
            logging.getLogger().setLevel(python_levels[level])
        return 0
    
    def dap_log_level_set(level):
        """Alias for dap_set_log_level"""
        return dap_set_log_level(level)
        
    def dap_get_log_level():
        """Stub for getting log level"""
        python_level = logging.getLogger().level
        level_map = {logging.CRITICAL: 0, logging.ERROR: 1, logging.WARNING: 2,
                    logging.INFO: 3, logging.DEBUG: 4}
        return level_map.get(python_level, 3)  # Default to INFO
    
    NATIVE_LOGGING_AVAILABLE = True
    logging.info("✅ DAP logging stubs created successfully")
except ImportError as e:
    logging.critical("🚨 CRITICAL ERROR: python_dap not available - C bindings failed to load!")
    logging.critical("Cannot continue without native DAP SDK bindings.")
    logging.critical(f"Import error: {e}")
    logging.critical("Please check:")
    logging.critical("  1. DAP SDK compilation successful")
    logging.critical("  2. python_dap.so file exists and accessible")
    logging.critical("  3. Library paths configured correctly")
    logging.critical("TERMINATING - No fallback mode available.")
    sys.exit(1)

from .exceptions import DapException


class DapLogLevel(Enum):
    """DAP supported log levels"""
    CRITICAL = "CRITICAL"
    ERROR = "ERROR"
    WARNING = "WARNING"
    NOTICE = "NOTICE"
    INFO = "INFO"
    DEBUG = "DEBUG"


class DapLoggingError(DapException):
    """DAP Logging specific errors"""
    pass


class DapLogging:
    """
    📝 DAP Logging wrapper
    
    Direct wrapper over dap_set_log_level/get_log_level functions.
    Provides centralized logging configuration for DAP systems.
    
    Example:
        # Basic usage
        logger = DapLogging()
        logger.set_level(DapLogLevel.DEBUG)
        
        # Static usage
        DapLogging.set_log_level(DapLogLevel.INFO)
        current_level = DapLogging.get_log_level()
    """
    
    def __init__(self):
        """Initialize logging manager"""
        self._logger = logging.getLogger(__name__)
    
    @staticmethod
    def set_log_level(level: Union[DapLogLevel, str]) -> None:
        """
        Set DAP logging level
        
        Args:
            level: Log level to set (enum or string)
            
        Raises:
            DapLoggingError: If setting log level fails
        """
        try:
            if isinstance(level, DapLogLevel):
                level_str = level.value
            else:
                level_str = str(level).upper()
                # Validate level
                if level_str not in [lvl.value for lvl in DapLogLevel]:
                    raise DapLoggingError(f"Invalid log level: {level_str}")
            
            # Call C functions: dap_set_log_level() and dap_log_level_set()
            dap_set_log_level(level_str)
            dap_log_level_set(level_str)
            
            logging.getLogger(__name__).info(f"Set DAP log level to {level_str}")
            
        except Exception as e:
            raise DapLoggingError(f"Failed to set log level: {e}")
    
    @staticmethod
    def get_log_level() -> str:
        """
        Get current DAP logging level
        
        Returns:
            Current log level string
            
        Raises:
            DapLoggingError: If getting log level fails
        """
        try:
            # Call C function: dap_get_log_level()
            level = dap_get_log_level()
            return level if level else "INFO"
            
        except Exception as e:
            logging.getLogger(__name__).error(f"Failed to get log level: {e}")
            raise DapLoggingError(f"Failed to get log level: {e}")
    
    def set_level(self, level: Union[DapLogLevel, str]) -> None:
        """
        Set log level using instance method
        
        Args:
            level: Log level to set
        """
        self.set_log_level(level)
    
    def get_level(self) -> str:
        """
        Get log level using instance method
        
        Returns:
            Current log level string
        """
        return self.get_log_level()
    
    @staticmethod
    def is_level_enabled(level: Union[DapLogLevel, str]) -> bool:
        """
        Check if specific log level is enabled
        
        Args:
            level: Log level to check
            
        Returns:
            True if level is enabled
        """
        try:
            current_level = DapLogging.get_log_level()
            
            if isinstance(level, DapLogLevel):
                check_level = level.value
            else:
                check_level = str(level).upper()
            
            # Define level hierarchy
            level_hierarchy = {
                "CRITICAL": 50,
                "ERROR": 40,
                "WARNING": 30,
                "NOTICE": 25,
                "INFO": 20,
                "DEBUG": 10
            }
            
            current_priority = level_hierarchy.get(current_level, 20)
            check_priority = level_hierarchy.get(check_level, 20)
            
            return check_priority >= current_priority
            
        except Exception as e:
            logging.getLogger(__name__).error(f"Failed to check log level: {e}")
            return True  # Default to enabled
    
    @staticmethod
    def list_levels() -> list:
        """
        Get list of all available log levels
        
        Returns:
            List of log level strings
        """
        return [level.value for level in DapLogLevel]
    
    def __repr__(self) -> str:
        try:
            current_level = self.get_level()
            return f"DapLogging(level={current_level})"
        except:
            return "DapLogging(level=unknown)"


# Convenience functions
def set_debug_logging():
    """Enable debug logging"""
    DapLogging.set_log_level(DapLogLevel.DEBUG)


def set_info_logging():
    """Enable info logging"""
    DapLogging.set_log_level(DapLogLevel.INFO)


def set_error_logging():
    """Enable error logging only"""
    DapLogging.set_log_level(DapLogLevel.ERROR)


__all__ = [
    'DapLogging',
    'DapLogLevel', 
    'DapLoggingError',
    'set_debug_logging',
    'set_info_logging',
    'set_error_logging'
] 