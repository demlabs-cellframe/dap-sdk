"""
Модуль контекста DAP SDK
"""

from pathlib import Path
from typing import List


class DAPSDKContext:
    """Контекст DAP SDK проекта"""

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

    @property
    def crypto_path(self) -> Path:
        """Путь к директории криптографических модулей"""
        return self.root_path / "crypto"

    @property
    def net_path(self) -> Path:
        """Путь к директории сетевых модулей"""
        return self.root_path / "net"

    @property
    def examples_path(self) -> Path:
        """Путь к директории примеров"""
        return self.root_path / "examples"

    def get_module_path(self, module_type: str, module_name: str) -> Path:
        """Получить путь к модулю по типу и имени"""
        if module_type == "crypto":
            return self.crypto_path / "src" / module_name
        elif module_type == "net":
            if "server" in module_name:
                return self.net_path / "server" / module_name.replace("_server", "_server")
            else:
                return self.net_path / module_name
        elif module_type == "core":
            return self.root_path / "core" / module_name
        else:
            return self.root_path / module_type / module_name

    def is_valid_project(self) -> bool:
        """Проверить, является ли путь валидным проектом DAP SDK"""
        required_paths = [
            self.crypto_path,
            self.net_path,
            self.root_path / "CMakeLists.txt"
        ]
        return all(path.exists() for path in required_paths)

