#!/usr/bin/env python3
"""
Точка входа для запуска MCP сервера DAP SDK
"""

import asyncio
import sys
import os

# Добавляем текущую директорию в путь для импорта
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core.server import DAPMCPServer


async def main():
    """Главная функция"""
    # Можно передать путь к проекту как аргумент командной строки
    root_path = sys.argv[1] if len(sys.argv) > 1 else "/home/naeper/work/cellframe-node/dap-sdk"

    server = DAPMCPServer(root_path)
    await server.run()


if __name__ == "__main__":
    asyncio.run(main())

