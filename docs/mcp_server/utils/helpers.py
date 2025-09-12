"""
Вспомогательные функции для MCP сервера DAP SDK
"""

import os
from pathlib import Path
from typing import List, Dict, Any


def find_files_by_extension(directory: Path, extension: str) -> List[Path]:
    """Найти все файлы с указанным расширением в директории"""
    if not directory.exists():
        return []

    return list(directory.rglob(f"*.{extension}"))


def get_file_info(file_path: Path) -> Dict[str, Any]:
    """Получить информацию о файле"""
    if not file_path.exists():
        return {}

    stat = file_path.stat()
    return {
        "name": file_path.name,
        "path": str(file_path),
        "size": stat.st_size,
        "modified": stat.st_mtime,
        "extension": file_path.suffix
    }


def count_lines_in_file(file_path: Path) -> int:
    """Посчитать количество строк в файле"""
    if not file_path.exists():
        return 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            return len(f.readlines())
    except (UnicodeDecodeError, OSError):
        return 0


def extract_includes_from_c_file(file_path: Path) -> List[str]:
    """Извлечь #include директивы из C файла"""
    includes = []

    if not file_path.exists():
        return includes

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line.startswith('#include'):
                    includes.append(line)
    except (UnicodeDecodeError, OSError):
        pass

    return includes


def get_directory_structure(directory: Path, max_depth: int = 3) -> Dict[str, Any]:
    """Получить структуру директории"""
    if not directory.exists():
        return {}

    def _walk_dir(dir_path: Path, current_depth: int) -> Dict[str, Any]:
        if current_depth > max_depth:
            return {"type": "directory", "truncated": True}

        result = {"type": "directory", "contents": {}}

        try:
            for item in sorted(dir_path.iterdir()):
                if item.is_file():
                    result["contents"][item.name] = {"type": "file", "size": item.stat().st_size}
                elif item.is_dir() and not item.name.startswith('.'):
                    result["contents"][item.name] = _walk_dir(item, current_depth + 1)
        except PermissionError:
            result["error"] = "Permission denied"

        return result

    return _walk_dir(directory, 0)


def validate_project_structure(project_root: Path) -> Dict[str, bool]:
    """Проверить структуру проекта DAP SDK"""
    required_dirs = [
        "crypto",
        "net",
        "core"
    ]

    required_files = [
        "CMakeLists.txt",
        "README.md"
    ]

    validation = {}

    # Проверяем директории
    for dir_name in required_dirs:
        dir_path = project_root / dir_name
        validation[f"dir_{dir_name}"] = dir_path.exists() and dir_path.is_dir()

    # Проверяем файлы
    for file_name in required_files:
        file_path = project_root / file_name
        validation[f"file_{file_name}"] = file_path.exists() and file_path.is_file()

    return validation

