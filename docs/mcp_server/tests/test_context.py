#!/usr/bin/env python3
"""
Тесты для класса DAPSDKContext
"""

import tempfile
from pathlib import Path
from unittest.mock import patch, MagicMock
import sys
import os
import pytest

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ..core import DAPSDKContext


@pytest.fixture
def temp_dir():
    """Фикстура для временной директории"""
    temp_dir = tempfile.mkdtemp()
    yield temp_dir
    import shutil
    shutil.rmtree(temp_dir)


@pytest.fixture
def context(temp_dir):
    """Фикстура для контекста DAP SDK"""
    return DAPSDKContext(temp_dir)


class TestDAPSDKContext:
    """Тесты для DAPSDKContext"""

    def test_initialization(self, context, temp_dir):
        """Тест инициализации контекста"""
        assert context.root_path == Path(temp_dir)
        assert isinstance(context.crypto_modules, list)
        assert isinstance(context.net_modules, list)
        assert isinstance(context.core_modules, list)

    def test_crypto_modules_content(self, context):
        """Тест содержимого списка крипто-модулей"""
        expected_crypto = [
            "kyber", "falcon", "sphincsplus", "dilithium", "bliss", "chipmunk"
        ]
        assert context.crypto_modules == expected_crypto

    def test_net_modules_content(self, context):
        """Тест содержимого списка сетевых модулей"""
        expected_net = [
            "http_server", "json_rpc_server", "dns_server",
            "encryption_server", "notification_server"
        ]
        assert context.net_modules == expected_net

    def test_core_modules_content(self, context):
        """Тест содержимого списка основных модулей"""
        expected_core = [
            "common", "platform_unix", "platform_win32", "platform_darwin"
        ]
        assert context.core_modules == expected_core

    def test_string_path_conversion(self):
        """Тест конвертации строкового пути в Path объект"""
        string_path = "/some/test/path"
        context = DAPSDKContext(string_path)
        assert context.root_path == Path(string_path)
        assert isinstance(context.root_path, Path)

    def test_relative_path_conversion(self):
        """Тест конвертации относительного пути"""
        relative_path = "relative/path"
        context = DAPSDKContext(relative_path)
        assert context.root_path == Path(relative_path)
        assert isinstance(context.root_path, Path)


# Тесты запускаются через pytest
