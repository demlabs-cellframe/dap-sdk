"""
🔍 DAP Config Parser

Класс для парсинга различных форматов конфигурации DAP SDK.
"""

import json
import configparser
from pathlib import Path
from typing import Dict, Any, Optional, Union
from ..core.exceptions import DapConfigError


class DapConfigParser:
    """
    Парсер конфигурационных файлов DAP SDK.
    
    Поддерживает различные форматы конфигурации:
    - INI файлы
    - JSON файлы  
    - Простой key=value формат
    """
    
    def __init__(self, default_format: str = 'ini'):
        """
        Инициализация парсера.
        
        Args:
            default_format: Формат по умолчанию ('ini', 'json', 'keyvalue').
        """
        self.default_format = default_format
        self._supported_formats = {'ini', 'json', 'keyvalue'}
        
    def parse(self, content: str, format_type: Optional[str] = None) -> Dict[str, Any]:
        """
        Парсинг конфигурации из строки.
        
        Args:
            content: Содержимое для парсинга.
            format_type: Тип формата. Если не указан, используется default_format.
            
        Returns:
            Словарь с распарсенными данными.
            
        Raises:
            DapConfigError: При ошибке парсинга.
        """
        fmt = format_type or self.default_format
        
        if fmt not in self._supported_formats:
            raise DapConfigError(f"Неподдерживаемый формат: {fmt}")
            
        try:
            if fmt == 'ini':
                return self._parse_ini(content)
            elif fmt == 'json':
                return self._parse_json(content)
            elif fmt == 'keyvalue':
                return self._parse_keyvalue(content)
            else:
                raise DapConfigError(f"Неизвестный формат: {fmt}")
                
        except Exception as e:
            raise DapConfigError(f"Ошибка парсинга конфигурации ({fmt}): {e}")
    
    def parse_string(self, config_string: str, format_type: Optional[str] = None) -> Dict[str, Any]:
        """
        Парсинг конфигурации из строки (алиас для parse).
        
        Args:
            config_string: Строка конфигурации.
            format_type: Тип формата.
            
        Returns:
            Словарь с распарсенными данными.
        """
        return self.parse(config_string, format_type)
    
    def parse_file(self, file_path: Union[str, Path], format_type: Optional[str] = None) -> Dict[str, Any]:
        """
        Парсинг конфигурации из файла.
        
        Args:
            file_path: Путь к файлу конфигурации.
            format_type: Тип формата. Если не указан, определяется по расширению файла.
            
        Returns:
            Словарь с распарсенными данными.
            
        Raises:
            DapConfigError: При ошибке чтения или парсинга файла.
        """
        path = Path(file_path)
        
        if not path.exists():
            raise DapConfigError(f"Файл конфигурации не найден: {path}")
            
        # Определяем формат по расширению если не указан
        if format_type is None:
            format_type = self._detect_format(path)
            
        try:
            with open(path, 'r', encoding='utf-8') as f:
                content = f.read()
            return self.parse(content, format_type)
            
        except Exception as e:
            raise DapConfigError(f"Ошибка чтения файла {path}: {e}")
    
    def get_format(self, file_path: Union[str, Path]) -> str:
        """
        Определение формата файла конфигурации.
        
        Args:
            file_path: Путь к файлу.
            
        Returns:
            Строка с типом формата.
        """
        return self._detect_format(Path(file_path))
    
    def _parse_ini(self, content: str) -> Dict[str, Any]:
        """Парсинг INI формата."""
        parser = configparser.ConfigParser()
        parser.read_string(content)
        
        result = {}
        for section_name in parser.sections():
            result[section_name] = dict(parser[section_name])
            
        # Обрабатываем DEFAULT секцию отдельно
        if parser.defaults():
            result['DEFAULT'] = dict(parser.defaults())
            
        return result
    
    def _parse_json(self, content: str) -> Dict[str, Any]:
        """Парсинг JSON формата."""
        return json.loads(content)
    
    def _parse_keyvalue(self, content: str) -> Dict[str, Any]:
        """Парсинг простого key=value формата."""
        result = {}
        
        for line in content.split('\n'):
            line = line.strip()
            
            # Пропускаем пустые строки и комментарии
            if not line or line.startswith('#') or line.startswith(';'):
                continue
                
            if '=' in line:
                key, value = line.split('=', 1)
                key = key.strip()
                value = value.strip()
                
                # Простая типизация значений
                if value.lower() in ('true', 'false'):
                    value = value.lower() == 'true'
                elif value.isdigit():
                    value = int(value)
                elif self._is_float(value):
                    value = float(value)
                    
                result[key] = value
                
        return result
    
    def _detect_format(self, file_path: Path) -> str:
        """Определение формата по расширению файла."""
        suffix = file_path.suffix.lower()
        
        if suffix in ['.json']:
            return 'json'
        elif suffix in ['.ini', '.cfg', '.conf']:
            return 'ini'
        else:
            # По умолчанию пробуем keyvalue формат
            return 'keyvalue'
    
    def _is_float(self, value: str) -> bool:
        """Проверка является ли строка числом с плавающей точкой."""
        try:
            float(value)
            return '.' in value
        except ValueError:
            return False
    
    @property
    def supported_formats(self) -> set:
        """Получение списка поддерживаемых форматов."""
        return self._supported_formats.copy()
    
    def __str__(self) -> str:
        return f"DapConfigParser(format={self.default_format})"
    
    def __repr__(self) -> str:
        return f"DapConfigParser(default_format='{self.default_format}')" 