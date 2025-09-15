"""
Основной MCP сервер для DAP SDK
"""

import json
import logging
from typing import Any, Dict, List

# MCP SDK imports
try:
    from mcp.server import Server
    from mcp.types import Tool, TextContent
    import mcp.server.stdio
except ImportError:
    print("Ошибка: MCP SDK не установлен. Установите: pip install mcp")
    exit(1)

from .context import DAPSDKContext
from .tools import DAPMCPTools

# Настройка логирования
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("dap-mcp-server")


class DAPMCPServer:
    """MCP сервер для DAP SDK"""

    def __init__(self, root_path: str = "/home/naeper/work/cellframe-node/dap-sdk"):
        self.context = DAPSDKContext(root_path)
        self.tools = DAPMCPTools(self.context)
        self.server = Server("dap-sdk-mcp-server")

    async def handle_list_tools(self) -> List[Tool]:
        """Список доступных инструментов"""
        return [
            Tool(
                name="search_documentation",
                description="Поиск по документации DAP SDK и CellFrame SDK",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "description": "Поисковый запрос (функция, модуль, концепция)"
                        }
                    },
                    "required": ["query"]
                }
            ),
            Tool(
                name="get_coding_style_guide",
                description="Получение руководства по стилю кодирования DAP SDK",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="get_module_structure_info",
                description="Информация о структуре и назначении модулей DAP SDK и CellFrame SDK",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="search_functions_and_apis",
                description="Поиск функций и API по запросу с рекомендациями по использованию",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "description": "Поиск функций или API (например: 'hash', 'wallet', 'crypto')"
                        }
                    },
                    "required": ["query"]
                }
            ),
            Tool(
                name="find_code_examples",
                description="Поиск примеров кода по запросу",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "query": {
                            "type": "string",
                            "description": "Тип примера или функциональность"
                        }
                    },
                    "required": ["query"]
                }
            ),
            Tool(
                name="get_project_overview",
                description="Получение общего обзора проектов DAP SDK и CellFrame SDK",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="show_sdk_architecture",
                description="Показать архитектуру DAP SDK и CellFrame SDK с описанием слоев и компонентов",
                inputSchema={
                    "type": "object",
                    "properties": {},
                    "required": []
                }
            ),
            Tool(
                name="get_build_and_integration_help",
                description="Помощь по сборке, установке, интеграции и использованию DAP SDK",
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
            if name == "search_documentation":
                query = arguments.get("query", "")
                result = await self.tools.search_documentation(query)
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "get_coding_style_guide":
                result = await self.tools.get_coding_style_guide()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "get_module_structure_info":
                result = await self.tools.get_module_structure_info()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "search_functions_and_apis":
                query = arguments.get("query", "")
                result = await self.tools.search_functions_and_apis(query)
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "find_code_examples":
                query = arguments.get("query", "")
                result = await self.tools.find_code_examples(query)
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "get_project_overview":
                overview = {
                    "projects": {
                        "dap_sdk": {
                            "name": "DAP SDK",
                            "description": "Decentralized Application Platform Software Development Kit",
                            "focus": "Quantum-resistant cryptography and low-level infrastructure",
                            "language": "C",
                            "path": "/home/naeper/work/cellframe-node/dap-sdk",
                            "key_features": [
                                "Post-quantum cryptographic algorithms (Falcon, SPHINCS+)",
                                "Network communication modules",
                                "Cross-platform support (Linux, macOS, Windows)",
                                "Modular architecture",
                                "High-performance implementations"
                            ],
                            "main_modules": [
                                "crypto - Криптографические алгоритмы (Falcon, SPHINCS+, хеширование)",
                                "net - Сетевая коммуникация (HTTP сервер, клиенты)",
                                "core - Основные утилиты и платформа",
                                "io - Ввод/вывод данных",
                                "global-db - Глобальная база данных"
                            ]
                        },
                        "cellframe_sdk": {
                            "name": "CellFrame SDK", 
                            "description": "Blockchain development framework built on DAP SDK",
                            "focus": "Blockchain applications and decentralized services",
                            "language": "C",
                            "path": "/home/naeper/work/cellframe-node/cellframe-sdk",
                            "key_features": [
                                "Blockchain chains and ledgers",
                                "Wallet and balance management", 
                                "Consensus algorithms (PoA, PoS, PoW)",
                                "Decentralized services (staking, exchange)",
                                "P2P networking"
                            ],
                            "main_modules": [
                                "chain - Блокчейн цепочки, блоки, транзакции",
                                "wallet - Управление кошельками и балансами",
                                "net - P2P сеть блокчейна",
                                "service - Децентрализованные сервисы (app, stake, xchange)",
                                "consensus - Алгоритмы консенсуса",
                                "common - Общие структуры данных"
                            ]
                        }
                    },
                    "relationship": "CellFrame SDK построен на основе DAP SDK. DAP SDK предоставляет низкоуровневые инструменты, CellFrame SDK - блокчейн функциональность.",
                    "development_approach": "Используйте DAP SDK для базовой криптографии и сети, CellFrame SDK для блокчейн приложений"
                }
                return [TextContent(
                    type="text",
                    text=json.dumps(overview, indent=2, ensure_ascii=False)
                )]

            elif name == "show_sdk_architecture":
                result = await self.tools.show_sdk_architecture()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            elif name == "get_build_and_integration_help":
                result = await self.tools.get_build_and_integration_help()
                return [TextContent(
                    type="text",
                    text=json.dumps(result, indent=2, ensure_ascii=False)
                )]

            else:
                raise ValueError(f"Unknown tool: {name}")

        except Exception as e:
            logger.error(f"Error calling tool {name}: {e}")
            return [TextContent(
                type="text",
                text=f"Error: {str(e)}"
            )]

    async def run(self):
        """Запуск сервера"""
        logger.info("Starting DAP SDK MCP Server...")
        logger.info("Available tools:")
        tools = await self.handle_list_tools()
        for tool in tools:
            logger.info(f"  - {tool.name}: {tool.description}")

        # Регистрируем обработчики
        @self.server.list_tools()
        async def handle_list_tools():
            return await self.handle_list_tools()

        @self.server.call_tool()
        async def handle_call_tool(name: str, arguments: dict):
            return await self.handle_call_tool(name, arguments)

        async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
            await self.server.run(
                read_stream,
                write_stream,
                self.server.create_initialization_options()
            )
