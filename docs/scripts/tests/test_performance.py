import pytest
#!/usr/bin/env python3
"""
Тесты производительности для MCP сервера DAP SDK
"""

import unittest
import tempfile
import time
import asyncio
from pathlib import Path
import sys
import os

# Добавляем путь к основному скрипту
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mcp_server import DAPMCPServer, DAPSDKContext, DAPMCPTools


class TestPerformance:
    """Тесты производительности"""

    @pytest.fixture(autouse=True)
    def setup_method(self):
        """Подготовка тестов производительности"""
        self.temp_dir = tempfile.mkdtemp()
        self.server = DAPMCPServer()
        self.server.context = DAPSDKContext(self.temp_dir)
        self.server.tools = DAPMCPTools(self.server.context)

        # Создаем масштабную тестовую структуру
        self._create_large_test_structure()

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        import shutil
        shutil.rmtree(self.temp_dir)

    def _create_large_test_structure(self):
        """Создание большой тестовой структуры для нагрузочных тестов"""
        # Создаем много файлов для тестирования производительности
        crypto_dir = Path(self.temp_dir) / "crypto" / "src"
        crypto_dir.mkdir(parents=True, exist_ok=True)

        # Создаем 10 алгоритмов с множеством файлов
        for i in range(10):
            algo_dir = crypto_dir / f"algorithm_{i}"
            algo_dir.mkdir(exist_ok=True)

            # Создаем несколько файлов для каждого алгоритма
            for j in range(5):
                c_file = algo_dir / f"algorithm_{i}_{j}.c"
                h_file = algo_dir / f"algorithm_{i}_{j}.h"

                c_content = f"""#include "algorithm_{i}_{j}.h"
/**
 * Algorithm {i} implementation part {j}
 */
void algorithm_{i}_function_{j}() {{
    // Implementation here
}}
"""
                h_content = f"""#ifndef ALGORITHM_{i}_{j}_H
#define ALGORITHM_{i}_{j}_H
void algorithm_{i}_function_{j}();
#endif
"""

                c_file.write_text(c_content)
                h_file.write_text(h_content)

        # Создаем много примеров
        examples_dir = Path(self.temp_dir) / "examples"
        examples_dir.mkdir(exist_ok=True)

        for i in range(20):
            example_file = examples_dir / f"example_{i}.c"
            content = f"""/*
 * Example {i}
 * This is example number {i} demonstrating DAP SDK usage
 */

#include <stdio.h>
#include "dap_sdk.h"

int main{i}() {{
    printf("Example {i} running\\n");
    return 0;
}}
"""
            example_file.write_text(content)

    @pytest.mark.asyncio
    async def test_crypto_analysis_performance(self):
        """Тест производительности анализа крипто алгоритмов"""
        start_time = time.time()

        result = await self.server.tools.analyze_crypto_algorithms()

        end_time = time.time()
        execution_time = end_time - start_time

        # Проверяем что анализ выполнился достаточно быстро (менее 1 секунды)
        assert execution_time < 1.0, f"Analysis took too long: {execution_time} seconds"

        # Проверяем что все алгоритмы были проанализированы
        assert len(result) > 0

    @pytest.mark.asyncio
    async def test_examples_search_performance(self):
        """Тест производительности поиска примеров"""
        start_time = time.time()

        examples = await self.server.tools.find_code_examples()

        end_time = time.time()
        execution_time = end_time - start_time

        # Проверяем производительность
        assert execution_time < 1.0, f"Examples search took too long: {execution_time} seconds"

        # Проверяем что найдены все примеры
        assert len(examples) == 20

    @pytest.mark.asyncio
    async def test_multiple_tool_calls_performance(self):
        """Тест производительности множественных вызовов инструментов"""
        tools = await self.server.handle_list_tools()
        tool_names = [tool.name for tool in tools]

        start_time = time.time()

        # Выполняем все инструменты последовательно
        for tool_name in tool_names:
            result = await self.server.handle_call_tool(tool_name, {})
            assert result is not None

        end_time = time.time()
        total_time = end_time - start_time

        # Проверяем что все вызовы выполнились достаточно быстро
        assert total_time < 5.0, f"All tools took too long: {total_time} seconds"

        # Вычисляем среднее время на инструмент
        avg_time = total_time / len(tool_names)
        assert avg_time < 1.0, f"Average tool execution time too high: {avg_time} seconds"

    @pytest.mark.asyncio
    async def test_concurrent_tool_calls_performance(self):
        """Тест производительности конкурентных вызовов инструментов"""
        import asyncio

        async def call_tool(tool_name):
            start = time.time()
            result = await self.server.handle_call_tool(tool_name, {})
            end = time.time()
            return tool_name, end - start, result

        tools = await self.server.handle_list_tools()
        tool_names = [tool.name for tool in tools]

        start_time = time.time()

        # Выполняем все инструменты конкурентно
        tasks = [call_tool(name) for name in tool_names]
        results = await asyncio.gather(*tasks)

        end_time = time.time()
        total_time = end_time - start_time

        # Проверяем что конкурентное выполнение быстрее последовательного
        assert total_time < 3.0, f"Concurrent execution took too long: {total_time} seconds"

        # Проверяем что все результаты получены
        assert len(results) == len(tool_names)

        for tool_name, exec_time, result in results:
            assert result is not None
            assert exec_time < 2.0, f"Tool {tool_name} took too long: {exec_time} seconds"

    @pytest.mark.asyncio
    async def test_memory_usage_analysis(self):
        """Тест анализа использования памяти"""
        import psutil
        import os

        process = psutil.Process(os.getpid())
        initial_memory = process.memory_info().rss / 1024 / 1024  # MB

        # Выполняем несколько тяжелых операций
        for _ in range(10):
            await self.server.tools.analyze_crypto_algorithms()
            await self.server.tools.find_code_examples()

        final_memory = process.memory_info().rss / 1024 / 1024  # MB
        memory_increase = final_memory - initial_memory

        # Проверяем что нет значительных утечек памяти (менее 50MB)
        assert memory_increase < 50.0, f"Memory increase too high: {memory_increase} MB"

    @pytest.mark.asyncio
    async def test_large_file_handling_performance(self):
        """Тест производительности обработки больших файлов"""
        # Создаем большой файл с примером
        large_example = Path(self.temp_dir) / "examples" / "large_example.c"
        large_example.parent.mkdir(exist_ok=True)

        # Создаем файл с 1000 строками
        lines = []
        lines.append("/*")
        lines.append(" * Large example file for performance testing")
        lines.append(" * This file contains many lines to test performance")
        lines.append(" */")
        lines.append("")
        lines.append("#include <stdio.h>")
        lines.append("#include \"dap_sdk.h\"")
        lines.append("")
        lines.append("int main() {")

        # Добавляем много строк кода
        for i in range(100):
            lines.append(f"    printf(\"Line {i}\\n\");")
            lines.append(f"    // Some comment {i}")
            lines.append("")

        lines.append("    return 0;")
        lines.append("}")

        content = "\n".join(lines)
        large_example.write_text(content)

        start_time = time.time()

        examples = await self.server.tools.find_code_examples()

        end_time = time.time()
        execution_time = end_time - start_time

        # Проверяем производительность с большим файлом
        assert execution_time < 2.0, f"Large file processing took too long: {execution_time} seconds"

        # Проверяем что большой файл найден
        large_example_found = any(ex["name"] == "large_example" for ex in examples)
        assert large_example_found

    def test_context_initialization_performance(self, context):
        """Тест производительности инициализации контекста"""
        start_time = time.time()

        # Создаем несколько контекстов
        for i in range(100):
            context = DAPSDKContext(self.temp_dir)
            assert context is not None

        end_time = time.time()
        execution_time = end_time - start_time

        # Проверяем что инициализация быстрая
        assert execution_time < 1.0, f"Context initialization took too long: {execution_time} seconds"

    @pytest.mark.asyncio
    async def test_error_handling_performance(self):
        """Тест производительности обработки ошибок"""
        # Создаем невалидный контекст
        invalid_context = DAPSDKContext("/nonexistent/path/that/does/not/exist")
        invalid_tools = DAPMCPTools(invalid_context)

        start_time = time.time()

        # Выполняем операции которые должны обработать ошибки
        for _ in range(10):
            await invalid_tools.analyze_crypto_algorithms()
            await invalid_tools.analyze_network_modules()
            await invalid_tools.find_code_examples()

        end_time = time.time()
        execution_time = end_time - start_time

        # Проверяем что обработка ошибок не замедляет систему значительно
        assert execution_time < 2.0, f"Error handling took too long: {execution_time} seconds"


class TestScalability:
    """Тесты масштабируемости"""

    @pytest.fixture(autouse=True)
    def setup_method(self):
        """Подготовка тестов масштабируемости"""
        self.base_temp_dir = tempfile.mkdtemp()

    @pytest.fixture(autouse=True)
    def teardown_method(self):
        """Очистка после тестов"""
        import shutil
        shutil.rmtree(self.base_temp_dir)

    def test_scaling_with_file_count(self):
        """Тест масштабируемости с ростом количества файлов"""
        async def test_with_file_count(file_count):
            temp_dir = Path(self.base_temp_dir) / f"test_{file_count}"
            temp_dir.mkdir(exist_ok=True)

            # Создаем тестовую структуру
            examples_dir = temp_dir / "examples"
            examples_dir.mkdir(exist_ok=True)

            # Создаем указанное количество файлов
            for i in range(file_count):
                example_file = examples_dir / f"example_{i}.c"
                content = f"""// Example {i}
#include "dap_sdk.h"
int main{i}() {{ return 0; }}
"""
                example_file.write_text(content)

            # Создаем сервер и выполняем анализ
            context = DAPSDKContext(str(temp_dir))
            tools = DAPMCPTools(context)

            start_time = time.time()
            examples = await tools.find_code_examples()
            end_time = time.time()

            execution_time = end_time - start_time

            return file_count, len(examples), execution_time

        # Тестируем с разным количеством файлов
        file_counts = [10, 50, 100, 200]

        async def run_scaling_test():
            results = []
            for count in file_counts:
                file_count, found_count, exec_time = await test_with_file_count(count)
                results.append((file_count, found_count, exec_time))

                # Проверяем что найдено правильное количество файлов
                assert found_count == file_count

                # Проверяем что время выполнения разумное
                # Для каждого дополнительного файла добавляем небольшую задержку
                expected_max_time = 0.1 + (count * 0.001)  # базовое время + время на файл
                assert exec_time < expected_max_time,
                              f"Too slow for {count} files: {exec_time} seconds"

            # Проверяем что производительность degrade gracefully
            for i in range(1, len(results)):
                prev_time_per_file = results[i-1][2] / results[i-1][0]
                curr_time_per_file = results[i][2] / results[i][0]

                # Время на файл не должно расти экспоненциально
                degradation_ratio = curr_time_per_file / prev_time_per_file
                assert degradation_ratio < 2.0,
                              f"Performance degraded too much: {degradation_ratio}x"

        asyncio.run(run_scaling_test())


# Tests run via pytest

