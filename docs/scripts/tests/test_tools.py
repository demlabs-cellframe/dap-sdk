#!/usr/bin/env python3
"""
Тесты для класса DAPMCPTools
"""

import tempfile
import json
from pathlib import Path
from unittest.mock import patch, MagicMock, mock_open
import sys
import os
import pytest

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mcp_server import DAPSDKContext, DAPMCPTools


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


@pytest.fixture
def tools(context):
    """Фикстура для инструментов DAP SDK"""
    return DAPMCPTools(context)


class TestDAPMCPTools:
    """Тесты для DAPMCPTools"""

        # Создаем тестовую структуру директорий
        self._create_test_structure()

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        import shutil
        shutil.rmtree(self.temp_dir)

    def _create_test_structure(self):
        """Создание тестовой структуры файлов и директорий"""
        # Создаем crypto директорию с алгоритмами
        crypto_dir = Path(self.temp_dir) / "crypto" / "src"
        crypto_dir.mkdir(parents=True, exist_ok=True)

        # Создаем файлы для kyber
        kyber_dir = crypto_dir / "kyber"
        kyber_dir.mkdir(exist_ok=True)
        (kyber_dir / "kyber.c").write_text("#include \"kyber.h\"\n// Kyber implementation")
        (kyber_dir / "kyber.h").write_text("#ifndef KYBER_H\n#define KYBER_H\n// Kyber header")

        # Создаем falcon
        falcon_dir = crypto_dir / "falcon"
        falcon_dir.mkdir(exist_ok=True)
        (falcon_dir / "falcon.c").write_text("#include \"falcon.h\"\n// Falcon implementation")
        (falcon_dir / "falcon.h").write_text("#ifndef FALCON_H\n#define FALCON_H\n// Falcon header")

        # Создаем net директорию
        net_dir = Path(self.temp_dir) / "net"
        net_dir.mkdir(exist_ok=True)

        # Создаем серверы
        server_dir = net_dir / "server"
        server_dir.mkdir(exist_ok=True)

        http_server_dir = server_dir / "http_server"
        http_server_dir.mkdir(exist_ok=True)
        (http_server_dir / "http_server.c").write_text("#include \"http_server.h\"\n// HTTP Server")

        json_rpc_server_dir = server_dir / "json_rpc_server"
        json_rpc_server_dir.mkdir(exist_ok=True)
        (json_rpc_server_dir / "json_rpc_server.c").write_text("#include \"json_rpc_server.h\"\n// JSON-RPC Server")

        # Создаем examples директорию
        examples_dir = Path(self.temp_dir) / "examples"
        examples_dir.mkdir(exist_ok=True)

        # Создаем примеры
        hello_world = examples_dir / "hello_world.c"
        hello_world.write_text("""/*
 * Hello World example for DAP SDK
 * This is a simple example demonstrating basic usage
 */

#include <stdio.h>
#include "dap_sdk.h"

int main() {
    printf("Hello, DAP SDK!\\n");
    return 0;
}
""")

        crypto_example = examples_dir / "crypto_example.c"
        crypto_example.write_text("""// Crypto example
// Demonstrates cryptographic operations

#include "dap_crypto.h"

void crypto_demo() {
    // Crypto operations here
}
""")

        # Создаем CMakeLists.txt
        cmake_file = Path(self.temp_dir) / "CMakeLists.txt"
        cmake_content = """cmake_minimum_required(VERSION 3.10)
project(DAP_SDK)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

option(BUILD_DAP_SDK_TESTS "Build tests" ON)

add_subdirectory(crypto)
add_subdirectory(net)
"""
        cmake_file.write_text(cmake_content)

    @pytest.mark.asyncio
    async def test_analyze_crypto_algorithms(self, tools):
        """Тест анализа криптографических алгоритмов"""
        result = await self.tools.analyze_crypto_algorithms()

        # Проверяем kyber
        assert "kyber" in result
        assert result["kyber"]["files"] == 1
        assert result["kyber"]["headers"] == 1
        assert result["kyber"]["status"] == "implemented"
        assert result["kyber"]["path"].endswith("kyber")

        # Проверяем falcon
        assert "falcon" in result
        assert result["falcon"]["files"] == 1
        assert result["falcon"]["headers"] == 1
        assert result["falcon"]["status"] == "implemented"

        # Проверяем отсутствующие алгоритмы
        assert "dilithium" in result
        assert result["dilithium"]["status"] == "not_found"

    @pytest.mark.asyncio
    async def test_analyze_network_modules(self, tools):
        """Тест анализа сетевых модулей"""
        result = await self.tools.analyze_network_modules()

        # Проверяем HTTP сервер
        assert "http_server" in result
        assert result["http_server"]["type"] == "server"
        assert result["http_server"]["files"] == 1
        assert result["http_server"]["status"] == "implemented"

        # Проверяем JSON-RPC сервер
        assert "json_rpc_server" in result
        assert result["json_rpc_server"]["type"] == "server"
        assert result["json_rpc_server"]["files"] == 1
        assert result["json_rpc_server"]["status"] == "implemented"

        # Проверяем отсутствующие серверы
        assert "dns_server" in result
        assert result["dns_server"]["type"] == "server"
        assert result["dns_server"]["status"] == "not_found"

    @pytest.mark.asyncio
    async def test_analyze_build_system(self, tools):
        """Тест анализа системы сборки"""
        result = await self.tools.analyze_build_system()

        assert "cmake" in result
        assert result["cmake"]["path"].endswith("CMakeLists.txt")
        assert "Threads" in result["cmake"]["dependencies"]
        assert "PkgConfig" in result["cmake"]["dependencies"]
        assert "OpenSSL" in result["cmake"]["dependencies"]
        assert result["cmake"]["has_tests"]

    @pytest.mark.asyncio
    async def test_find_code_examples(self, tools):
        """Тест поиска примеров кода"""
        result = await self.tools.find_code_examples()

        assert len(result) == 2

        # Проверяем hello_world пример
        hello_world = next(ex for ex in result if ex["name"] == "hello_world")
        assert hello_world["language"] == "C"
        assert hello_world["path"].endswith("hello_world.c")
        assert "Hello World example" in hello_world["description"]
        assert hello_world["lines"] > 0

        # Проверяем crypto_example
        crypto_example = next(ex for ex in result if ex["name"] == "crypto_example")
        assert crypto_example["language"] == "C"
        assert crypto_example["path"].endswith("crypto_example.c")
        assert "Crypto example" in crypto_example["description"]

    def test_extract_description_with_comments(self):
        """Тест извлечения описания из комментариев"""
        content = """/*
 * This is a test function
 * It demonstrates description extraction
 * From multi-line comments
 */

#include <stdio.h>

int main() {
    return 0;
}
"""
        description = self.tools._extract_description(content)
        assert "This is a test function" in description
        assert "It demonstrates description extraction" in description

    def test_extract_description_single_line_comments(self):
        """Тест извлечения описания из однострочных комментариев"""
        content = """// This is a simple example
// Demonstrating single line comments
// Description extraction

#include <stdio.h>
"""
        description = self.tools._extract_description(content)
        assert "This is a simple example" in description
        assert "Demonstrating single line comments" in description

    def test_extract_description_no_comments(self):
        """Тест извлечения описания без комментариев"""
        content = """#include <stdio.h>

int main() {
    printf("Hello\\n");
    return 0;
}
"""
        description = self.tools._extract_description(content)
        assert description == "No description found"

    @pytest.mark.asyncio
    async def test_analyze_security_features(self, tools):
        """Тест анализа функций безопасности"""
        result = await self.tools.analyze_security_features()

        assert "post_quantum_crypto" in result
        assert "side_channel_protection" in result
        assert "memory_safety" in result

        # Проверяем пост-квантовые алгоритмы
        pq_crypto = result["post_quantum_crypto"]
        assert "kyber" in pq_crypto
        assert "falcon" in pq_crypto
        self.assertNotIn("dilithium", pq_crypto)  # Не найден в тестовой структуре

    def test_empty_crypto_directory(self):
        """Тест анализа крипто алгоритмов в пустой директории"""
        # Создаем пустую директорию (без crypto папки)
        empty_temp = tempfile.mkdtemp()
        empty_context = DAPSDKContext(empty_temp)
        empty_tools = DAPMCPTools(empty_context)

        async def run_test():
            result = await empty_tools.analyze_crypto_algorithms()
            # Если директория crypto не существует, результат должен быть пустым
            assert len(result) == 0

        import asyncio
        asyncio.run(run_test())


# Tests run via pytest
