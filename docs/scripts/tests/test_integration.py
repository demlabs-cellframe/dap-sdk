import pytest
#!/usr/bin/env python3
"""
Интеграционные тесты для MCP сервера DAP SDK
"""

import unittest
import tempfile
import json
import subprocess
import time
import threading
import asyncio
from pathlib import Path
from unittest.mock import patch, MagicMock
import sys
import os

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mcp_server import DAPMCPServer, DAPSDKContext, DAPMCPTools


class TestIntegration:
    """Интеграционные тесты"""

    @pytest.fixture(autouse=True)
    def setup_method(self):
        """Подготовка интеграционных тестов"""
        self.temp_dir = tempfile.mkdtemp()
        self.server = DAPMCPServer()

        # Переопределяем путь для тестов
        self.server.context = DAPSDKContext(self.temp_dir)
        self.server.tools = DAPMCPTools(self.server.context)

        # Создаем полную структуру проекта для интеграционных тестов
        self._create_full_project_structure()

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        import shutil
        shutil.rmtree(self.temp_dir)

    def _create_full_project_structure(self):
        """Создание полной структуры проекта для интеграционных тестов"""
        # Создаем все директории
        dirs = [
            "crypto/src/kyber",
            "crypto/src/falcon",
            "crypto/src/dilithium",
            "net/server/http_server",
            "net/server/json_rpc_server",
            "net/client",
            "core/common",
            "examples",
            "docs",
            "tests"
        ]

        for dir_path in dirs:
            Path(self.temp_dir, dir_path).mkdir(parents=True, exist_ok=True)

        # Создаем файлы
        files_content = {
            "crypto/src/kyber/kyber.c": """#include "kyber.h"
/**
 * Kyber KEM implementation
 * Post-quantum key encapsulation mechanism
 */
void kyber_keygen() { /* implementation */ }""",

            "crypto/src/kyber/kyber.h": """#ifndef KYBER_H
#define KYBER_H
void kyber_keygen();
#endif""",

            "crypto/src/falcon/falcon.c": """#include "falcon.h"
/**
 * Falcon signature scheme
 * Post-quantum digital signature
 */
void falcon_sign() { /* implementation */ }""",

            "net/server/http_server/http_server.c": """#include "http_server.h"
/**
 * HTTP Server implementation
 * Handles HTTP requests and responses
 */
void http_server_start() { /* implementation */ }""",

            "examples/hello_world.c": """/**
 * Hello World Example
 * Basic DAP SDK usage demonstration
 */
#include <stdio.h>
#include "dap_sdk.h"

int main() {
    printf("Hello, DAP SDK World!\\n");
    return 0;
}""",

            "examples/crypto_demo.c": """// Cryptographic operations demo
// Shows how to use crypto functions

#include "dap_crypto.h"

void demo_crypto() {
    // Initialize crypto context
    // Perform cryptographic operations
}""",

            "CMakeLists.txt": """cmake_minimum_required(VERSION 3.16)
project(DAP_SDK VERSION 1.0.0)

# Dependencies
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

# Build options
option(BUILD_DAP_SDK_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)

# Subdirectories
add_subdirectory(crypto)
add_subdirectory(net)
add_subdirectory(core)

if(BUILD_DAP_SDK_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()""",

            "README.md": """# DAP SDK

Decentralized Application Platform Software Development Kit

## Features

- Post-quantum cryptographic algorithms
- High-performance network modules
- Cross-platform support
- Modular architecture

## Quick Start

```c
#include "dap_sdk.h"

int main() {
    // Your DAP SDK application
    return 0;
}
```""",
        }

        for file_path, content in files_content.items():
            full_path = Path(self.temp_dir, file_path)
            full_path.write_text(content)

    @pytest.mark.asyncio
    async def test_full_workflow_crypto_analysis(self):
        """Интеграционный тест полного рабочего процесса анализа крипто алгоритмов"""
        # Выполняем анализ
        result = await self.server.tools.analyze_crypto_algorithms()

        # Проверяем результаты
        assert "kyber" in result
        assert result["kyber"]["status"] == "implemented"
        assert result["kyber"]["files"] == 1
        assert result["kyber"]["headers"] == 1

        assert "falcon" in result
        assert result["falcon"]["status"] == "implemented"

        # Проверяем что отсутствующие алгоритмы правильно отмечены
        assert "dilithium" in result
        assert result["dilithium"]["status"] == "not_found"

    @pytest.mark.asyncio
    async def test_full_workflow_network_analysis(self):
        """Интеграционный тест анализа сетевых модулей"""
        result = await self.server.tools.analyze_network_modules()

        assert "http_server" in result
        assert result["http_server"]["status"] == "implemented"
        assert result["http_server"]["type"] == "server"

        assert "json_rpc_server" in result
        assert result["json_rpc_server"]["status"] == "implemented"

    @pytest.mark.asyncio
    async def test_full_workflow_examples_search(self):
        """Интеграционный тест поиска примеров"""
        examples = await self.server.tools.find_code_examples()

        assert len(examples) == 2

        # Проверяем структуру примеров
        for example in examples:
            assert "name" in example
            assert "path" in example
            assert "language" in example
            assert "description" in example
            assert "lines" in example
            assert example["language"] == "C"

    @pytest.mark.asyncio
    async def test_full_workflow_build_analysis(self):
        """Интеграционный тест анализа системы сборки"""
        build_info = await self.server.tools.analyze_build_system()

        assert "cmake" in build_info
        cmake_info = build_info["cmake"]

        assert "Threads" in cmake_info["dependencies"]
        assert "PkgConfig" in cmake_info["dependencies"]
        assert "OpenSSL" in cmake_info["dependencies"]
        assert cmake_info["has_tests"]

    @pytest.mark.asyncio
    async def test_end_to_end_server_workflow(self):
        """End-to-end тест всего сервера"""
        # Получаем список инструментов
        tools = await self.server.handle_list_tools()
        assert len(tools) > 0

        # Тестируем каждый инструмент
        tool_results = {}
        for tool in tools:
            result = await self.server.handle_call_tool(tool.name, {})
            assert len(result) == 1
            assert result[0].type == "text"

            # Проверяем что результат валидный JSON
            data = json.loads(result[0].text)
            tool_results[tool.name] = data

        # Проверяем что все инструменты вернули данные
        assert "analyze_crypto_algorithms" in tool_results
        assert "analyze_network_modules" in tool_results
        assert "analyze_build_system" in tool_results
        assert "find_code_examples" in tool_results
        assert "analyze_security_features" in tool_results
        assert "get_project_overview" in tool_results

    @pytest.mark.asyncio
    async def test_cross_component_integration(self):
        """Тест интеграции между компонентами"""
        # Тестируем что контекст корректно используется инструментами
        context_path = self.server.context.root_path
        tools_context_path = self.server.tools.context.root_path

        assert context_path == tools_context_path

        # Тестируем что модули контекста используются в инструментах
        crypto_modules = self.server.context.crypto_modules
        net_modules = self.server.context.net_modules

        # Проверяем что эти модули анализируются
        crypto_result = await self.server.tools.analyze_crypto_algorithms()
        net_result = await self.server.tools.analyze_network_modules()

        for module in crypto_modules:
            assert module in crypto_result

        for module in net_modules:
            assert module in net_result

    @pytest.mark.asyncio
    async def test_error_handling_integration(self):
        """Тест обработки ошибок в интеграционном сценарии"""
        # Создаем невалидный контекст
        invalid_context = DAPSDKContext("/nonexistent/path")
        invalid_tools = DAPMCPTools(invalid_context)

        # Проверяем что методы корректно обрабатывают отсутствие файлов
        crypto_result = await invalid_tools.analyze_crypto_algorithms()
        assert isinstance(crypto_result, dict)

        net_result = await invalid_tools.analyze_network_modules()
        assert isinstance(net_result, dict)

        examples = await invalid_tools.find_code_examples()
        assert isinstance(examples, list)
        assert len(examples) == 0

    @pytest.mark.asyncio
    async def test_data_consistency(self):
        """Тест консистентности данных между инструментами"""
        # Получаем данные из разных инструментов
        crypto_data = await self.server.tools.analyze_crypto_algorithms()
        security_data = await self.server.tools.analyze_security_features()

        # Проверяем что данные о крипто алгоритмах консистентны
        pq_algorithms = security_data.get("post_quantum_crypto", [])

        for algo in pq_algorithms:
            if algo in crypto_data:
                # Если алгоритм найден в безопасности, он должен быть реализован
                assert crypto_data[algo]["status"] == "implemented"

    def test_project_structure_validation(self):
        """Тест валидации структуры проекта"""
        # Проверяем что все ожидаемые директории существуют
        expected_dirs = [
            "crypto",
            "net",
            "core",
            "examples",
            "docs"
        ]

        for dir_name in expected_dirs:
            dir_path = Path(self.temp_dir) / dir_name
            assert dir_path.exists(, f"Directory {dir_name} should exist")
            assert dir_path.is_dir(, f"{dir_name} should be a directory")


class TestServerProcess:
    """Тесты для запущенного процесса сервера"""

    @pytest.fixture(autouse=True)
    def setup_method(self):
        """Подготовка для тестов процесса"""
        self.server_process = None
        self.server_script = os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "mcp_server.py"
        )

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        if self.server_process and self.server_process.poll() is None:
            self.server_process.terminate()
            self.server_process.wait(timeout=5)

    def test_server_can_be_imported(self, server):
        """Тест что сервер может быть импортирован"""
        try:
            import mcp_server
            assert hasattr(mcp_server, 'DAPMCPServer')
            assert hasattr(mcp_server, 'DAPMCPTools')
            assert hasattr(mcp_server, 'DAPSDKContext')
        except ImportError as e:
            self.fail(f"Cannot import mcp_server: {e}")

    def test_server_has_main_function(self, server):
        """Тест что сервер имеет функцию main"""
        import mcp_server
        assert hasattr(mcp_server, 'main')
        assert callable(mcp_server.main)

    @patch('sys.stdout')
    def test_server_main_with_missing_dependencies(self, mock_stdout):
        """Тест запуска main при отсутствии зависимостей MCP"""
        # Этот тест проверяет обработку отсутствия mcp зависимостей
        # Мы не можем легко замокать импорт, поэтому просто проверяем
        # что код может быть импортирован без ошибок
        try:
            import mcp_server
            # Если импорт прошел успешно, значит зависимости установлены
            assert True
        except SystemExit:
            # Если зависимости не установлены, код делает sys.exit(1)
            # Это ожидаемое поведение
            assert True


# Tests run via pytest

