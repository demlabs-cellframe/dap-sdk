"""
🔧 DAP Python SDK

Main DAP package providing crypto, network, and database functionality.
Clean API without fallbacks or mocks.
"""

__version__ = "3.0.0"

# Import all existing DAP modules
from . import common, config, core, crypto, events, global_db, network

# Re-export main classes for convenience
from .crypto import (
    DapSign, DapCert, DapSignError, DapCertError
)

from .core.exceptions import DapException

from .global_db import (
    Gdb, GdbCluster, GdbNode, GdbInstance,
    MemberRole, ClusterRole, create_cluster, connect_to_cluster
)

# Main DAP classes for export
__all__ = [
    # Core
    'DapException',
    
    # Crypto
    'DapSign',
    'DapCert', 
    'DapSignError',
    'DapCertError',
    
    # Global Database
    'Gdb',
    'GdbCluster',
    'GdbNode',
    'GdbInstance',
    'MemberRole',
    'ClusterRole',
    'create_cluster',
    'connect_to_cluster',
    
    # Modules
    'common',
    'config',
    'core',
    'crypto',
    'events',
    'global_db',
    'network'
] 