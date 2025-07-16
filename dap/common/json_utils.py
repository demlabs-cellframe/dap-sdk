"""
🔧 DAP JSON Utilities

JSON serialization/deserialization utilities.
Integrated from helpers with enhancements.
"""

import json
import logging
from typing import Any, Optional, Dict, List, Union
from datetime import datetime, date
from decimal import Decimal

from ..core.exceptions import DapException

logger = logging.getLogger(__name__)


class DapJSONError(DapException):
    """JSON operation error."""
    pass


class DateTimeEncoder(json.JSONEncoder):
    """
    Enhanced JSON encoder that handles datetime and Decimal objects.
    """
    
    def default(self, obj):
        """Encode special objects."""
        if isinstance(obj, (datetime, date)):
            return obj.isoformat()
        elif isinstance(obj, Decimal):
            return float(obj)
        elif hasattr(obj, '__dict__'):
            # Handle custom objects with __dict__
            return obj.__dict__
        return super().default(obj)


def json_dump(data: Any, indent: Optional[int] = None, ensure_ascii: bool = False) -> str:
    """
    Serialize Python object to JSON string.
    
    Enhanced version of the original helpers function.
    
    Args:
        data: The Python object to serialize
        indent: Indentation level for pretty printing
        ensure_ascii: Ensure ASCII output
        
    Returns:
        JSON formatted string
        
    Raises:
        DapJSONError: If serialization fails
    """
    try:
        return json.dumps(
            data, 
            cls=DateTimeEncoder,
            indent=indent,
            ensure_ascii=ensure_ascii
        )
    except Exception as e:
        logger.error(f"JSON serialization failed: {e}")
        raise DapJSONError(f"Failed to serialize to JSON: {e}")


def json_load(data: str, strict: bool = True) -> Any:
    """
    Deserialize JSON string to Python object.
    
    Enhanced version of the original helpers function.
    
    Args:
        data: JSON formatted string to deserialize
        strict: If False, return None on error instead of raising exception
        
    Returns:
        Deserialized Python object, or None if strict=False and error occurs
        
    Raises:
        DapJSONError: If deserialization fails and strict=True
    """
    try:
        return json.loads(data)
    except Exception as e:
        logger.error(f"JSON deserialization failed: {e}")
        if strict:
            raise DapJSONError(f"Failed to deserialize JSON: {e}")
        return None


def json_load_file(file_path: str, encoding: str = 'utf-8', strict: bool = True) -> Any:
    """
    Load JSON data from file.
    
    Args:
        file_path: Path to JSON file
        encoding: File encoding
        strict: If False, return None on error instead of raising exception
        
    Returns:
        Deserialized Python object
        
    Raises:
        DapJSONError: If file read or JSON parse fails and strict=True
    """
    try:
        with open(file_path, 'r', encoding=encoding) as f:
            return json.load(f)
    except Exception as e:
        logger.error(f"Failed to load JSON from {file_path}: {e}")
        if strict:
            raise DapJSONError(f"Failed to load JSON file: {e}")
        return None


def json_save_file(data: Any, file_path: str, encoding: str = 'utf-8', 
                   indent: int = 2, ensure_ascii: bool = False) -> bool:
    """
    Save data to JSON file.
    
    Args:
        data: Data to serialize
        file_path: Path to save file
        encoding: File encoding
        indent: Indentation for pretty printing
        ensure_ascii: Ensure ASCII output
        
    Returns:
        True if saved successfully
        
    Raises:
        DapJSONError: If save fails
    """
    try:
        with open(file_path, 'w', encoding=encoding) as f:
            json.dump(
                data, f,
                cls=DateTimeEncoder,
                indent=indent,
                ensure_ascii=ensure_ascii
            )
        return True
    except Exception as e:
        logger.error(f"Failed to save JSON to {file_path}: {e}")
        raise DapJSONError(f"Failed to save JSON file: {e}")


def json_pretty_print(data: Any, indent: int = 2) -> str:
    """
    Pretty print JSON data.
    
    Args:
        data: Data to pretty print
        indent: Indentation level
        
    Returns:
        Pretty formatted JSON string
    """
    return json_dump(data, indent=indent)


def json_validate(data: str) -> bool:
    """
    Validate JSON string.
    
    Args:
        data: JSON string to validate
        
    Returns:
        True if valid JSON
    """
    try:
        json.loads(data)
        return True
    except:
        return False


def json_merge(dict1: Dict, dict2: Dict, deep: bool = True) -> Dict:
    """
    Merge two dictionaries.
    
    Args:
        dict1: First dictionary
        dict2: Second dictionary  
        deep: Perform deep merge
        
    Returns:
        Merged dictionary
    """
    if not deep:
        result = dict1.copy()
        result.update(dict2)
        return result
    
    result = dict1.copy()
    for key, value in dict2.items():
        if key in result and isinstance(result[key], dict) and isinstance(value, dict):
            result[key] = json_merge(result[key], value, deep=True)
        else:
            result[key] = value
    return result


def json_extract_keys(data: Union[Dict, List], keys: List[str]) -> Dict:
    """
    Extract specific keys from JSON data.
    
    Args:
        data: JSON data (dict or list)
        keys: Keys to extract
        
    Returns:
        Dictionary with extracted keys
    """
    result = {}
    
    if isinstance(data, dict):
        for key in keys:
            if key in data:
                result[key] = data[key]
    elif isinstance(data, list):
        result['items'] = []
        for item in data:
            if isinstance(item, dict):
                extracted = {k: item.get(k) for k in keys if k in item}
                if extracted:
                    result['items'].append(extracted)
    
    return result


def json_flatten(data: Dict, separator: str = '.') -> Dict:
    """
    Flatten nested dictionary.
    
    Args:
        data: Dictionary to flatten
        separator: Key separator
        
    Returns:
        Flattened dictionary
    """
    def _flatten(obj, prefix=''):
        if isinstance(obj, dict):
            for key, value in obj.items():
                new_key = f"{prefix}{separator}{key}" if prefix else key
                yield from _flatten(value, new_key)
        else:
            yield prefix, obj
    
    return dict(_flatten(data))


# Convenience aliases for backward compatibility
dump_json = json_dump
load_json = json_load 