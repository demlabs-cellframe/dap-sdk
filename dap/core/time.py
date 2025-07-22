"""
⏰ DAP Time Utilities

Minimal utilities for DAP time types (dap_time_t).
Python handles all time operations, we only provide conversion to/from DAP native types.
"""

import logging
import sys
import time
from typing import Union

# Import only DAP time conversion functions we actually need - CREATE STUBS FOR MISSING
try:
    from ..python_dap import (
        py_dap_time_now as dap_time_now,
    )
    
    # Create stub for missing dap_time_to_str_rfc822
    def dap_time_to_str_rfc822(timestamp):
        """Stub for missing dap_time_to_str_rfc822 - uses Python time instead"""
        return time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(timestamp))
    
    NATIVE_TIME_AVAILABLE = True
    logging.info("✅ DAP time functions with stubs loaded successfully")
except ImportError as e:
    logging.critical("🚨 CRITICAL ERROR: python_dap missing - C bindings failed to load!")
    logging.critical("Cannot continue without native DAP SDK time bindings.")
    logging.critical(f"Import error: {e}")
    logging.critical("Time operations require native implementation.")
    logging.critical("TERMINATING - No fallback mode available.")

from .exceptions import DapException


class DapTimeError(DapException):
    """DAP Time integration specific errors"""
    pass


class DapTime:
    """
    ⏰ DAP Time utilities
    
    Minimal DAP time operations for dap_time_t ↔ Python datetime conversion.
    All time operations handled by Python, only DAP type conversion.
    
    Example:
        # Get current time in DAP format
        dap_time = DapTime()
        dap_timestamp = dap_time.now_dap()
        
        # Convert to RFC822 for DAP protocols
        rfc822 = dap_time.to_rfc822(timestamp)
    """
    
    def __init__(self):
        """Initialize time helper"""
        self._logger = logging.getLogger(__name__)
    
    def now_dap(self) -> int:
        """
        Get current timestamp in DAP format (dap_time_t compatible)
        
        Returns:
            Current timestamp as DAP time integer
        """
        try:
            # Call C function: dap_time_now() - returns dap_time_t
            dap_timestamp = dap_time_now()
            self._logger.debug(f"Got DAP timestamp: {dap_timestamp}")
            return dap_timestamp
            
        except Exception as e:
            self._logger.warning(f"DAP time failed, using Python fallback: {e}")
            return int(time.time())
    
    def to_rfc822(self, timestamp: Union[int, float]) -> str:
        """
        Convert timestamp to RFC822 format for DAP protocols
        
        Args:
            timestamp: Unix timestamp (Python time or DAP time)
            
        Returns:
            RFC822 formatted time string
        """
        try:
            # Call C function: dap_time_to_str_rfc822()
            rfc822_str = dap_time_to_str_rfc822(int(timestamp))
            self._logger.debug(f"Converted {timestamp} to RFC822: {rfc822_str}")
            return rfc822_str
            
        except Exception as e:
            self._logger.warning(f"DAP RFC822 conversion failed, using Python fallback: {e}")
            return time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(timestamp))
    
    def python_to_dap_time(self, python_timestamp: Union[int, float]) -> int:
        """
        Convert Python timestamp to DAP time format if needed
        
        Args:
            python_timestamp: Python time.time() result
            
        Returns:
            DAP-compatible timestamp
        """
        # For now, they're the same (Unix timestamp)
        # This function exists for future compatibility if DAP time format changes
        return int(python_timestamp)
    
    def dap_to_python_time(self, dap_timestamp: int) -> float:
        """
        Convert DAP time to Python timestamp if needed
        
        Args:
            dap_timestamp: DAP time value
            
        Returns:
            Python-compatible timestamp
        """
        # For now, they're the same (Unix timestamp)
        # This function exists for future compatibility if DAP time format changes
        return float(dap_timestamp)
    
    def now(self) -> int:
        """
        Get current timestamp (alias for now_dap)
        
        Returns:
            Current timestamp as integer
        """
        return self.now_dap()
    
    def to_str_rfc822(self, timestamp: Union[int, float]) -> str:
        """
        Convert timestamp to RFC822 string (alias for to_rfc822)
        
        Args:
            timestamp: Unix timestamp to convert
            
        Returns:
            RFC822 formatted time string
        """
        return self.to_rfc822(timestamp)
    
    def now_sec(self) -> float:
        """
        Get current time in seconds (Unix timestamp)
        
        Returns:
            Current time as Unix timestamp in seconds
        """
        return time.time()
    
    def now_usec(self) -> int:
        """
        Get current time in microseconds
        
        Returns:
            Current time as microseconds since Unix epoch
        """
        return int(time.time() * 1_000_000)
    
    def from_str_rfc822(self, rfc822_string: str) -> float:
        """
        Convert RFC822 formatted string to Unix timestamp
        
        Args:
            rfc822_string: RFC822 formatted time string
            
        Returns:
            Unix timestamp as float
            
        Raises:
            DapTimeError: If string cannot be parsed
        """
        try:
            # Try to parse RFC822 format
            # Example: "Wed, 21 Jan 2025 18:30:00 GMT"
            import email.utils
            parsed_time = email.utils.parsedate_tz(rfc822_string)
            
            if parsed_time is None:
                raise DapTimeError(f"Cannot parse RFC822 string: {rfc822_string}")
            
            # Convert to timestamp
            timestamp = email.utils.mktime_tz(parsed_time)
            return float(timestamp)
            
        except Exception as e:
            raise DapTimeError(f"Failed to parse RFC822 time string '{rfc822_string}': {e}")
    
    def __repr__(self) -> str:
        return "DapTime()"


# Convenience functions for quick operations
def now_dap() -> int:
    """Get current timestamp in DAP format"""
    dap_time = DapTime()
    return dap_time.now_dap()


def to_rfc822(timestamp: Union[int, float]) -> str:
    """Convert timestamp to RFC822 format for DAP protocols"""
    dap_time = DapTime()
    return dap_time.to_rfc822(timestamp)


def format_duration(seconds: Union[int, float]) -> str:
    """
    Format time duration in human readable format (pure Python)
    
    Args:
        seconds: Duration in seconds
        
    Returns:
        Human readable duration string
    """
    try:
        seconds = int(seconds)
        if seconds < 0:
            return "0s"
        
        if seconds < 60:
            return f"{seconds}s"
        elif seconds < 3600:
            minutes = seconds // 60
            remaining_seconds = seconds % 60
            if remaining_seconds == 0:
                return f"{minutes}m"
            else:
                return f"{minutes}m {remaining_seconds}s"
        elif seconds < 86400:
            hours = seconds // 3600
            remaining_minutes = (seconds % 3600) // 60
            if remaining_minutes == 0:
                return f"{hours}h"
            else:
                return f"{hours}h {remaining_minutes}m"
        else:
            days = seconds // 86400
            remaining_hours = (seconds % 86400) // 3600
            if remaining_hours == 0:
                return f"{days}d"
            else:
                return f"{days}d {remaining_hours}h"
                
    except Exception as e:
        logging.getLogger(__name__).error(f"Failed to format duration: {e}")
        return f"{seconds}s"


__all__ = [
    'DapTime',
    'DapTimeError',
    'now_dap',
    'to_rfc822',
    'format_duration'
] 