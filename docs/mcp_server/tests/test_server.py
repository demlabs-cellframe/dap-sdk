#!/usr/bin/env python3
"""
Тесты для класса DAPMCPServer
"""

import unittest
import tempfile
import json
from pathlib import Path
from unittest.mock import patch, MagicMock, AsyncMock
import sys
import os
import asyncio
import pytest

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ..core import DAPMCPServer, DAPSDKContext, DAPMCPTools


class TestDAPMCPServer:
    """Тесты для DAPMCPServer"""

    @pytest.fixture(autouse=True)
    def setup_method(self):
        """Подготовка тестов"""
        self.temp_dir = tempfile.mkdtemp()
        self.server = DAPMCPServer()

        # Переопределяем путь для тестов
        self.server.context = DAPSDKContext(self.temp_dir)
        self.server.tools = DAPMCPTools(self.server.context)

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        import shutil
        shutil.rmtree(self.temp_dir)

    def test_server_initialization(self, server):
        """Тест инициализации сервера"""
        assert isinstance(self.server.context, DAPSDKContext)
        assert isinstance(self.server.tools, DAPMCPTools)
        assert self.server.server.name == "dap-sdk-mcp-server"

    @pytest.mark.asyncio
    async def test_handle_list_tools(self):
        """Тест получения списка инструментов"""
        tools = await self.server.handle_list_tools()

        assert isinstance(tools, list)
        assert len(tools) > 0

        # Проверяем наличие основных инструментов
        tool_names = [tool.name for tool in tools]
        expected_tools = [
            "analyze_crypto_algorithms",
            "analyze_network_modules",
            "analyze_build_system",
            "find_code_examples",
            "analyze_security_features",
            "get_project_overview"
        ]

        for expected_tool in expected_tools:
            assert expected_tool in tool_names

        # Проверяем структуру инструментов
        for tool in tools:
            assert tool.name is not None
            assert tool.description is not None
            assert tool.inputSchema is not None

    @pytest.mark.asyncio
    async def test_handle_call_tool_analyze_crypto_algorithms(self):
        """Тест вызова инструмента анализа крипто алгоритмов"""
        result = await self.server.handle_call_tool("analyze_crypto_algorithms", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем, что результат является валидным JSON
        data = json.loads(result[0].text)
        assert isinstance(data, dict)

    @pytest.mark.asyncio
    async def test_handle_call_tool_analyze_network_modules(self):
        """Тест вызова инструмента анализа сетевых модулей"""
        result = await self.server.handle_call_tool("analyze_network_modules", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем, что результат является валидным JSON
        data = json.loads(result[0].text)
        assert isinstance(data, dict)

    @pytest.mark.asyncio
    async def test_handle_call_tool_analyze_build_system(self):
        """Тест вызова инструмента анализа системы сборки"""
        result = await self.server.handle_call_tool("analyze_build_system", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем, что результат является валидным JSON
        data = json.loads(result[0].text)
        assert isinstance(data, dict)

    @pytest.mark.asyncio
    async def test_handle_call_tool_find_code_examples(self):
        """Тест вызова инструмента поиска примеров кода"""
        result = await self.server.handle_call_tool("find_code_examples", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем, что результат является валидным JSON
        data = json.loads(result[0].text)
        assert isinstance(data, list)

    @pytest.mark.asyncio
    async def test_handle_call_tool_analyze_security_features(self):
        """Тест вызова инструмента анализа функций безопасности"""
        result = await self.server.handle_call_tool("analyze_security_features", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем, что результат является валидным JSON
        data = json.loads(result[0].text)
        assert isinstance(data, dict)

    @pytest.mark.asyncio
    async def test_handle_call_tool_get_project_overview(self):
        """Тест вызова инструмента получения обзора проекта"""
        result = await self.server.handle_call_tool("get_project_overview", {})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

        # Проверяем содержимое обзора проекта
        data = json.loads(result[0].text)
        assert data["name"] == "DAP SDK"
        assert data["language"] == "C"
        assert "key_features" in data
        assert "main_modules" in data
        assert "Quantum-resistant cryptography" in data["focus"]

    @pytest.mark.asyncio
    async def test_handle_call_tool_unknown_tool(self):
        """Тест вызова неизвестного инструмента"""
        with self.assertRaises(ValueError) as context:
            await self.server.handle_call_tool("unknown_tool", {})

        assert "Unknown tool" in str(context.exception)

    @pytest.mark.asyncio
    async def test_handle_call_tool_with_error(self):
        """Тест обработки ошибок при вызове инструмента"""
        # Мокаем метод чтобы он выбрасывал исключение
        with patch.object(self.server.tools, 'analyze_crypto_algorithms', side_effect=Exception("Test error")):
            result = await self.server.handle_call_tool("analyze_crypto_algorithms", {})

            assert isinstance(result, list)
            assert len(result) == 1
            assert result[0].type == "text"
            assert "Error:" in result[0].text
            assert "Test error" in result[0].text

    @pytest.mark.asyncio
    async def test_handle_call_tool_with_arguments(self):
        """Тест вызова инструмента с аргументами"""
        # Все инструменты в данный момент не используют аргументы,
        # но тест проверяет что аргументы передаются корректно
        result = await self.server.handle_call_tool("get_project_overview", {"test": "value"})

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0].type == "text"

    def test_server_context_path(self, context):
        """Тест что сервер использует правильный путь к контексту"""
        # В тестах мы переопределяем контекст, поэтому проверяем что он существует
        assert self.server.context.root_path is not None
        assert str(self.server.context.root_path.startswith("/tmp"))  # временная директория

    @pytest.mark.asyncio
    async def test_all_tools_have_descriptions(self):
        """Тест что все инструменты имеют описания на русском"""
        tools = await self.server.handle_list_tools()

        for tool in tools:
            assert tool.description is not None
            assert len(tool.description) > 10
            # Проверяем что описание на русском (содержит кириллицу)
            assert any(ord(char > 127 for char in tool.description))

    @pytest.mark.asyncio
    async def test_tools_input_schema(self):
        """Тест схем входных данных инструментов"""
        tools = await self.server.handle_list_tools()

        for tool in tools:
            assert "type" in tool.inputSchema
            assert tool.inputSchema["type"] == "object"
            assert "properties" in tool.inputSchema
            assert "required" in tool.inputSchema


if __name__ == '__main__':
    # Запуск асинхронных тестов
    async def run_async_tests():
        loader = unittest.TestLoader()
        suite = loader.loadTestsFromTestCase(TestDAPMCPServer)

        # Создаем runner для асинхронных тестов
        runner = unittest.TextTestRunner(verbosity=2)

        # Запускаем тесты в asyncio
        loop = asyncio.get_event_loop()
        for test in suite:
            if hasattr(test, '_testMethodName'):
                try:
                    await test()
                    print(f"✓ {test._testMethodName}")
                except Exception as e:
                    print(f"✗ {test._testMethodName}: {e}")
            else:
                # Для обычных тестов
                try:
                    test.debug()
                    print(f"✓ {test}")
                except Exception as e:
                    print(f"✗ {test}: {e}")

    asyncio.run(run_async_tests())
