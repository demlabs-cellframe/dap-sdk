"""
🗃️ DAP Global Database API

Global database functionality.
Clean API without fallbacks or mocks.
"""

import logging
from collections.abc import MutableMapping
from enum import Enum
from typing import Any, Callable, Iterator, Optional, List, Tuple, Dict

from ..core.exceptions import DapException
from ..common import logger


class GdbError(DapException):
    """Global database operation error."""
    pass


class MemberRole(Enum):
    """Member role enumeration."""
    INVALID = "invalid"
    NOBODY = "nobody"
    GUEST = "guest"
    USER = "user"
    ROOT = "root"
    DEFAULT = "default"


class ClusterRole(Enum):
    """Cluster role enumeration.""" 
    INVALID = "invalid"
    EMBEDDED = "embedded"
    AUTONOMIC = "autonomic"
    ISOLATED = "isolated"
    VIRTUAL = "virtual"


class GdbNode:
    """
    Global database node.
    """
    
    def __init__(self, address: str, port: int = 8089):
        """
        Initialize GDB node.
        
        Args:
            address: Node address
            port: Node port
        """
        self.address = address
        self.port = port
        self._connected = False
        
    def connect(self) -> bool:
        """Connect to node."""
        try:
            from DAP.GlobalDB import connect_to_node
            result = connect_to_node(self.address, self.port)
            self._connected = result
            return result
        except ImportError:
            raise GdbError("Native GlobalDB implementation missing")
        except Exception as e:
            raise GdbError(f"Failed to connect to node: {e}")
    
    def disconnect(self):
        """Disconnect from node."""
        try:
            from DAP.GlobalDB import disconnect_from_node
            disconnect_from_node(self.address, self.port)
            self._connected = False
        except ImportError:
            raise GdbError("Native GlobalDB implementation missing")
        except Exception as e:
            raise GdbError(f"Failed to disconnect from node: {e}")
    
    @property
    def is_connected(self) -> bool:
        """Check if connected to node."""
        return self._connected


class GdbInstance:
    """
    Global database instance.
    """
    
    def __init__(self, instance_handle: Any):
        """
        Initialize GDB instance.
        
        Args:
            instance_handle: Native instance handle
        """
        self._instance_handle = instance_handle
        
    def get(self, key: str) -> Any:
        """Get value by key."""
        try:
            return self._instance_handle.get(key)
        except Exception as e:
            raise GdbError(f"Failed to get key {key}: {e}")
    
    def set(self, key: str, value: Any) -> bool:
        """Set value by key."""
        try:
            self._instance_handle[key] = value
            return True
        except Exception as e:
            raise GdbError(f"Failed to set key {key}: {e}")
    
    def delete(self, key: str) -> bool:
        """Delete key."""
        try:
            del self._instance_handle[key]
            return True
        except Exception as e:
            raise GdbError(f"Failed to delete key {key}: {e}")
    
    def keys(self) -> List[str]:
        """Get all keys."""
        try:
            return list(self._instance_handle.keys())
        except Exception as e:
            raise GdbError(f"Failed to get keys: {e}")
    
    def values(self) -> List[Any]:
        """Get all values."""
        try:
            return list(self._instance_handle.values())
        except Exception as e:
            raise GdbError(f"Failed to get values: {e}")
    
    def items(self) -> List[Tuple[str, Any]]:
        """Get all items."""
        try:
            return list(self._instance_handle.items())
        except Exception as e:
            raise GdbError(f"Failed to get items: {e}")


class GdbCluster:
    """
    Global database cluster management.
    """
    
    def __init__(self, cluster_handle: Any):
        """
        Initialize GDB cluster.
        
        Args:
            cluster_handle: Native cluster handle
        """
        self._cluster_handle = cluster_handle
        self._nodes: List[GdbNode] = []
        self._instances: Dict[str, GdbInstance] = {}
        
    def add_node(self, address: str, port: int = 8089) -> GdbNode:
        """Add node to cluster."""
        try:
            node = GdbNode(address, port)
            self._cluster_handle.add_node(address, port)
            self._nodes.append(node)
            return node
        except Exception as e:
            raise GdbError(f"Failed to add node: {e}")
    
    def remove_node(self, address: str, port: int = 8089) -> bool:
        """Remove node from cluster."""
        try:
            self._cluster_handle.remove_node(address, port)
            for i, node in enumerate(self._nodes):
                if node.address == address and node.port == port:
                    self._nodes.pop(i)
                    return True
            return False
        except Exception as e:
            raise GdbError(f"Failed to remove node: {e}")
    
    def get_nodes(self) -> List[GdbNode]:
        """Get all cluster nodes."""
        return self._nodes.copy()
    
    def connect_all_nodes(self) -> bool:
        """Connect to all cluster nodes."""
        success = True
        for node in self._nodes:
            if not node.connect():
                success = False
        return success
    
    def disconnect_all_nodes(self):
        """Disconnect from all cluster nodes."""
        for node in self._nodes:
            node.disconnect()
    
    def create_instance(self, name: str) -> GdbInstance:
        """Create new database instance."""
        try:
            instance_handle = self._cluster_handle.create_instance(name)
            instance = GdbInstance(instance_handle)
            self._instances[name] = instance
            return instance
        except Exception as e:
            raise GdbError(f"Failed to create instance: {e}")
    
    def get_instance(self, name: str) -> Optional[GdbInstance]:
        """Get database instance by name."""
        return self._instances.get(name)
    
    def delete_instance(self, name: str) -> bool:
        """Delete database instance."""
        try:
            if name in self._instances:
                self._cluster_handle.delete_instance(name)
                del self._instances[name]
                return True
            return False
        except Exception as e:
            raise GdbError(f"Failed to delete instance: {e}")
    
    def get_instances(self) -> Dict[str, GdbInstance]:
        """Get all instances."""
        return self._instances.copy()


class Gdb(MutableMapping):
    """
    Main Global Database interface.
    """
    
    def __init__(self):
        """Initialize global database."""
        self._cluster: Optional[GdbCluster] = None
        self._active_instance: Optional[GdbInstance] = None
        
    def set_cluster(self, cluster: GdbCluster):
        """Set active cluster."""
        self._cluster = cluster
    
    def set_instance(self, instance_name: str):
        """Set active instance."""
        if self._cluster:
            instance = self._cluster.get_instance(instance_name)
            if instance:
                self._active_instance = instance
                return
        raise GdbError(f"Instance {instance_name} not found")
    
    def __getitem__(self, key: str) -> Any:
        """Get item by key."""
        if not self._active_instance:
            raise GdbError("No active instance set")
        return self._active_instance.get(key)
    
    def __setitem__(self, key: str, value: Any):
        """Set item by key."""
        if not self._active_instance:
            raise GdbError("No active instance set")
        self._active_instance.set(key, value)
    
    def __delitem__(self, key: str):
        """Delete item by key."""
        if not self._active_instance:
            raise GdbError("No active instance set")
        self._active_instance.delete(key)
    
    def __iter__(self) -> Iterator[str]:
        """Iterate over keys."""
        if not self._active_instance:
            raise GdbError("No active instance set")
        return iter(self._active_instance.keys())
    
    def __len__(self) -> int:
        """Get number of items."""
        if not self._active_instance:
            raise GdbError("No active instance set")
        return len(self._active_instance.keys())
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get value with default."""
        try:
            return self[key]
        except (KeyError, GdbError):
            return default
    
    def sync(self) -> bool:
        """Synchronize with cluster."""
        try:
            if self._cluster:
                self._cluster._cluster_handle.sync()
                return True
            return False
        except Exception as e:
            raise GdbError(f"Failed to sync: {e}")


# Convenience functions
def create_cluster() -> GdbCluster:
    """Create new GDB cluster."""
    try:
        from DAP.GlobalDB import create_cluster_native
        cluster_handle = create_cluster_native()
        return GdbCluster(cluster_handle)
    except ImportError:
        raise GdbError("Native GlobalDB implementation missing")
    except Exception as e:
        raise GdbError(f"Failed to create cluster: {e}")


def connect_to_cluster(nodes: List[Tuple[str, int]]) -> GdbCluster:
    """Connect to GDB cluster."""
    cluster = create_cluster()
    for address, port in nodes:
        cluster.add_node(address, port)
    cluster.connect_all_nodes()
    return cluster


# Global instance
_global_gdb = Gdb()


def get_global_db() -> Gdb:
    """Get global database instance."""
    return _global_gdb 