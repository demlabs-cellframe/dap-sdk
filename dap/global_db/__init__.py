"""
🗃️ DAP Global Database Module

Clean global database operations for DAP.
"""

from .gdb import (
    GdbCluster, Gdb, GdbInstance, 
    MemberRole, ClusterRole, GdbNode,
    create_cluster, connect_to_cluster
)

__all__ = [
    'GdbCluster', 'Gdb', 'GdbInstance',
    'MemberRole', 'ClusterRole', 'GdbNode', 
    'create_cluster', 'connect_to_cluster'
]

# Version info
__version__ = "2.0.0"
__description__ = "DAP Global Database" 