"""
DAP SDK MCP Server - Model Context Protocol Server для DAP SDK

Этот пакет предоставляет инструменты для анализа и работы с DAP SDK
через Model Context Protocol для интеграции с AI-системами.
"""

__version__ = "1.0.0"
__author__ = "DAP SDK Team"

from .core.server import DAPMCPServer
from .core.context import DAPSDKContext
from .core.tools import DAPMCPTools

__all__ = [
    'DAPMCPServer',
    'DAPSDKContext',
    'DAPMCPTools'
]
