"""
⚙️ DAP System Utilities

Minimal utilities for DAP system functions.
Python handles all system operations, we only provide DAP-specific system calls.
"""

import logging
import subprocess
from typing import Optional

# Import DAP system functions
from python_dap import (
    py_exec_with_ret_multistring,
    dap_malloc, dap_free, dap_calloc, dap_realloc
)

from .exceptions import DapException


class DapSystemError(DapException):
    """DAP System integration specific errors"""
    pass


class DapSystem:
    """
    ⚙️ DAP System utilities
    
    Minimal DAP-specific system operations.
    Python handles regular system calls, this provides DAP SDK integration.
    
    Example:
        # Use DAP-specific command execution when needed
        dap_system = DapSystem()
        result = dap_system.execute_dap_command("dap_specific_command")
    """
    
    def __init__(self):
        """Initialize system helper"""
        self._logger = logging.getLogger(__name__)
    
    def execute_dap_command(self, command: str) -> str:
        """
        Execute command through DAP's exec_with_ret_multistring
        
        This is only for DAP-specific commands that need to go through
        the DAP execution environment. For regular system commands,
        use Python's subprocess module directly.
        
        Args:
            command: Command to execute through DAP
            
        Returns:
            Command output as string
            
        Raises:
            DapSystemError: If command execution fails
        """
        if not command or not command.strip():
            raise DapSystemError("Empty command provided")
        
        # Call C function: exec_with_ret_multistring()
        result = py_exec_with_ret_multistring(command)
        
        self._logger.debug(f"Executed DAP command: {command}")
        return result if result else ""
    
    def exec_with_ret_multistring(self, command: str) -> str:
        """
        Execute command and return result as multistring (alias for execute_dap_command)
        
        Args:
            command: Command to execute
            
        Returns:
            Command output as string
        """
        return self.execute_dap_command(command)
    
    def validate_command(self, command: str) -> bool:
        """
        Basic command validation for safety
        
        Args:
            command: Command to validate
            
        Returns:
            True if command appears safe
        """
        if not command or not command.strip():
            return False
        
        # Basic validation - could be extended
        dangerous_patterns = [
            'rm -rf /', 'del /f /q', 'format ', 'mkfs.', 'dd if=', 'shutdown', 'reboot'
        ]
        
        command_lower = command.lower()
        for pattern in dangerous_patterns:
            if pattern in command_lower:
                return False
        
        return True
    
    def malloc(self, size: int) -> int:
        """
        Allocate memory through DAP memory manager
        
        Args:
            size: Size in bytes to allocate
            
        Returns:
            Pointer as integer (for Python compatibility)
            
        Raises:
            DapSystemError: If allocation fails
        """
        if size <= 0:
            raise DapSystemError("Invalid size for memory allocation")
        
        # Call C function: dap_malloc()
        ptr = dap_malloc(size)
        if ptr is None or ptr == 0:
            raise DapSystemError(f"Failed to allocate {size} bytes")
        
        self._logger.debug(f"Allocated {size} bytes at {ptr}")
        return int(ptr)
    
    def free(self, ptr: int) -> None:
        """
        Free memory allocated by DAP memory manager
        
        Args:
            ptr: Pointer to free (as integer)
            
        Raises:
            DapSystemError: If free fails
        """
        if ptr <= 0:
            raise DapSystemError("Invalid pointer for memory free")
        
        # Call C function: dap_free()
        dap_free(ptr)
        self._logger.debug(f"Freed memory at {ptr}")
    
    def calloc(self, num: int, size: int) -> int:
        """
        Allocate zero-initialized memory through DAP memory manager
        
        Args:
            num: Number of elements
            size: Size per element in bytes
            
        Returns:
            Pointer as integer (for Python compatibility)
            
        Raises:
            DapSystemError: If allocation fails
        """
        if num <= 0 or size <= 0:
            raise DapSystemError("Invalid parameters for calloc")
        
        # Call C function: dap_calloc()
        ptr = dap_calloc(num, size)
        if ptr is None or ptr == 0:
            raise DapSystemError(f"Failed to calloc {num}*{size} bytes")
        
        self._logger.debug(f"Allocated {num}*{size} zeroed bytes at {ptr}")
        return int(ptr)
    
    def realloc(self, ptr: int, size: int) -> int:
        """
        Reallocate memory through DAP memory manager
        
        Args:
            ptr: Existing pointer (as integer)
            size: New size in bytes
            
        Returns:
            New pointer as integer
            
        Raises:
            DapSystemError: If reallocation fails
        """
        if size <= 0:
            raise DapSystemError("Invalid size for realloc")
        
        # Call C function: dap_realloc()
        new_ptr = dap_realloc(ptr, size)
        if new_ptr is None or new_ptr == 0:
            raise DapSystemError(f"Failed to realloc to {size} bytes")
        
        self._logger.debug(f"Reallocated from {ptr} to {new_ptr} ({size} bytes)")
        return int(new_ptr)
    
    def __repr__(self) -> str:
        return "DapSystem()"


# Convenience function for DAP commands
def execute_dap_command(command: str) -> str:
    """Execute command through DAP system (for DAP-specific commands only)"""
    dap_system = DapSystem()
    return dap_system.execute_dap_command(command)





__all__ = [
    'DapSystem',
    'DapSystemError',
    'execute_dap_command'
] 