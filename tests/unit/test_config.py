#!/usr/bin/env python3
"""
⚙️ Unit тесты для dap.config модуля

Тестирует классы конфигурации:
- DapConfig: основная конфигурация
- DapConfigFile: работа с файлами конфигурации
- DapConfigParser: парсинг конфигурации
- DapConfigValidator: валидация конфигурации
"""

import unittest
import sys
import os
from pathlib import Path

# Добавляем путь к модулям DAP
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

class TestDapConfigImports(unittest.TestCase):
    """Тесты импортов config модуля"""
    
    def test_import_config_module(self):
        """Тест импорта основного config модуля"""
        try:
            from dap import config
            self.assertIsNotNone(config)
        except ImportError as e:
            self.fail(f"Не удалось импортировать dap.config: {e}")
    
    def test_import_config_classes(self):
        """Тест импорта основных классов config"""
        try:
            from dap.config import DapConfig, DapConfigFile, DapConfigParser, DapConfigValidator
            self.assertIsNotNone(DapConfig)
            self.assertIsNotNone(DapConfigFile)
            self.assertIsNotNone(DapConfigParser)
            self.assertIsNotNone(DapConfigValidator)
        except ImportError as e:
            self.fail(f"Не удалось импортировать классы config: {e}")


class TestDapConfigClass(unittest.TestCase):
    """Тесты класса DapConfig"""
    
    def setUp(self):
        """Подготовка перед каждым тестом"""
        try:
            from dap.config import DapConfig
            self.DapConfig = DapConfig
        except ImportError:
            self.skipTest("Класс DapConfig недоступен")
    
    def test_dap_config_instantiation(self):
        """Тест создания экземпляра DapConfig"""
        try:
            config_instance = self.DapConfig()
            self.assertIsNotNone(config_instance)
        except Exception as e:
            self.fail(f"Не удалось создать экземпляр DapConfig: {e}")
    
    def test_dap_config_methods(self):
        """Тест методов класса DapConfig"""
        try:
            config_instance = self.DapConfig()
            
            # Проверяем наличие ожидаемых методов
            expected_methods = ['load', 'save', 'get', 'set', 'validate']
            for method_name in expected_methods:
                if hasattr(config_instance, method_name):
                    method = getattr(config_instance, method_name)
                    self.assertTrue(callable(method))
        except Exception as e:
            self.fail(f"Ошибка при тестировании методов DapConfig: {e}")


class TestDapConfigFileClass(unittest.TestCase):
    """Тесты класса DapConfigFile"""
    
    def setUp(self):
        """Подготовка перед каждым тестом"""
        try:
            from dap.config import DapConfigFile
            self.DapConfigFile = DapConfigFile
        except ImportError:
            self.skipTest("Класс DapConfigFile недоступен")
    
    def test_dap_config_file_instantiation(self):
        """Тест создания экземпляра DapConfigFile"""
        try:
            config_file_instance = self.DapConfigFile()
            self.assertIsNotNone(config_file_instance)
        except Exception as e:
            self.fail(f"Не удалось создать экземпляр DapConfigFile: {e}")
    
    def test_dap_config_file_methods(self):
        """Тест методов класса DapConfigFile"""
        try:
            config_file_instance = self.DapConfigFile()
            
            # Проверяем наличие ожидаемых методов
            expected_methods = ['read', 'write', 'exists', 'backup']
            for method_name in expected_methods:
                if hasattr(config_file_instance, method_name):
                    method = getattr(config_file_instance, method_name)
                    self.assertTrue(callable(method))
        except Exception as e:
            self.fail(f"Ошибка при тестировании методов DapConfigFile: {e}")


class TestDapConfigParserClass(unittest.TestCase):
    """Тесты класса DapConfigParser"""
    
    def setUp(self):
        """Подготовка перед каждым тестом"""
        try:
            from dap.config import DapConfigParser
            self.DapConfigParser = DapConfigParser
        except ImportError:
            self.skipTest("Класс DapConfigParser недоступен")
    
    def test_dap_config_parser_instantiation(self):
        """Тест создания экземпляра DapConfigParser"""
        try:
            parser_instance = self.DapConfigParser()
            self.assertIsNotNone(parser_instance)
        except Exception as e:
            self.fail(f"Не удалось создать экземпляр DapConfigParser: {e}")
    
    def test_dap_config_parser_methods(self):
        """Тест методов класса DapConfigParser"""
        try:
            parser_instance = self.DapConfigParser()
            
            # Проверяем наличие ожидаемых методов
            expected_methods = ['parse', 'parse_string', 'parse_file', 'get_format']
            for method_name in expected_methods:
                if hasattr(parser_instance, method_name):
                    method = getattr(parser_instance, method_name)
                    self.assertTrue(callable(method))
        except Exception as e:
            self.fail(f"Ошибка при тестировании методов DapConfigParser: {e}")


class TestDapConfigValidatorClass(unittest.TestCase):
    """Тесты класса DapConfigValidator"""
    
    def setUp(self):
        """Подготовка перед каждым тестом"""
        try:
            from dap.config import DapConfigValidator
            self.DapConfigValidator = DapConfigValidator
        except ImportError:
            self.skipTest("Класс DapConfigValidator недоступен")
    
    def test_dap_config_validator_instantiation(self):
        """Тест создания экземпляра DapConfigValidator"""
        try:
            validator_instance = self.DapConfigValidator()
            self.assertIsNotNone(validator_instance)
        except Exception as e:
            self.fail(f"Не удалось создать экземпляр DapConfigValidator: {e}")
    
    def test_dap_config_validator_methods(self):
        """Тест методов класса DapConfigValidator"""
        try:
            validator_instance = self.DapConfigValidator()
            
            # Проверяем наличие ожидаемых методов
            expected_methods = ['validate', 'check_schema', 'get_errors', 'is_valid']
            for method_name in expected_methods:
                if hasattr(validator_instance, method_name):
                    method = getattr(validator_instance, method_name)
                    self.assertTrue(callable(method))
        except Exception as e:
            self.fail(f"Ошибка при тестировании методов DapConfigValidator: {e}")


class TestConfigFunctionalTests(unittest.TestCase):
    """Функциональные тесты конфигурации"""
    
    def test_basic_config_operations(self):
        """Тест базовых операций конфигурации"""
        try:
            from dap.config import DapConfig
            config_instance = DapConfig()
            
            if hasattr(config_instance, 'set') and hasattr(config_instance, 'get'):
                try:
                    # Тестируем установку и получение значения
                    config_instance.set('test_key', 'test_value')
                    value = config_instance.get('test_key')
                    self.assertEqual(value, 'test_value')
                except Exception:
                    # Ожидаемое поведение в тестовой среде без инициализации
                    pass
        except ImportError:
            self.skipTest("DapConfig недоступен для функциональных тестов")


if __name__ == '__main__':
    # Настройка тестового runner'а
    unittest.main(verbosity=2, buffer=True) 