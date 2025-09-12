#!/usr/bin/env python3
"""
Демон для запуска MCP сервера DAP SDK в фоновом режиме
"""

import asyncio
import sys
import os
import signal
import logging
from pathlib import Path

# Добавляем текущую директорию в путь для импорта
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core.server import DAPMCPServer

# Настройка логирования для демона
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('mcp_server_daemon.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger("dap-mcp-daemon")

class MCPServerDaemon:
    """Демон для MCP сервера"""
    
    def __init__(self, root_path: str):
        self.root_path = root_path
        self.server = None
        self.running = False
        
    def signal_handler(self, signum, frame):
        """Обработчик сигналов для корректного завершения"""
        logger.info(f"Получен сигнал {signum}, завершение работы...")
        self.running = False
        
    async def run_server(self):
        """Запуск сервера с обработкой ошибок"""
        try:
            self.server = DAPMCPServer(self.root_path)
            logger.info(f"Запуск MCP сервера для проекта: {self.root_path}")
            await self.server.run()
        except Exception as e:
            logger.error(f"Ошибка при запуске сервера: {e}")
            raise
            
    async def main_loop(self):
        """Основной цикл демона"""
        # Регистрируем обработчики сигналов
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        self.running = True
        
        while self.running:
            try:
                logger.info("Запуск MCP сервера...")
                await self.run_server()
            except KeyboardInterrupt:
                logger.info("Получен сигнал прерывания")
                break
            except Exception as e:
                logger.error(f"Ошибка в работе сервера: {e}")
                if self.running:
                    logger.info("Перезапуск через 5 секунд...")
                    await asyncio.sleep(5)
                else:
                    break
                    
        logger.info("Демон завершен")

async def main():
    """Главная функция демона"""
    # Получаем путь к проекту из аргументов командной строки
    root_path = sys.argv[1] if len(sys.argv) > 1 else "/home/naeper/work/cellframe-node/dap-sdk"
    
    daemon = MCPServerDaemon(root_path)
    await daemon.main_loop()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Демон остановлен пользователем")
    except Exception as e:
        logger.error(f"Критическая ошибка демона: {e}")
        sys.exit(1)

