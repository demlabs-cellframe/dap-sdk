"""
🛠️ DAP Common Module

Common utilities and types for DAP operations.
Enhanced with JSON utilities from helpers integration.
"""

# Import types from external module since common/types.py doesn't exist yet
try:
    from ..core.types import DapType, DapTypeError
except ImportError:
    # Create dummy types for fallback
    class DapTypeError(Exception):
        pass
    
    class DapType:
        pass

# Import logging
try:
    from ..core.logging import logger
except ImportError:
    import logging
    logger = logging.getLogger(__name__)

# Import JSON utilities (integrated from helpers)
from .json_utils import (
    json_dump, json_load, json_load_file, json_save_file,
    json_pretty_print, json_validate, json_merge, json_extract_keys,
    json_flatten, DapJSONError, DateTimeEncoder
)

__all__ = [
    # Core types
    'DapType', 'DapTypeError',
    'logger',
    
    # JSON utilities (integrated from helpers)
    'json_dump', 'json_load', 'json_load_file', 'json_save_file',
    'json_pretty_print', 'json_validate', 'json_merge', 'json_extract_keys',
    'json_flatten', 'DapJSONError', 'DateTimeEncoder'
]

# Version info
__version__ = "3.0.0"
__description__ = "DAP Common utilities - integrated with helpers functionality"
