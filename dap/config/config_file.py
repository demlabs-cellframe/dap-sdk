"""
📁 DAP Config File Management

Класс для работы с файлами конфигурации DAP SDK.
"""

import os
import shutil
from pathlib import Path
from typing import Optional, Dict, Any
from ..core.exceptions import DapConfigError


class DapConfigFile:
    """
    Управление файлами конфигурации DAP SDK.
    
    Предоставляет функциональность для чтения, записи,
    резервного копирования и проверки существования файлов конфигурации.
    """
    
    def __init__(self, config_path: Optional[str] = None):
        """
        Инициализация файла конфигурации.
        
        Args:
            config_path: Путь к файлу конфигурации. Если не указан, 
                        используется системный путь DAP.
        """
        self.config_path = config_path
        self._config_data = {}
        
    def read(self, file_path: Optional[str] = None) -> Dict[str, Any]:
        """
        Чтение файла конфигурации.
        
        Args:
            file_path: Путь к файлу. Если не указан, используется self.config_path.
            
        Returns:
            Словарь с данными конфигурации.
            
        Raises:
            DapConfigError: При ошибке чтения файла.
        """
        path = file_path or self.config_path
        if not path:
            raise DapConfigError("Не указан путь к файлу конфигурации")
            
        try:
            if not self.exists(path):
                return {}
                
            # Простая реализация - в продакшене можно расширить
            # для поддержки различных форматов (JSON, YAML, INI)
            with open(path, 'r', encoding='utf-8') as f:
                content = f.read()
                
            # Базовая поддержка INI-подобного формата
            config_data = {}
            current_section = 'default'
            
            for line in content.split('\n'):
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                    
                if line.startswith('[') and line.endswith(']'):
                    current_section = line[1:-1]
                    config_data[current_section] = {}
                elif '=' in line:
                    key, value = line.split('=', 1)
                    if current_section not in config_data:
                        config_data[current_section] = {}
                    config_data[current_section][key.strip()] = value.strip()
                    
            self._config_data = config_data
            return config_data
            
        except Exception as e:
            raise DapConfigError(f"Ошибка чтения файла конфигурации: {e}")
    
    def write(self, config_data: Dict[str, Any], file_path: Optional[str] = None) -> bool:
        """
        Запись данных в файл конфигурации.
        
        Args:
            config_data: Данные для записи.
            file_path: Путь к файлу. Если не указан, используется self.config_path.
            
        Returns:
            True при успешной записи.
            
        Raises:
            DapConfigError: При ошибке записи файла.
        """
        path = file_path or self.config_path
        if not path:
            raise DapConfigError("Не указан путь к файлу конфигурации")
            
        try:
            # Создаем директорию если не существует
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            
            # Простой INI-подобный формат
            content_lines = []
            for section, section_data in config_data.items():
                if section != 'default':
                    content_lines.append(f"[{section}]")
                    
                if isinstance(section_data, dict):
                    for key, value in section_data.items():
                        content_lines.append(f"{key}={value}")
                else:
                    # Для простых значений в default секции
                    if section == 'default':
                        content_lines.append(f"{section}={section_data}")
                        
                content_lines.append("")  # Пустая строка между секциями
                
            with open(path, 'w', encoding='utf-8') as f:
                f.write('\n'.join(content_lines))
                
            self._config_data = config_data
            return True
            
        except Exception as e:
            raise DapConfigError(f"Ошибка записи файла конфигурации: {e}")
    
    def exists(self, file_path: Optional[str] = None) -> bool:
        """
        Проверка существования файла конфигурации.
        
        Args:
            file_path: Путь к файлу. Если не указан, используется self.config_path.
            
        Returns:
            True если файл существует.
        """
        path = file_path or self.config_path
        if not path:
            return False
            
        return Path(path).exists()
    
    def backup(self, backup_suffix: str = '.backup') -> bool:
        """
        Создание резервной копии файла конфигурации.
        
        Args:
            backup_suffix: Суффикс для файла резервной копии.
            
        Returns:
            True при успешном создании резервной копии.
            
        Raises:
            DapConfigError: При ошибке создания резервной копии.
        """
        if not self.config_path:
            raise DapConfigError("Не указан путь к файлу конфигурации")
            
        if not self.exists():
            raise DapConfigError("Файл конфигурации не существует")
            
        try:
            backup_path = self.config_path + backup_suffix
            shutil.copy2(self.config_path, backup_path)
            return True
            
        except Exception as e:
            raise DapConfigError(f"Ошибка создания резервной копии: {e}")
    
    @property
    def data(self) -> Dict[str, Any]:
        """Получение текущих данных конфигурации."""
        return self._config_data.copy()
    
    def __str__(self) -> str:
        return f"DapConfigFile(path={self.config_path})"
    
    def __repr__(self) -> str:
        return f"DapConfigFile(config_path='{self.config_path}')" 