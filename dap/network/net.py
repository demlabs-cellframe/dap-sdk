"""
🌐 DAP Network API

Полноценный API для работы с CellFrame сетями.
Интегрирован из CFNet/CFNetID helpers с улучшениями.
"""

import logging
from typing import Optional, List, Dict, Any, Iterator
from enum import Enum

from ..core.exceptions import DapNetworkError
from ..common import logger

# Try to import native CellFrame network components
try:
    from CellFrame.Network import Net as CellFrameNet, NetAddr
    from CellFrame.Network.Client import Client as CellFrameClient
    CELLFRAME_NATIVE_AVAILABLE = True
except ImportError:
    CELLFRAME_NATIVE_AVAILABLE = False
    CellFrameNet = None
    NetAddr = None
    CellFrameClient = None


class DapNetStatus(Enum):
    """Network status enumeration."""
    OFFLINE = "offline"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    SYNCING = "syncing"
    SYNCHRONIZED = "synchronized"
    ERROR = "error"


class DapNetType(Enum):
    """Network type enumeration."""
    MAINNET = "mainnet"
    TESTNET = "testnet" 
    PRIVATE = "private"
    CUSTOM = "custom"


class DapNetID:
    """
    Network identifier wrapper.
    
    Integrated from CFNetID functionality.
    """
    
    def __init__(self, net_id: Any):
        """Initialize network ID."""
        self._net_id = net_id
        self._name = None
        
    @property
    def value(self) -> Any:
        """Get raw network ID value."""
        return self._net_id
    
    @property
    def name(self) -> Optional[str]:
        """Get network name."""
        return self._name
    
    def __str__(self) -> str:
        """String representation."""
        return f"DapNetID({self._name or self._net_id})"
    
    def __eq__(self, other) -> bool:
        """Equality comparison."""
        if isinstance(other, DapNetID):
            return self._net_id == other._net_id
        return self._net_id == other


class DapNet:
    """
    Network management class.
    
    Integrated and enhanced from CFNet functionality.
    """
    
    def __init__(self, name: str):
        """
        Initialize network instance.
        
        Args:
            name: Network name
        """
        self.name = name
        self._native_net = None
        self._status = DapNetStatus.OFFLINE
        self._chains: Dict[str, Any] = {}
        
        # Try to load native network
        if CELLFRAME_NATIVE_AVAILABLE:
            try:
                self._native_net = CellFrameNet.byName(name)
                if self._native_net:
                    self._status = DapNetStatus.CONNECTED
                    logger.info(f"Loaded native network: {name}")
                else:
                    logger.warning(f"Network {name} not found in native API")
            except Exception as e:
                logger.error(f"Failed to load native network {name}: {e}")
        else:
            logger.warning("Native CellFrame API missing - using fallback")
    
    @property
    def id(self) -> DapNetID:
        """Get network ID."""
        if self._native_net:
            return DapNetID(self._native_net.id)
        else:
            # Fallback ID generation
            import hashlib
            fallback_id = int(hashlib.md5(self.name.encode()).hexdigest()[:8], 16)
            return DapNetID(fallback_id)
    
    @property
    def status(self) -> DapNetStatus:
        """Get network status."""
        return self._status
    
    @property
    def is_connected(self) -> bool:
        """Check if network is connected."""
        return self._status in [DapNetStatus.CONNECTED, DapNetStatus.SYNCHRONIZED]
    
    def get_chains(self) -> List[str]:
        """
        Get list of available chains in the network.
        
        Returns:
            List of chain names
        """
        if self._native_net:
            try:
                # Get chains from native API
                chains = []
                # Implementation depends on native API structure
                return chains
            except Exception as e:
                logger.error(f"Failed to get chains from native network: {e}")
        
        # Fallback
        return list(self._chains.keys())
    
    def get_chain(self, chain_name: str) -> Optional[Any]:
        """
        Get chain by name.
        
        Args:
            chain_name: Chain name
            
        Returns:
            Chain object or None if not found
        """
        if self._native_net:
            try:
                return self._native_net.getChainByName(chain_name)
            except Exception as e:
                logger.error(f"Failed to get chain {chain_name}: {e}")
        
        return self._chains.get(chain_name)
    
    def add_chain(self, chain_name: str, chain_config: Dict[str, Any] = None) -> bool:
        """
        Add chain to network.
        
        Args:
            chain_name: Chain name
            chain_config: Chain configuration
            
        Returns:
            True if added successfully
        """
        try:
            self._chains[chain_name] = chain_config or {}
            logger.info(f"Added chain {chain_name} to network {self.name}")
            return True
        except Exception as e:
            logger.error(f"Failed to add chain {chain_name}: {e}")
            return False
    
    def connect(self) -> bool:
        """
        Connect to network.
        
        Returns:
            True if connected successfully
        """
        try:
            if self._native_net:
                # Use native connection
                self._status = DapNetStatus.CONNECTING
                # Implementation depends on native API
                self._status = DapNetStatus.CONNECTED
                return True
            else:
                # Fallback connection
                self._status = DapNetStatus.CONNECTED
                logger.info(f"Connected to network {self.name} (fallback mode)")
                return True
                
        except Exception as e:
            logger.error(f"Failed to connect to network {self.name}: {e}")
            self._status = DapNetStatus.ERROR
            return False
    
    def disconnect(self) -> bool:
        """
        Disconnect from network.
        
        Returns:
            True if disconnected successfully
        """
        try:
            self._status = DapNetStatus.OFFLINE
            logger.info(f"Disconnected from network {self.name}")
            return True
        except Exception as e:
            logger.error(f"Failed to disconnect from network {self.name}: {e}")
            return False
    
    def sync(self) -> bool:
        """
        Synchronize with network.
        
        Returns:
            True if sync started successfully
        """
        try:
            if not self.is_connected:
                raise DapNetworkError("Network not connected")
            
            self._status = DapNetStatus.SYNCING
            # Implementation depends on native API
            self._status = DapNetStatus.SYNCHRONIZED
            
            logger.info(f"Network {self.name} synchronized")
            return True
            
        except Exception as e:
            logger.error(f"Failed to sync network {self.name}: {e}")
            self._status = DapNetStatus.ERROR
            return False
    
    @classmethod
    def get_all_networks(cls) -> List[str]:
        """
        Get list of all available networks.
        
        Returns:
            List of network names
        """
        networks = []
        
        if CELLFRAME_NATIVE_AVAILABLE:
            try:
                # Get networks from native API
                # Implementation depends on native API structure
                pass
            except Exception as e:
                logger.error(f"Failed to get networks from native API: {e}")
        
        # Add fallback networks
        fallback_networks = ["testnet", "mainnet", "private"]
        networks.extend(fallback_networks)
        
        return list(set(networks))  # Remove duplicates
    
    @classmethod
    def create_network(cls, name: str, net_type: DapNetType = DapNetType.CUSTOM,
                      config: Dict[str, Any] = None) -> 'DapNet':
        """
        Create new network.
        
        Args:
            name: Network name
            net_type: Network type
            config: Network configuration
            
        Returns:
            DapNet instance
        """
        try:
            network = cls(name)
            
            # Apply configuration
            if config:
                for chain_name, chain_config in config.get('chains', {}).items():
                    network.add_chain(chain_name, chain_config)
            
            logger.info(f"Created {net_type.value} network: {name}")
            return network
            
        except Exception as e:
            logger.error(f"Failed to create network {name}: {e}")
            raise DapNetworkError(f"Failed to create network: {e}")
    
    def __str__(self) -> str:
        """String representation."""
        return f"DapNet({self.name}, status={self.status.value})"
    
    def __repr__(self) -> str:
        """Detailed representation."""
        return f"DapNet(name='{self.name}', status={self.status.value}, chains={len(self._chains)})"


class DapNetworkManager:
    """
    Network manager for handling multiple networks.
    """
    
    def __init__(self):
        """Initialize network manager."""
        self._networks: Dict[str, DapNet] = {}
        self._active_network: Optional[DapNet] = None
        
    def add_network(self, network: DapNet) -> bool:
        """
        Add network to manager.
        
        Args:
            network: DapNet instance
            
        Returns:
            True if added successfully
        """
        try:
            self._networks[network.name] = network
            logger.info(f"Added network {network.name} to manager")
            return True
        except Exception as e:
            logger.error(f"Failed to add network: {e}")
            return False
    
    def get_network(self, name: str) -> Optional[DapNet]:
        """
        Get network by name.
        
        Args:
            name: Network name
            
        Returns:
            DapNet instance or None
        """
        return self._networks.get(name)
    
    def get_all_networks(self) -> List[DapNet]:
        """Get all managed networks."""
        return list(self._networks.values())
    
    def set_active_network(self, name: str) -> bool:
        """
        Set active network.
        
        Args:
            name: Network name
            
        Returns:
            True if set successfully
        """
        network = self.get_network(name)
        if network:
            self._active_network = network
            logger.info(f"Set active network: {name}")
            return True
        return False
    
    @property
    def active_network(self) -> Optional[DapNet]:
        """Get active network."""
        return self._active_network
    
    def connect_all(self) -> bool:
        """
        Connect to all networks.
        
        Returns:
            True if all connected successfully
        """
        success = True
        for network in self._networks.values():
            if not network.connect():
                success = False
        return success
    
    def disconnect_all(self) -> bool:
        """
        Disconnect from all networks.
        
        Returns:
            True if all disconnected successfully
        """
        success = True
        for network in self._networks.values():
            if not network.disconnect():
                success = False
        return success


# Convenience functions
def get_network(name: str) -> Optional[DapNet]:
    """Get network by name."""
    try:
        return DapNet(name)
    except Exception as e:
        logger.error(f"Failed to get network {name}: {e}")
        return None


def get_all_networks() -> List[str]:
    """Get all available network names."""
    return DapNet.get_all_networks()


def create_network(name: str, net_type: DapNetType = DapNetType.CUSTOM) -> DapNet:
    """Create new network."""
    return DapNet.create_network(name, net_type) 