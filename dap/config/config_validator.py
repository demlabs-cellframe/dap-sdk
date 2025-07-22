"""
✅ DAP Config Validator

Класс для валидации конфигурационных данных DAP SDK.
"""

from typing import Dict, Any, List, Optional, Union, Callable
from ..core.exceptions import DapConfigError


class DapConfigValidator:
    """
    Валидатор конфигурационных данных DAP SDK.
    
    Предоставляет возможности для:
    - Валидации структуры конфигурации
    - Проверки схемы данных
    - Сбора ошибок валидации
    - Проверки корректности значений
    """
    
    def __init__(self, schema: Optional[Dict[str, Any]] = None):
        """
        Инициализация валидатора.
        
        Args:
            schema: Схема валидации. Если не указана, используется базовая валидация.
        """
        self.schema = schema or self._get_default_schema()
        self.errors = []
        self._validation_rules = {}
        self._setup_default_rules()
        
    def validate(self, config_data: Dict[str, Any]) -> bool:
        """
        Валидация конфигурационных данных.
        
        Args:
            config_data: Данные конфигурации для валидации.
            
        Returns:
            True если валидация прошла успешно.
        """
        self.errors.clear()
        
        try:
            # Проверяем структуру по схеме
            if not self.check_schema(config_data):
                return False
                
            # Применяем дополнительные правила валидации
            for rule_name, rule_func in self._validation_rules.items():
                try:
                    if not rule_func(config_data):
                        self.errors.append(f"Ошибка правила валидации: {rule_name}")
                except Exception as e:
                    self.errors.append(f"Ошибка выполнения правила {rule_name}: {e}")
                    
            return len(self.errors) == 0
            
        except Exception as e:
            self.errors.append(f"Общая ошибка валидации: {e}")
            return False
    
    def check_schema(self, config_data: Dict[str, Any]) -> bool:
        """
        Проверка соответствия данных схеме.
        
        Args:
            config_data: Данные для проверки.
            
        Returns:
            True если данные соответствуют схеме.
        """
        try:
            return self._validate_against_schema(config_data, self.schema)
        except Exception as e:
            self.errors.append(f"Ошибка проверки схемы: {e}")
            return False
    
    def get_errors(self) -> List[str]:
        """
        Получение списка ошибок валидации.
        
        Returns:
            Список строк с описанием ошибок.
        """
        return self.errors.copy()
    
    def is_valid(self, config_data: Dict[str, Any]) -> bool:
        """
        Проверка валидности конфигурации (алиас для validate).
        
        Args:
            config_data: Данные конфигурации.
            
        Returns:
            True если конфигурация валидна.
        """
        return self.validate(config_data)
    
    def add_validation_rule(self, name: str, rule_func: Callable[[Dict[str, Any]], bool]):
        """
        Добавление пользовательского правила валидации.
        
        Args:
            name: Имя правила.
            rule_func: Функция валидации, возвращающая True при успехе.
        """
        self._validation_rules[name] = rule_func
    
    def remove_validation_rule(self, name: str):
        """
        Удаление правила валидации.
        
        Args:
            name: Имя правила для удаления.
        """
        if name in self._validation_rules:
            del self._validation_rules[name]
    
    def _validate_against_schema(self, data: Dict[str, Any], schema: Dict[str, Any]) -> bool:
        """Валидация данных против схемы."""
        if not isinstance(data, dict):
            self.errors.append("Данные конфигурации должны быть словарем")
            return False
            
        # Проверяем обязательные поля
        required_fields = schema.get('required', [])
        for field in required_fields:
            if field not in data:
                self.errors.append(f"Отсутствует обязательное поле: {field}")
                
        # Проверяем типы полей
        field_types = schema.get('properties', {})
        for field_name, field_schema in field_types.items():
            if field_name in data:
                if not self._validate_field_type(data[field_name], field_schema, field_name):
                    return False
                    
        return len(self.errors) == 0
    
    def _validate_field_type(self, value: Any, field_schema: Dict[str, Any], field_name: str) -> bool:
        """Валидация типа отдельного поля."""
        expected_type = field_schema.get('type')
        
        if expected_type == 'string' and not isinstance(value, str):
            self.errors.append(f"Поле '{field_name}' должно быть строкой")
            return False
        elif expected_type == 'integer' and not isinstance(value, int):
            self.errors.append(f"Поле '{field_name}' должно быть целым числом")
            return False
        elif expected_type == 'number' and not isinstance(value, (int, float)):
            self.errors.append(f"Поле '{field_name}' должно быть числом")
            return False
        elif expected_type == 'boolean' and not isinstance(value, bool):
            self.errors.append(f"Поле '{field_name}' должно быть булевым значением")
            return False
        elif expected_type == 'array' and not isinstance(value, list):
            self.errors.append(f"Поле '{field_name}' должно быть массивом")
            return False
        elif expected_type == 'object' and not isinstance(value, dict):
            self.errors.append(f"Поле '{field_name}' должно быть объектом")
            return False
            
        # Проверяем диапазон значений если указан
        if 'min' in field_schema and isinstance(value, (int, float)):
            if value < field_schema['min']:
                self.errors.append(f"Поле '{field_name}' меньше минимального значения ({field_schema['min']})")
                return False
                
        if 'max' in field_schema and isinstance(value, (int, float)):
            if value > field_schema['max']:
                self.errors.append(f"Поле '{field_name}' больше максимального значения ({field_schema['max']})")
                return False
                
        return True
    
    def _get_default_schema(self) -> Dict[str, Any]:
        """Получение схемы по умолчанию для DAP конфигурации."""
        return {
            'type': 'object',
            'properties': {
                'log_level': {
                    'type': 'integer',
                    'min': 0,
                    'max': 4
                },
                'debug_mode': {
                    'type': 'boolean'
                },
                'config_path': {
                    'type': 'string'
                },
                'max_connections': {
                    'type': 'integer',
                    'min': 1,
                    'max': 10000
                }
            },
            'required': []
        }
    
    def _setup_default_rules(self):
        """Настройка правил валидации по умолчанию."""
        
        def validate_log_level(config_data):
            """Проверка корректности уровня логирования."""
            log_level = config_data.get('log_level')
            if log_level is not None:
                return 0 <= log_level <= 4
            return True
            
        def validate_paths(config_data):
            """Проверка корректности путей."""
            for key in ['config_path', 'data_path', 'log_path']:
                path = config_data.get(key)
                if path is not None and not isinstance(path, str):
                    return False
            return True
            
        self._validation_rules.update({
            'log_level_range': validate_log_level,
            'path_validation': validate_paths
        })
    
    def clear_errors(self):
        """Очистка списка ошибок."""
        self.errors.clear()
    
    @property
    def has_errors(self) -> bool:
        """Проверка наличия ошибок валидации."""
        return len(self.errors) > 0
    
    def __str__(self) -> str:
        return f"DapConfigValidator(errors={len(self.errors)})"
    
    def __repr__(self) -> str:
        return f"DapConfigValidator(schema={bool(self.schema)}, errors_count={len(self.errors)})" 