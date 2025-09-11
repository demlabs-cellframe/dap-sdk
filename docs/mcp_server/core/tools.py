"""
Модуль инструментов для поиска по документации DAP SDK и CellFrame SDK
"""

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Optional

from .context import DAPSDKContext


class DAPMCPTools:
    """Инструменты для поиска по документации DAP SDK и CellFrame SDK"""

    def __init__(self, context: DAPSDKContext):
        self.context = context
        self.cellframe_sdk_path = self.context.root_path.parent / "cellframe-sdk"

    async def search_documentation(self, query: str) -> Dict[str, Any]:
        """Поиск по документации DAP SDK и CellFrame SDK"""
        results = {
            "query": query,
            "dap_sdk_results": [],
            "cellframe_sdk_results": [],
            "suggested_modules": [],
            "code_examples": [],
            "api_references": []
        }

        # Поиск в документации DAP SDK
        dap_docs_path = self.context.root_path / "docs"
        if dap_docs_path.exists():
            results["dap_sdk_results"] = await self._search_in_directory(dap_docs_path, query)

        # Поиск в документации CellFrame SDK
        cellframe_docs_path = self.cellframe_sdk_path / "docs"
        if cellframe_docs_path.exists():
            results["cellframe_sdk_results"] = await self._search_in_directory(cellframe_docs_path, query)

        # Генерация рекомендаций
        results["suggested_modules"] = self._suggest_modules_for_query(query)
        results["code_examples"] = await self._find_relevant_examples(query)
        results["api_references"] = await self._find_api_references(query)

        return results

    async def get_coding_style_guide(self) -> Dict[str, Any]:
        """Получение руководства по стилю кодирования DAP SDK"""
        style_guide = {
            "naming_conventions": {
                "functions": {
                    "pattern": "dap_module_function_name",
                    "examples": ["dap_chain_new", "dap_wallet_create", "dap_hash_fast"],
                    "description": "Все функции начинаются с dap_, затем модуль, затем действие"
                },
                "structures": {
                    "pattern": "dap_module_name_t",
                    "examples": ["dap_chain_t", "dap_wallet_t", "dap_hash_t"],
                    "description": "Структуры заканчиваются на _t и следуют паттерну модуля"
                },
                "constants": {
                    "pattern": "DAP_MODULE_CONSTANT_NAME",
                    "examples": ["DAP_CHAIN_NET_ID_0x1", "DAP_WALLET_TYPE_HD"],
                    "description": "Константы в UPPER_CASE с префиксом DAP_"
                }
            },
            "include_rules": {
                "rule": "НИКОГДА НЕ ПРОПИСЫВАЙ ПУТИ В INCLUDE - ВСЕГДА ПРОПИСЫВАЙ ПУТИ ДО ИНКЛУДОВ В CMAKE",
                "correct": [
                    "#include <dap_common.h>",
                    "#include <dap_chain.h>",
                    "#include \"local_header.h\""
                ],
                "incorrect": [
                    "#include \"../../../module/core/include/dap_common.h\"",
                    "#include \"../../crypto/include/dap_hash.h\""
                ],
                "cmake_setup": "include_directories(${CMAKE_SOURCE_DIR}/module/core/include)"
            },
            "memory_management": {
                "rules": [
                    "Всегда освобождать память через соответствующие _free функции",
                    "Проверять результаты malloc/calloc на NULL",
                    "Использовать DAP_DELETE для безопасного удаления"
                ],
                "patterns": [
                    "dap_chain_t* chain = dap_chain_new(); // создание",
                    "dap_chain_free(chain); // освобождение",
                    "DAP_DELETE(ptr); // безопасное удаление"
                ]
            },
            "error_handling": {
                "return_codes": "Использовать стандартные коды возврата: 0 - успех, отрицательные - ошибки",
                "logging": "Использовать dap_log_* для логирования",
                "patterns": [
                    "if (!result) { log_it(L_ERROR, \"Operation failed\"); return -1; }"
                ]
            }
        }
        
        return style_guide

    async def get_module_structure_info(self) -> Dict[str, Any]:
        """Информация о структуре и назначении модулей"""
        modules_info = {
            "dap_sdk": {
                "core": {
                    "path": "dap-sdk/core/",
                    "purpose": "Основные утилиты и платформо-специфичные реализации",
                    "key_files": ["dap_common.h", "dap_strfuncs.h", "dap_time.h"],
                    "when_to_use": "Базовые операции, работа с строками, время, платформа"
                },
                "crypto": {
                    "path": "dap-sdk/crypto/",
                    "purpose": "Криптографические операции и пост-квантовые алгоритмы",
                    "key_files": ["dap_enc.h", "dap_sign.h", "dap_hash.h"],
                    "when_to_use": "Подписи, хеширование, шифрование, генерация ключей",
                    "algorithms": ["Falcon", "SPHINCS+", "SHA3", "Blake2b"]
                },
                "net": {
                    "path": "dap-sdk/net/",
                    "purpose": "Сетевая коммуникация и серверы",
                    "key_files": ["dap_http_server.h", "dap_client.h"],
                    "when_to_use": "HTTP API, сетевые запросы, серверы"
                },
                "io": {
                    "path": "dap-sdk/io/",
                    "purpose": "Ввод/вывод данных и файловые операции",
                    "key_files": ["dap_file_utils.h", "dap_stream.h"],
                    "when_to_use": "Работа с файлами, потоками данных"
                },
                "global_db": {
                    "path": "dap-sdk/global-db/",
                    "purpose": "Глобальная база данных",
                    "key_files": ["dap_global_db.h"],
                    "when_to_use": "Хранение глобальных данных, кеширование"
                }
            },
            "cellframe_sdk": {
                "chain": {
                    "path": "cellframe-sdk/modules/chain/",
                    "purpose": "Блокчейн цепочки, блоки, транзакции",
                    "key_files": ["dap_chain.h", "dap_chain_ledger.h"],
                    "when_to_use": "Создание блокчейнов, работа с транзакциями"
                },
                "wallet": {
                    "path": "cellframe-sdk/modules/wallet/",
                    "purpose": "Управление кошельками и балансами",
                    "key_files": ["dap_chain_wallet.h"],
                    "when_to_use": "Создание кошельков, проверка балансов, переводы"
                },
                "net": {
                    "path": "cellframe-sdk/modules/net/",
                    "purpose": "Сетевая инфраструктура блокчейна",
                    "key_files": ["dap_chain_net.h", "dap_stream_ch.h"],
                    "when_to_use": "P2P сеть, синхронизация блокчейна"
                },
                "service": {
                    "path": "cellframe-sdk/modules/service/",
                    "purpose": "Сервисы блокчейна (app, stake, xchange)",
                    "key_files": ["dap_chain_net_srv.h"],
                    "when_to_use": "Создание dApps, стейкинг, обмен токенами"
                },
                "consensus": {
                    "path": "cellframe-sdk/modules/consensus/",
                    "purpose": "Алгоритмы консенсуса",
                    "key_files": ["dap_chain_cs.h"],
                    "when_to_use": "Настройка консенсуса для сети"
                }
            }
        }
        
        return modules_info

    async def search_functions_and_apis(self, query: str) -> Dict[str, Any]:
        """Поиск функций и API по запросу"""
        results = {
            "query": query,
            "found_functions": [],
            "found_structures": [],
            "usage_examples": [],
            "recommended_approach": ""
        }
        
        # Поиск в заголовочных файлах
        header_files = []
        
        # DAP SDK headers
        dap_headers = list(self.context.root_path.rglob("*.h"))
        # CellFrame SDK headers  
        cellframe_headers = list(self.cellframe_sdk_path.rglob("*.h"))
        
        header_files.extend(dap_headers[:50])  # Ограничиваем для производительности
        header_files.extend(cellframe_headers[:50])
        
        query_lower = query.lower()
        
        for header_file in header_files:
            try:
                with open(header_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                    
                if query_lower in content.lower():
                    # Ищем функции
                    functions = self._find_functions_in_header(content, query_lower)
                    structures = self._find_structures_in_header(content, query_lower)
                    
                    if functions:
                        results["found_functions"].extend([
                            {**func, "header": str(header_file.name), "sdk": self._get_sdk_name(header_file)}
                            for func in functions
                        ])
                        
                    if structures:
                        results["found_structures"].extend([
                            {**struct, "header": str(header_file.name), "sdk": self._get_sdk_name(header_file)}
                            for struct in structures
                        ])
                        
            except (UnicodeDecodeError, OSError):
                continue
        
        # Генерируем рекомендации
        results["recommended_approach"] = self._generate_usage_recommendation(query, results)
        
        return results

    def _find_functions_in_header(self, content: str, query: str) -> List[Dict[str, str]]:
        """Находит функции в заголовочном файле"""
        functions = []
        
        # Паттерн для функций DAP SDK
        function_patterns = [
            r'(\w+\s+)?(\w*' + re.escape(query) + r'\w*)\s*\([^)]*\)\s*;',
            r'(\w+\s+)?(dap_\w*' + re.escape(query) + r'\w*)\s*\([^)]*\)\s*;'
        ]
        
        for pattern in function_patterns:
            matches = re.finditer(pattern, content, re.IGNORECASE)
            for match in matches:
                function_name = match.group(2) if match.group(2) else match.group(1)
                if function_name:
                    functions.append({
                        "name": function_name,
                        "signature": match.group(0).strip(),
                        "description": self._get_function_description(content, match.start())
                    })
                    
        return functions

    def _find_structures_in_header(self, content: str, query: str) -> List[Dict[str, str]]:
        """Находит структуры в заголовочном файле"""
        structures = []
        
        # Ищем typedef struct с упоминанием query
        struct_pattern = r'typedef struct[^{]*\{[^}]*' + re.escape(query) + r'[^}]*\}\s*(\w+)\s*;'
        matches = re.finditer(struct_pattern, content, re.IGNORECASE | re.DOTALL)
        
        for match in matches:
            struct_name = match.group(1)
            structures.append({
                "name": struct_name,
                "definition": match.group(0).strip(),
                "description": self._get_struct_description(content, match.start())
            })
            
        return structures

    def _get_sdk_name(self, file_path: Path) -> str:
        """Определяет к какому SDK относится файл"""
        if "dap-sdk" in str(file_path):
            return "DAP SDK"
        elif "cellframe-sdk" in str(file_path):
            return "CellFrame SDK"
        else:
            return "Unknown SDK"

    def _generate_usage_recommendation(self, query: str, search_results: Dict) -> str:
        """Генерирует рекомендации по использованию"""
        recommendations = []
        
        if search_results["found_functions"]:
            func_count = len(search_results["found_functions"])
            recommendations.append(f"Найдено {func_count} функций для '{query}'. Рекомендуется изучить их сигнатуры.")
            
        if search_results["found_structures"]:
            struct_count = len(search_results["found_structures"])
            recommendations.append(f"Найдено {struct_count} структур. Изучите их поля для понимания API.")
            
        # Добавляем общие рекомендации
        query_lower = query.lower()
        if "crypto" in query_lower:
            recommendations.append("Для криптографии используйте модуль dap-sdk/crypto/. Начните с dap_enc.h для шифрования.")
        elif "chain" in query_lower:
            recommendations.append("Для блокчейна используйте cellframe-sdk/modules/chain/. Начните с dap_chain.h.")
        elif "wallet" in query_lower:
            recommendations.append("Для кошельков используйте cellframe-sdk/modules/wallet/. Начните с dap_chain_wallet.h.")
        elif "net" in query_lower:
            recommendations.append("Для сети есть два модуля: dap-sdk/net/ (базовая сеть) и cellframe-sdk/modules/net/ (блокчейн сеть).")
            
        return " ".join(recommendations) if recommendations else "Изучите найденные результаты для выбора подходящего подхода."

    async def analyze_network_modules(self) -> Dict[str, Any]:
        """Анализ сетевых модулей DAP SDK"""
        net_info = {}

        net_path = self.context.net_path
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

    async def find_code_examples(self, query: str = "") -> List[Dict[str, Any]]:
        """Поиск примеров кода в DAP SDK и CellFrame SDK"""
        if query:
            return await self._find_relevant_examples(query)
        else:
            # Если запрос пустой, возвращаем все примеры
            examples = []

            # DAP SDK примеры
            examples_path = self.context.examples_path
            if examples_path.exists():
                for example_file in examples_path.glob("*.c"):
                    with open(example_file, 'r', encoding='utf-8') as f:
                        content = f.read()

                    examples.append({
                        "name": example_file.stem,
                        "path": str(example_file),
                        "sdk": "DAP SDK",
                        "language": "C",
                        "description": self._extract_description(content),
                        "lines": len(content.split('\n'))
                    })

            # CellFrame SDK примеры
            cellframe_examples = self.cellframe_sdk_path / "docs" / "examples"
            if cellframe_examples.exists():
                for example_file in cellframe_examples.rglob("*.c"):
                    try:
                        with open(example_file, 'r', encoding='utf-8') as f:
                            content = f.read()

                        examples.append({
                            "name": example_file.stem,
                            "path": str(example_file),
                            "sdk": "CellFrame SDK",
                            "language": "C",
                            "description": self._extract_description(content),
                            "lines": len(content.split('\n'))
                        })
                    except (UnicodeDecodeError, OSError):
                        continue

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
        crypto_path = self.context.crypto_path
        if crypto_path.exists():
            pq_algos = ["kyber", "falcon", "sphincsplus", "dilithium", "bliss"]
            for algo in pq_algos:
                algo_path = crypto_path / "src" / algo
                if algo_path.exists():
                    security_info["post_quantum_crypto"].append(algo)

        # Анализ защиты от side-channel атак
        # (Здесь можно добавить более детальный анализ)

        return security_info

    async def _search_in_directory(self, directory: Path, query: str) -> List[Dict[str, Any]]:
        """Поиск в директории документации"""
        results = []
        query_lower = query.lower()
        
        for file_path in directory.rglob("*.md"):
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    
                if query_lower in content.lower():
                    # Находим контекст вокруг найденного текста
                    matches = self._find_context_matches(content, query_lower)
                    
                    results.append({
                        "file": str(file_path.relative_to(directory)),
                        "full_path": str(file_path),
                        "matches": matches,
                        "relevance_score": len(matches)
                    })
            except (UnicodeDecodeError, OSError):
                continue
                
        return sorted(results, key=lambda x: x["relevance_score"], reverse=True)

    def _find_context_matches(self, content: str, query: str) -> List[Dict[str, str]]:
        """Находит контекст вокруг найденных совпадений"""
        lines = content.split('\n')
        matches = []
        
        for i, line in enumerate(lines):
            if query in line.lower():
                # Берем контекст: 2 строки до и после
                start = max(0, i - 2)
                end = min(len(lines), i + 3)
                context_lines = lines[start:end]
                
                matches.append({
                    "line_number": i + 1,
                    "matched_line": line.strip(),
                    "context": "\n".join(context_lines)
                })
                
        return matches

    def _suggest_modules_for_query(self, query: str) -> List[Dict[str, str]]:
        """Предлагает модули на основе запроса"""
        query_lower = query.lower()
        suggestions = []
        
        # Криптография
        crypto_keywords = ["crypto", "sign", "hash", "encrypt", "decrypt", "key", "криптография", "подпись", "шифрование"]
        if any(keyword in query_lower for keyword in crypto_keywords):
            suggestions.append({
                "module": "crypto",
                "path": "dap-sdk/crypto/",
                "description": "Криптографические алгоритмы: Falcon, SPHINCS+, хеширование",
                "key_functions": ["dap_enc_*", "dap_sign_*", "dap_hash_*"],
                "usage": "Для подписей, шифрования, генерации ключей"
            })
            
        # Сеть
        net_keywords = ["net", "http", "server", "client", "socket", "сеть", "сервер", "клиент"]
        if any(keyword in query_lower for keyword in net_keywords):
            suggestions.append({
                "module": "net",
                "path": "dap-sdk/net/",
                "description": "Сетевые модули: HTTP сервер, клиенты",
                "key_functions": ["dap_http_*", "dap_client_*"],
                "usage": "Для сетевой коммуникации, HTTP API"
            })
            
        # Блокчейн
        chain_keywords = ["chain", "block", "transaction", "ledger", "блокчейн", "блок", "транзакция", "цепь"]
        if any(keyword in query_lower for keyword in chain_keywords):
            suggestions.append({
                "module": "chain",
                "path": "cellframe-sdk/modules/chain/",
                "description": "Блокчейн функциональность: цепи, блоки, транзакции",
                "key_functions": ["dap_chain_*", "dap_ledger_*"],
                "usage": "Для работы с блокчейн данными"
            })
            
        # Кошелек
        wallet_keywords = ["wallet", "balance", "address", "кошелек", "баланс", "адрес"]
        if any(keyword in query_lower for keyword in wallet_keywords):
            suggestions.append({
                "module": "wallet", 
                "path": "cellframe-sdk/modules/wallet/",
                "description": "Управление кошельками и балансами",
                "key_functions": ["dap_chain_wallet_*", "dap_ledger_balance_*"],
                "usage": "Для управления криптовалютными кошельками"
            })
            
        # База данных
        db_keywords = ["database", "db", "storage", "store", "база", "данные", "хранение"]
        if any(keyword in query_lower for keyword in db_keywords):
            suggestions.append({
                "module": "global-db",
                "path": "dap-sdk/global-db/",
                "description": "Система управления базой данных",
                "key_functions": ["dap_db_*", "dap_global_db_*"],
                "usage": "Для хранения и управления данными"
            })
            
        return suggestions

    async def _find_relevant_examples(self, query: str) -> List[Dict[str, Any]]:
        """Находит релевантные примеры кода"""
        examples = []
        query_lower = query.lower()
        
        # Поиск в примерах DAP SDK
        dap_examples = self.context.root_path / "examples"
        if dap_examples.exists():
            examples.extend(await self._search_examples_in_directory(dap_examples, query_lower, "DAP SDK"))
            
        # Поиск в примерах CellFrame SDK  
        cellframe_examples = self.cellframe_sdk_path / "docs" / "examples"
        if cellframe_examples.exists():
            examples.extend(await self._search_examples_in_directory(cellframe_examples, query_lower, "CellFrame SDK"))
            
        return examples

    async def _search_examples_in_directory(self, directory: Path, query: str, sdk_name: str) -> List[Dict[str, Any]]:
        """Поиск примеров в директории"""
        examples = []
        
        for file_path in directory.rglob("*.c"):
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    
                if query in content.lower():
                    examples.append({
                        "name": file_path.stem,
                        "path": str(file_path),
                        "sdk": sdk_name,
                        "description": self._extract_description(content),
                        "lines": len(content.split('\n')),
                        "relevance": content.lower().count(query)
                    })
            except (UnicodeDecodeError, OSError):
                continue
                
        return examples

    async def _find_api_references(self, query: str) -> List[Dict[str, Any]]:
        """Находит API референсы"""
        api_refs = []
        query_lower = query.lower()
        
        # Поиск в API документации
        api_paths = [
            self.context.root_path / "docs" / "api",
            self.cellframe_sdk_path / "docs" / "api"
        ]
        
        for api_path in api_paths:
            if api_path.exists():
                for file_path in api_path.rglob("*.md"):
                    try:
                        with open(file_path, 'r', encoding='utf-8') as f:
                            content = f.read()
                            
                        # Ищем функции, структуры, константы
                        functions = self._extract_functions(content, query_lower)
                        structures = self._extract_structures(content, query_lower)
                        
                        if functions or structures:
                            sdk_name = "DAP SDK" if "dap-sdk" in str(file_path) else "CellFrame SDK"
                            api_refs.append({
                                "file": str(file_path.name),
                                "sdk": sdk_name,
                                "functions": functions,
                                "structures": structures,
                                "path": str(file_path)
                            })
                    except (UnicodeDecodeError, OSError):
                        continue
                        
        return api_refs

    def _extract_functions(self, content: str, query: str) -> List[Dict[str, str]]:
        """Извлекает функции из документации"""
        functions = []
        
        # Ищем определения функций в стиле C
        function_pattern = r'(\w+\s+)?(\w+)\s*\([^)]*\)\s*;?'
        matches = re.finditer(function_pattern, content)
        
        for match in matches:
            function_line = match.group(0)
            if query in function_line.lower():
                functions.append({
                    "signature": function_line.strip(),
                    "description": self._get_function_description(content, match.start())
                })
                
        return functions

    def _extract_structures(self, content: str, query: str) -> List[Dict[str, str]]:
        """Извлекает структуры из документации"""
        structures = []
        
        # Ищем определения структур
        struct_pattern = r'typedef struct \w* \{[^}]*\} \w+;'
        matches = re.finditer(struct_pattern, content, re.MULTILINE | re.DOTALL)
        
        for match in matches:
            struct_content = match.group(0)
            if query in struct_content.lower():
                struct_name = re.search(r'\} (\w+);', struct_content)
                if struct_name:
                    structures.append({
                        "name": struct_name.group(1),
                        "definition": struct_content.strip(),
                        "description": self._get_struct_description(content, match.start())
                    })
                    
        return structures

    def _get_function_description(self, content: str, position: int) -> str:
        """Получает описание функции из комментариев"""
        lines = content[:position].split('\n')
        description_lines = []
        
        # Ищем комментарии перед функцией
        for line in reversed(lines[-5:]):
            line = line.strip()
            if line.startswith('//') or line.startswith('*') or line.startswith('/*'):
                description_lines.insert(0, line.strip('/*/ '))
            elif description_lines and not line:
                continue
            elif description_lines:
                break
                
        return ' '.join(description_lines) if description_lines else "Описание не найдено"

    def _get_struct_description(self, content: str, position: int) -> str:
        """Получает описание структуры"""
        return self._get_function_description(content, position)

    async def show_sdk_architecture(self) -> Dict[str, Any]:
        """Показать архитектуру DAP SDK и CellFrame SDK"""
        architecture = {
            "dap_sdk_architecture": {
                "description": "DAP SDK - низкоуровневая платформа для децентрализованных приложений",
                "layers": {
                    "core_layer": {
                        "description": "Основной слой - базовые утилиты и платформа",
                        "components": [
                            "dap_common - Общие структуры и макросы",
                            "dap_strfuncs - Функции для работы со строками", 
                            "dap_time - Функции времени",
                            "dap_config - Система конфигурации",
                            "dap_events - Система событий"
                        ],
                        "purpose": "Предоставляет базовую функциональность для всех остальных модулей"
                    },
                    "crypto_layer": {
                        "description": "Криптографический слой - пост-квантовые алгоритмы",
                        "components": [
                            "Falcon - Пост-квантовые цифровые подписи",
                            "SPHINCS+ - Альтернативные пост-квантовые подписи",
                            "SHA3/Keccak - Криптографическое хеширование",
                            "Blake2b - Быстрое хеширование",
                            "AES - Симметричное шифрование"
                        ],
                        "purpose": "Защита от квантовых атак и современная криптография"
                    },
                    "network_layer": {
                        "description": "Сетевой слой - коммуникация и серверы",
                        "components": [
                            "HTTP Server - Веб-сервер с поддержкой SSL/TLS",
                            "JSON-RPC - API сервер для удаленных вызовов",
                            "Stream Processing - Обработка потоков данных",
                            "Client API - Клиентские соединения"
                        ],
                        "purpose": "Сетевая инфраструктура для приложений"
                    },
                    "io_layer": {
                        "description": "Слой ввода/вывода - файлы и потоки",
                        "components": [
                            "File Utils - Утилиты для работы с файлами",
                            "Stream API - Потоковая обработка данных",
                            "Serialization - Сериализация объектов"
                        ],
                        "purpose": "Эффективная работа с данными"
                    },
                    "storage_layer": {
                        "description": "Слой хранения - глобальная база данных",
                        "components": [
                            "Global DB - Глобальная база данных",
                            "MDBX Driver - Высокопроизводительная БД",
                            "Caching - Система кеширования"
                        ],
                        "purpose": "Надежное хранение данных"
                    }
                }
            },
            "cellframe_sdk_architecture": {
                "description": "CellFrame SDK - блокчейн фреймворк построенный на DAP SDK",
                "layers": {
                    "application_layer": {
                        "description": "Слой приложений - dApps и сервисы",
                        "components": [
                            "App Service - Децентрализованные приложения",
                            "Stake Service - Система стейкинга",
                            "Exchange Service - Обмен токенами",
                            "Bridge Service - Мосты между сетями"
                        ],
                        "purpose": "Готовые сервисы для блокчейн приложений"
                    },
                    "consensus_layer": {
                        "description": "Слой консенсуса - алгоритмы достижения согласия",
                        "components": [
                            "DAG PoA - Proof of Authority на DAG",
                            "DAG PoS - Proof of Stake на DAG", 
                            "Block PoW - Proof of Work на блоках",
                            "ESBOCS - Enhanced Scalable Blockchain Consensus"
                        ],
                        "purpose": "Различные алгоритмы консенсуса для разных сценариев"
                    },
                    "blockchain_layer": {
                        "description": "Блокчейн слой - цепи, блоки, транзакции",
                        "components": [
                            "Chain Management - Управление цепочками",
                            "Ledger - Система учета балансов",
                            "Transactions - Обработка транзакций",
                            "Mempool - Пул неподтвержденных транзакций"
                        ],
                        "purpose": "Основная блокчейн функциональность"
                    },
                    "wallet_layer": {
                        "description": "Слой кошельков - управление криптовалютами",
                        "components": [
                            "HD Wallets - Иерархические детерминистические кошельки",
                            "Address Management - Управление адресами",
                            "Balance Tracking - Отслеживание балансов",
                            "Transaction Signing - Подписание транзакций"
                        ],
                        "purpose": "Полное управление криптовалютными кошельками"
                    },
                    "network_layer": {
                        "description": "Сетевой слой блокчейна - P2P и синхронизация",
                        "components": [
                            "Chain Network - Сеть блокчейна",
                            "P2P Protocol - Peer-to-peer протокол",
                            "Synchronization - Синхронизация блоков",
                            "Node Discovery - Обнаружение узлов"
                        ],
                        "purpose": "Сетевая инфраструктура блокчейна"
                    }
                }
            },
            "integration_model": {
                "description": "Как DAP SDK и CellFrame SDK работают вместе",
                "flow": [
                    "1. DAP SDK предоставляет базовую инфраструктуру (крипто, сеть, IO)",
                    "2. CellFrame SDK использует DAP SDK как фундамент",
                    "3. CellFrame SDK добавляет блокчейн специфичную функциональность",
                    "4. Приложения используют CellFrame SDK для блокчейн функций",
                    "5. При необходимости приложения могут обращаться напрямую к DAP SDK"
                ],
                "dependencies": "CellFrame SDK -> DAP SDK -> System Libraries"
            }
        }
        
        return architecture

    async def get_build_and_integration_help(self) -> Dict[str, Any]:
        """Помощь по сборке, установке, интеграции и использованию"""
        help_info = {
            "installation": {
                "system_requirements": {
                    "os": ["Linux (Ubuntu 18.04+, Debian 10+)", "macOS 10.15+", "Windows 10+ (через WSL)"],
                    "compiler": ["GCC 7.0+", "Clang 5.0+"],
                    "cmake": "3.10+",
                    "memory": "Минимум 2GB RAM, рекомендуется 4GB+",
                    "disk": "Минимум 1GB свободного места"
                },
                "dependencies": {
                    "required": [
                        "libmdbx - Высокопроизводительная база данных",
                        "json-c - Библиотека для работы с JSON",
                        "pthread - POSIX threads"
                    ],
                    "optional": [
                        "OpenSSL - Для дополнительных криптографических функций",
                        "libcurl - Для HTTP клиентов",
                        "zlib - Для сжатия данных"
                    ]
                },
                "installation_steps": [
                    "1. Клонировать репозиторий: git clone <repository-url>",
                    "2. Создать директорию сборки: mkdir build && cd build",
                    "3. Настроить сборку: cmake .. -DCMAKE_BUILD_TYPE=Release",
                    "4. Собрать проект: make -j$(nproc)",
                    "5. Установить (опционально): sudo make install"
                ]
            },
            "build_configuration": {
                "cmake_options": {
                    "CMAKE_BUILD_TYPE": ["Debug", "Release", "RelWithDebInfo"],
                    "BUILD_DAP_SDK_TESTS": "ON/OFF - сборка тестов",
                    "BUILD_EXAMPLES": "ON/OFF - сборка примеров",
                    "WITH_SSL": "ON/OFF - поддержка SSL/TLS",
                    "WITH_PYTHON_BINDINGS": "ON/OFF - Python биндинги"
                },
                "example_configurations": [
                    "cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_DAP_SDK_TESTS=ON",
                    "cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_SSL=ON",
                    "cmake .. -DBUILD_EXAMPLES=ON -DWITH_PYTHON_BINDINGS=ON"
                ]
            },
            "integration_guide": {
                "cmake_integration": {
                    "description": "Интеграция DAP SDK в ваш CMake проект",
                    "steps": [
                        "1. Добавить DAP SDK как submodule или скачать",
                        "2. Добавить в CMakeLists.txt: add_subdirectory(dap-sdk)",
                        "3. Линковать библиотеки: target_link_libraries(your_app dap_core dap_crypto dap_net)",
                        "4. Добавить include директории: target_include_directories(your_app PRIVATE ${DAP_SDK_INCLUDE_DIRS})"
                    ],
                    "example_cmakelists": """
cmake_minimum_required(VERSION 3.10)
project(MyDAPApp)

# Добавляем DAP SDK
add_subdirectory(dap-sdk)

# Создаем наше приложение
add_executable(my_app main.c)

# Линкуем с DAP SDK
target_link_libraries(my_app 
    dap_core 
    dap_crypto 
    dap_net
    dap_io
)

# Добавляем include директории
target_include_directories(my_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/dap-sdk/core/include
    ${CMAKE_SOURCE_DIR}/dap-sdk/crypto/include
    ${CMAKE_SOURCE_DIR}/dap-sdk/net/include
)
"""
                },
                "pkg_config_integration": {
                    "description": "Использование pkg-config для интеграции",
                    "commands": [
                        "pkg-config --cflags dap-sdk",
                        "pkg-config --libs dap-sdk"
                    ],
                    "makefile_example": """
CFLAGS += $(shell pkg-config --cflags dap-sdk)
LDFLAGS += $(shell pkg-config --libs dap-sdk)

my_app: main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
"""
                }
            },
            "usage_patterns": {
                "basic_application": {
                    "description": "Базовое DAP приложение",
                    "code_template": """
#include <dap_common.h>
#include <dap_config.h>

int main(int argc, char **argv) {
    // Инициализация DAP SDK
    dap_common_init("my_app", NULL);
    
    // Загрузка конфигурации
    dap_config_t *config = dap_config_open("my_app.cfg");
    
    // Ваша логика приложения
    // ...
    
    // Очистка ресурсов
    dap_config_close(config);
    dap_common_deinit();
    
    return 0;
}
""",
                    "required_includes": ["dap_common.h", "dap_config.h"],
                    "required_libraries": ["dap_core"]
                },
                "crypto_application": {
                    "description": "Приложение с криптографией",
                    "code_template": """
#include <dap_common.h>
#include <dap_enc.h>
#include <dap_sign.h>

int main() {
    // Инициализация
    dap_common_init("crypto_app", NULL);
    
    // Создание ключа для подписи
    dap_enc_key_t *key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_FALCON);
    dap_enc_key_generate(key, NULL, 0, NULL, 0, 0);
    
    // Подписание данных
    const char *data = "Hello, DAP SDK!";
    size_t data_size = strlen(data);
    
    size_t signature_size = dap_enc_sign_output_size(key, data_size);
    uint8_t *signature = malloc(signature_size);
    
    if (dap_enc_sign(key, data, data_size, signature, &signature_size) == 1) {
        printf("Signature created successfully\\n");
    }
    
    // Очистка
    free(signature);
    dap_enc_key_delete(key);
    dap_common_deinit();
    
    return 0;
}
""",
                    "required_includes": ["dap_common.h", "dap_enc.h", "dap_sign.h"],
                    "required_libraries": ["dap_core", "dap_crypto"]
                },
                "http_server_application": {
                    "description": "HTTP сервер приложение",
                    "code_template": """
#include <dap_common.h>
#include <dap_http_server.h>
#include <dap_http_simple.h>

// Обработчик HTTP запросов
void hello_handler(dap_http_simple_t *client, void *arg) {
    const char *response = "Hello from DAP SDK HTTP Server!";
    dap_http_simple_reply(client, response, strlen(response));
}

int main() {
    // Инициализация
    dap_common_init("http_server_app", NULL);
    dap_http_init();
    
    // Создание HTTP сервера
    dap_http_server_t *server = dap_http_server_create("0.0.0.0", 8080);
    if (!server) {
        printf("Failed to create HTTP server\\n");
        return -1;
    }
    
    // Добавление обработчиков
    dap_http_simple_proc_add(server, "/hello", hello_handler, NULL);
    
    // Запуск сервера
    if (dap_http_server_start(server) != 0) {
        printf("Failed to start server\\n");
        return -1;
    }
    
    printf("Server started on http://localhost:8080\\n");
    printf("Try: curl http://localhost:8080/hello\\n");
    
    // Основной цикл (в реальном приложении здесь event loop)
    getchar(); // Ждем Enter для завершения
    
    // Очистка
    dap_http_server_delete(server);
    dap_http_deinit();
    dap_common_deinit();
    
    return 0;
}
""",
                    "required_includes": ["dap_common.h", "dap_http_server.h", "dap_http_simple.h"],
                    "required_libraries": ["dap_core", "dap_net"]
                },
                "blockchain_application": {
                    "description": "Блокчейн приложение (CellFrame SDK)",
                    "code_template": """
#include <dap_common.h>
#include <dap_chain.h>
#include <dap_chain_wallet.h>

int main() {
    // Инициализация CellFrame SDK
    dap_common_init("blockchain_app", NULL);
    dap_chain_init();
    
    // Создание или загрузка цепочки
    dap_chain_t *chain = dap_chain_find_by_name("my_chain");
    if (!chain) {
        chain = dap_chain_create("my_chain", "my_network", DAP_CHAIN_CS_DAG_POA);
        if (!chain) {
            printf("Failed to create chain\\n");
            return -1;
        }
    }
    
    // Создание кошелька
    dap_chain_wallet_t *wallet = dap_chain_wallet_create("my_wallet", "my_network");
    if (!wallet) {
        printf("Failed to create wallet\\n");
        return -1;
    }
    
    // Получение баланса
    uint256_t balance = dap_chain_wallet_get_balance(wallet, chain->net_id, "CELL");
    printf("Wallet balance: %s CELL\\n", dap_chain_balance_to_coins(balance));
    
    // Очистка
    dap_chain_wallet_close(wallet);
    dap_chain_deinit();
    dap_common_deinit();
    
    return 0;
}
""",
                    "required_includes": ["dap_common.h", "dap_chain.h", "dap_chain_wallet.h"],
                    "required_libraries": ["dap_core", "dap_chain", "dap_wallet"]
                }
            },
            "troubleshooting": {
                "common_build_issues": [
                    {
                        "issue": "CMake не находит зависимости",
                        "solution": "Установите dev пакеты: sudo apt-get install libssl-dev libjson-c-dev"
                    },
                    {
                        "issue": "Ошибки компиляции с include файлами",
                        "solution": "Проверьте что все include_directories() прописаны в CMakeLists.txt"
                    },
                    {
                        "issue": "Ошибки линковки",
                        "solution": "Убедитесь что все необходимые библиотеки добавлены в target_link_libraries()"
                    },
                    {
                        "issue": "Segmentation fault при запуске",
                        "solution": "Проверьте что вызваны функции инициализации (dap_common_init, dap_*_init)"
                    }
                ],
                "debugging_tips": [
                    "Используйте valgrind для поиска утечек памяти",
                    "Включайте Debug сборку для отладки: CMAKE_BUILD_TYPE=Debug",
                    "Используйте dap_log_* функции для логирования",
                    "Проверяйте возвращаемые значения всех функций"
                ]
            },
            "best_practices": {
                "memory_management": [
                    "Всегда вызывайте _delete/_free функции для освобождения ресурсов",
                    "Проверяйте результаты malloc/calloc на NULL",
                    "Используйте DAP_NEW/DAP_DELETE макросы для безопасности"
                ],
                "error_handling": [
                    "Проверяйте возвращаемые значения всех функций",
                    "Используйте стандартные коды возврата (0 - успех, отрицательные - ошибка)",
                    "Логируйте ошибки через dap_log_* функции"
                ],
                "performance": [
                    "Используйте Release сборку для продакшена",
                    "Включайте оптимизации компилятора (-O2, -O3)",
                    "Избегайте частых malloc/free в критических участках",
                    "Используйте пулы памяти для часто создаваемых объектов"
                ]
            }
        }
        
        return help_info

