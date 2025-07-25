"""
DAP Network Client Module

Simple client wrapper for DAP SDK network operations using C functions.
"""

import sys
import logging
from typing import Optional, Any, Callable, Dict, List
from enum import Enum

# Import DAP client functions from C module
try:
    from ..python_dap import (
        dap_client_new, dap_client_delete, dap_client_connect_to, dap_client_disconnect,
        # Stage constants
        DAP_CLIENT_STAGE_BEGIN, DAP_CLIENT_STAGE_ENC_INIT, DAP_CLIENT_STAGE_STREAM_CTL,
        DAP_CLIENT_STAGE_STREAM_SESSION, DAP_CLIENT_STAGE_STREAM_STREAMING,
        DAP_CLIENT_STAGE_DISCONNECTED, DAP_CLIENT_STAGE_ERROR, DAP_CLIENT_STAGE_ESTABLISHED
    )
except ImportError as e:
    # If C module import fails, this is a critical error
    raise ImportError(f"Cannot import DAP client functions from C module: {e}")

from ..core.exceptions import DapException

class DapClientError(DapException):
    """DAP Client specific exception"""
    pass

class DapClientStage(Enum):
    """DAP Client stages (based on C constants)"""
    BEGIN = DAP_CLIENT_STAGE_BEGIN
    ENC_INIT = DAP_CLIENT_STAGE_ENC_INIT
    STREAM_CTL = DAP_CLIENT_STAGE_STREAM_CTL
    STREAM_SESSION = DAP_CLIENT_STAGE_STREAM_SESSION
    STREAM_STREAMING = DAP_CLIENT_STAGE_STREAM_STREAMING
    DISCONNECTED = DAP_CLIENT_STAGE_DISCONNECTED
    ERROR = DAP_CLIENT_STAGE_ERROR
    ESTABLISHED = DAP_CLIENT_STAGE_ESTABLISHED

class DapClient:
    """
    DAP Network Client wrapper using C functions
    """
    
    def __init__(self):
        self._client_handle = None
        self._callbacks = {}
        self._logger = logging.getLogger(self.__class__.__name__)
        
        # Create new client using C function
        self._client_handle = dap_client_new()
        if not self._client_handle:
            raise DapClientError("Failed to create DAP client")
    
    def __del__(self):
        """Cleanup client resources"""
        if self._client_handle:
            try:
                dap_client_delete(self._client_handle)
            except:
                pass
    
    def connect_to(self, host: str, port: int, protocol: str = "tcp") -> bool:
        """
        Connect to remote address using C function
        
        Args:
            host: Target hostname or IP
            port: Target port number  
            protocol: Connection protocol (tcp/udp)
            
        Returns:
            True if connection initiated successfully
        """
        if not self._client_handle:
            raise DapClientError("Client not initialized")
        
        try:
            result = dap_client_connect_to(self._client_handle, host, port, protocol)
            if result == 0:
                self._logger.debug(f"Connection to {host}:{port} initiated")
                return True
            else:
                self._logger.error(f"Failed to connect to {host}:{port}, error: {result}")
                return False
        except Exception as e:
            raise DapClientError(f"Connection failed: {e}")
    
    def disconnect(self) -> bool:
        """
        Disconnect from remote address
        
        Returns:
            True if disconnect successful
        """
        if not self._client_handle:
            return True
            
        try:
            result = dap_client_disconnect(self._client_handle)
            if result == 0:
                self._logger.debug("Client disconnected successfully")
                return True
            else:
                self._logger.warning(f"Disconnect returned code: {result}")
                return False
        except Exception as e:
            self._logger.error(f"Disconnect error: {e}")
            return False
    
    def is_connected(self) -> bool:
        """Check if client is connected (placeholder implementation)"""
        return self._client_handle is not None
    
    def get_client_handle(self):
        """Get raw client handle for advanced usage"""
        return self._client_handle

class DapClientManager:
    """
    Simple manager for multiple DAP clients
    """
    
    def __init__(self):
        self._clients = {}
        self._logger = logging.getLogger(self.__class__.__name__)
    
    def create_client(self, client_id: str) -> DapClient:
        """Create and register new client"""
        if client_id in self._clients:
            self._logger.warning(f"Client {client_id} already exists")
            return self._clients[client_id]
        
        client = DapClient()
        self._clients[client_id] = client
        self._logger.debug(f"Created client: {client_id}")
        return client
    
    def get_client(self, client_id: str) -> Optional[DapClient]:
        """Get existing client by ID"""
        return self._clients.get(client_id)
    
    def remove_client(self, client_id: str) -> bool:
        """Remove and cleanup client"""
        if client_id in self._clients:
            client = self._clients.pop(client_id)
            del client  # Trigger cleanup
            self._logger.debug(f"Removed client: {client_id}")
            return True
        return False
    
    def get_all_clients(self) -> Dict[str, DapClient]:
        """Get all registered clients"""
        return self._clients.copy()

# Module-level convenience functions
def create_client() -> DapClient:
    """Create a new DAP client"""
    return DapClient()

def connect_to_peer(host: str, port: int, protocol: str = "tcp") -> DapClient:
    """Create client and connect to peer in one step"""
    client = DapClient()
    if client.connect_to(host, port, protocol):
        return client
    else:
        del client
        raise DapClientError(f"Failed to connect to {host}:{port}") 