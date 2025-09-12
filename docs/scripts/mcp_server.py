#!/usr/bin/env python3
"""
DAP SDK MCP Server - Model Context Protocol Server для DAP SDK

Этот сервер предоставляет инструменты для анализа и работы с DAP SDK
через Model Context Protocol для интеграции с AI-системами.
"""

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence
import asyncio
import logging
from dataclasses import dataclass
from enum import Enum

# MCP SDK imports
try:
    from mcp.server import Server
    from mcp.types import (
        Resource,
        Tool,
        TextContent,
        ImageContent,
        EmbeddedResource,
        LoggingLevel
    )
    import mcp.server.stdio
except ImportError:
    print("Ошибка: MCP SDK не установлен. Установите: pip install mcp")
    sys.exit(1)

# Настройка логирования
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("dap-mcp-server")

@dataclass
class DAPSDKContext:
    """Контекст DAP SDK проекта"""
    root_path: Path
    crypto_modules: List[str]
    net_modules: List[str]
    core_modules: List[str]

    def __init__(self, root_path: str):
        self.root_path = Path(root_path)
        self.crypto_modules = [
            "kyber", "falcon", "sphincsplus", "dilithium", "bliss", "chipmunk"
        ]
        self.net_modules = [
            "http_server", "json_rpc_server", "dns_server",
            "encryption_server", "notification_server"
        ]
        self.core_modules = [
            "common", "platform_unix", "platform_win32", "platform_darwin"
        ]

class DAPMCPTools:
    """Инструменты для работы с DAP SDK через MCP"""

    def __init__(self, context: DAPSDKContext):
        self.context = context

    async def analyze_crypto_algorithms(self) -> Dict[str, Any]:
        """Анализ криптографических алгоритмов DAP SDK"""
        crypto_info = {}

        crypto_path = self.context.root_path / "crypto"
        if crypto_path.exists():
            for algo in self.context.crypto_modules:
                algo_path = crypto_path / "src" / algo
                if algo_path.exists():
                    files = list(algo_path.glob("*.c"))
                    headers = list(algo_path.glob("*.h"))
                    crypto_info[algo] = {
                        "files": len(files),
                        "headers": len(headers),
                        "path": str(algo_path),
                        "status": "implemented"
                    }
                else:
                    crypto_info[algo] = {
                        "status": "not_found",
                        "path": str(algo_path)
                    }

        return crypto_info

    async def analyze_network_modules(self) -> Dict[str, Any]:
        """Анализ сетевых модулей DAP SDK"""
        net_info = {}

        net_path = self.context.root_path / "net"
        if net_path.exists():
            for module in self.context.net_modules:
                if "server" in module:
                    module_path = net_path / "server" / module.replace("_server", "_server")
                    if module_path.exists():
                        net_info[module] = {
                            "type": "server",
                            "files": len(list(module_path.glob("*.c"))),
                            "path": str(module_path),
                            "status": "implemented"
                        }
                    else:
                        net_info[module] = {
                            "type": "server",
                            "status": "not_found"
                        }
                else:
                    module_path = net_path / module
                    if module_path.exists():
                        net_info[module] = {
                            "type": "client",
                            "files": len(list(module_path.glob("*.c"))),
                            "path": str(module_path),
                            "status": "implemented"
                        }

        return net_info

    async def analyze_build_system(self) -> Dict[str, Any]:
        """Анализ системы сборки DAP SDK"""
        build_info = {}

        cmake_path = self.context.root_path / "CMakeLists.txt"
        if cmake_path.exists():
            with open(cmake_path, 'r', encoding='utf-8') as f:
                cmake_content = f.read()

            # Анализ зависимостей
            dependencies = []
            if "Threads" in cmake_content:
                dependencies.append("Threads")
            if "PkgConfig" in cmake_content:
                dependencies.append("PkgConfig")
            if "OpenSSL" in cmake_content:
                dependencies.append("OpenSSL")

            build_info["cmake"] = {
                "path": str(cmake_path),
                "dependencies": dependencies,
                "has_tests": "BUILD_DAP_SDK_TESTS" in cmake_content
            }

        return build_info

    async def find_code_examples(self) -> List[Dict[str, Any]]:
        """Поиск примеров кода в DAP SDK"""
        examples = []

        examples_path = self.context.root_path / "examples"
        if examples_path.exists():
            for example_file in examples_path.glob("*.c"):
                with open(example_file, 'r', encoding='utf-8') as f:
                    content = f.read()

                examples.append({
                    "name": example_file.stem,
                    "path": str(example_file),
                    "language": "C",
                    "description": self._extract_description(content),
                    "lines": len(content.split('\n'))
                })

        return examples

    def _extract_description(self, content: str) -> str:
        """Извлечение описания из комментариев в коде"""
        lines = content.split('\n')
        description = []

        for line in lines[:20]:  # Проверяем первые 20 строк
            if line.strip().startswith('/*') or line.strip().startswith('*'):
                description.append(line.strip('/* ').strip())
            elif line.strip().startswith('//'):
                description.append(line.strip('// ').strip())
            elif description and not line.strip():
                break

        return ' '.join(description) if description else "No description found"

    async def analyze_security_features(self) -> Dict[str, Any]:
        """Анализ функций безопасности DAP SDK"""
        security_info = {
            "post_quantum_crypto": [],
            "side_channel_protection": [],
            "memory_safety": []
        }

        # Анализ пост-квантовых алгоритмов
        crypto_path = self.context.root_path / "crypto"
        if crypto_path.exists():
            pq_algos = ["kyber", "falcon", "sphincsplus", "dilithium", "bliss"]
            for algo in pq_algos:
                algo_path = crypto_path / "src" / algo
                if algo_path.exists():
                    security_info["post_quantum_crypto"].append(algo)

        # Анализ защиты от side-channel атак
        # (Здесь можно добавить более детальный анализ)

        return security_info

class DAPMCPServer:
    """MCP сервер для DAP SDK"""

    def __init__(self):
        self.context = DAPSDKContext("/home/naeper/work/cellframe-node/dap-sdk")
        self.tools = DAPMCPTools(self.context)
        self.server = Server("dap-sdk-mcp-server")

    async def handle_list_tools(self) -> List[Tool]:
        """Список доступных инструментов"""
        return [
            Tool(
                name="analyze_crypto_algorithms",
                description="Анализ криптографических алгоритмов DAP SDK (Kyber, Falcon, SPHINCS+, etc.)",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="analyze_network_modules",
                description="Анализ сетевых модулей DAP SDK (HTTP, JSON-RPC, DNS серверы)",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="analyze_build_system",
                description="Анализ системы сборки DAP SDK (CMake, зависимости, конфигурация)",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="find_code_examples",
                description="Поиск и анализ примеров кода в DAP SDK",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="analyze_security_features",
                description="Анализ функций безопасности DAP SDK (пост-квантовые алгоритмы, защита)",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="get_project_overview",
                description="Получение общего обзора проекта DAP SDK",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            )
        ]

    async def handle_call_tool(self, name: str, arguments: Dict[str, Any]) -> List[TextContent]:
        """Обработка вызова инструмента"""
        try:
            if name == "analyze_crypto_algorithms":
                result = await self.tools.analyze_crypto_algorithms()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "analyze_network_modules":
                result = await self.tools.analyze_network_modules()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "analyze_build_system":
                result = await self.tools.analyze_build_system()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "find_code_examples":
                result = await self.tools.find_code_examples()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "analyze_security_features":
                result = await self.tools.analyze_security_features()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "get_project_overview":
                overview = {
                    "name": "DAP SDK",
                    "description": "Decentralized Application Platform Software Development Kit",
                    "focus": "Quantum-resistant cryptography and blockchain infrastructure",
                    "language": "C",
                    "key_features": [
                        "Post-quantum cryptographic algorithms",
                        "Network communication modules",
                        "Cross-platform support (Linux, macOS, Windows)",
                        "Modular architecture",
                        "High-performance implementations"
                    ],
                    "main_modules": [
                        "crypto - Cryptographic algorithms",
                        "net - Network communication",
                        "core - Core utilities and platform abstraction",
                        "io - Input/Output operations",
                        "global-db - Database management"
                    ]
                }
                return [TextContent(
                    type="text",
                    text=json.dumps(overview, indent=2, ensure_ascii=False)
                )]

            else:
                raise ValueError(f"Unknown tool: {name}")

        except Exception as e:
            logger.error(f"Error calling tool {name}: {e}")
            return [TextContent(
                type="text",
                text=f"Error: {str(e)}"
            )]

async def main():
    """Главная функция MCP сервера"""
    server = DAPMCPServer()

    # Регистрация обработчиков
    @server.server.list_tools()
    async def handle_list_tools():
        return await server.handle_list_tools()

    @server.server.call_tool()
    async def handle_call_tool(name: str, arguments: Dict[str, Any]):
        return await server.handle_call_tool(name, arguments)

    # Запуск сервера
    logger.info("Starting DAP SDK MCP Server...")
    logger.info("Available tools:")
    tools = await server.handle_list_tools()
    for tool in tools:
        logger.info(f"  - {tool.name}: {tool.description}")

    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await server.server.run(
            read_stream,
            write_stream,
            server.server.create_initialization_options()
        )

if __name__ == "__main__":
    asyncio.run(main())
