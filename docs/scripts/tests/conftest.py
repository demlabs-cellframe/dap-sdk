#!/usr/bin/env python3
"""
Конфигурация для pytest и дополнительные фикстуры
"""

import pytest
import tempfile
import asyncio
from pathlib import Path
import sys
import os

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mcp_server import DAPMCPServer, DAPSDKContext, DAPMCPTools


@pytest.fixture
def temp_dir():
    """Фикстура для временной директории"""
    temp_dir = tempfile.mkdtemp()
    yield temp_dir
    import shutil
    shutil.rmtree(temp_dir)


@pytest.fixture
def server(temp_dir):
    """Фикстура для MCP сервера"""
    server = DAPMCPServer()
    server.context = DAPSDKContext(temp_dir)
    server.tools = DAPMCPTools(server.context)
    return server


@pytest.fixture
def context(temp_dir):
    """Фикстура для контекста DAP SDK"""
    return DAPSDKContext(temp_dir)


@pytest.fixture
def tools(context):
    """Фикстура для инструментов DAP SDK"""
    return DAPMCPTools(context)


# Убираем переопределение event_loop fixture, чтобы избежать warnings


@pytest.fixture
def sample_project_structure(temp_dir):
    """Фикстура создающая пример структуры проекта"""
    # Создаем базовую структуру
    dirs = [
        "crypto/src/kyber",
        "crypto/src/falcon",
        "net/server/http_server",
        "examples",
        "docs"
    ]

    for dir_path in dirs:
        Path(temp_dir, dir_path).mkdir(parents=True, exist_ok=True)

    # Создаем примеры файлов
    files = {
        "crypto/src/kyber/kyber.c": "#include \"kyber.h\"\nvoid kyber_keygen() {}",
        "crypto/src/kyber/kyber.h": "#ifndef KYBER_H\n#define KYBER_H\n#endif",
        "examples/hello.c": "int main() { return 0; }",
        "CMakeLists.txt": "cmake_minimum_required(VERSION 3.10)\nproject(Test)"
    }

    for file_path, content in files.items():
        Path(temp_dir, file_path).write_text(content)

    return temp_dir


@pytest.fixture
def large_project_structure(temp_dir):
    """Фикстура для большой структуры проекта"""
    # Создаем много файлов для тестирования производительности
    examples_dir = Path(temp_dir, "examples")
    examples_dir.mkdir(parents=True, exist_ok=True)

    for i in range(50):
        example_file = examples_dir / f"example_{i}.c"
        content = f"// Example {i}\n#include <stdio.h>\nint main{i}() {{ return {i}; }}\n"
        example_file.write_text(content)

    crypto_dir = Path(temp_dir, "crypto/src")
    crypto_dir.mkdir(parents=True, exist_ok=True)

    for i in range(10):
        algo_dir = crypto_dir / f"algo_{i}"
        algo_dir.mkdir(exist_ok=True)
        (algo_dir / f"algo_{i}.c").write_text(f"void algo_{i}() {{}}")
        (algo_dir / f"algo_{i}.h").write_text(f"#ifndef ALGO_{i}_H\n#define ALGO_{i}_H\n#endif")

    return temp_dir


# Настройки pytest
def pytest_configure(config):
    """Конфигурация pytest"""
    config.addinivalue_line(
        "markers", "slow: marks tests as slow (deselect with '-m \"not slow\"')"
    )
    config.addinivalue_line(
        "markers", "integration: marks tests as integration tests"
    )
    config.addinivalue_line(
        "markers", "performance: marks tests as performance tests"
    )


# Плагины для асинхронных тестов
@pytest.fixture
def async_runner():
    """Runner для асинхронных тестов"""
    def run_async(coro):
        return asyncio.run(coro)
    return run_async
