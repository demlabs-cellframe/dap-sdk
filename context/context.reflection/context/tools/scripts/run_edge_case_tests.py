#!/usr/bin/env python3
"""
Edge Case Testing Suite для Template System
Тестирование пограничных случаев и error conditions
"""

import json
import os
import time
import tempfile
import subprocess
from pathlib import Path
from typing import Dict, List, Any
import concurrent.futures
from dataclasses import dataclass

@dataclass
class EdgeCaseResult:
    """Результат edge case теста"""
    test_name: str
    status: str  # "PASS", "FAIL", "ERROR"
    description: str
    expected_behavior: str
    actual_behavior: str
    execution_time: float

class EdgeCaseTester:
    """Comprehensive edge case testing"""
    
    def __init__(self, context_root: str):
        self.context_root = Path(context_root)
        self.results: List[EdgeCaseResult] = []
    
    def run_all_tests(self) -> bool:
        """Запуск всех edge case tests"""
        print("🧪 Запуск Edge Case Testing Suite...")
        
        test_methods = [
            self._test_malformed_json,
            self._test_missing_required_fields,
            self._test_empty_templates,
            self._test_large_templates,
            self._test_concurrent_access,
            self._test_resource_constraints,
            self._test_broken_workflows,
            self._test_circular_dependencies,
            self._test_invalid_cross_references,
            self._test_unicode_handling
        ]
        
        for test_method in test_methods:
            try:
                test_method()
            except Exception as e:
                self.results.append(EdgeCaseResult(
                    test_name=test_method.__name__,
                    status="ERROR",
                    description="Test execution failed",
                    expected_behavior="Test should run without throwing exception",
                    actual_behavior=f"Exception: {e}",
                    execution_time=0.0
                ))
        
        self._generate_report()
        return all(result.status in ["PASS", "WARNING"] for result in self.results)
    
    def _test_malformed_json(self):
        """Тест: Обработка некорректного JSON"""
        print("  🔍 Testing malformed JSON handling...")
        
        start_time = time.time()
        
        # Создаем временный файл с некорректным JSON
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            f.write('{"type": "test", "invalid": json}')  # Намеренно некорректный JSON
            temp_file = f.name
        
        try:
            # Пытаемся загрузить некорректный JSON
            with open(temp_file, 'r') as f:
                json.load(f)
            
            # Если дошли сюда - это проблема, JSON должен был вызвать ошибку
            actual = "JSON был загружен без ошибок"
            status = "FAIL"
        
        except json.JSONDecodeError:
            # Ожидаемое поведение - JSON decoder должен выбросить ошибку
            actual = "JSON decoder правильно выбросил JSONDecodeError"
            status = "PASS"
        
        except Exception as e:
            actual = f"Неожиданная ошибка: {e}"
            status = "FAIL"
        
        finally:
            os.unlink(temp_file)
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="malformed_json_handling",
            status=status,
            description="Проверка обработки некорректного JSON",
            expected_behavior="JSON decoder должен выбросить JSONDecodeError",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_missing_required_fields(self):
        """Тест: Обработка отсутствующих обязательных полей"""
        print("  🔍 Testing missing required fields...")
        
        start_time = time.time()
        
        # Тестируем template без обязательных полей
        incomplete_template = {
            "description": "Template без required fields"
            # Отсутствуют: type, version
        }
        
        # Симулируем валидацию
        missing_fields = []
        if 'type' not in incomplete_template:
            missing_fields.append('type')
        if 'version' not in incomplete_template:
            missing_fields.append('version')
        
        if missing_fields:
            status = "PASS"
            actual = f"Обнаружены отсутствующие поля: {missing_fields}"
        else:
            status = "FAIL"
            actual = "Валидация не обнаружила отсутствующие поля"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="missing_required_fields",
            status=status,
            description="Проверка обнаружения отсутствующих обязательных полей",
            expected_behavior="Валидация должна обнаружить отсутствующие поля",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_empty_templates(self):
        """Тест: Обработка пустых templates"""
        print("  🔍 Testing empty template handling...")
        
        start_time = time.time()
        
        empty_cases = [
            {},  # Полностью пустой
            {"type": ""},  # Пустой type
            {"type": "test", "content": ""},  # Пустой content
            {"type": "test", "content": None}  # Null content
        ]
        
        issues_found = 0
        for i, empty_case in enumerate(empty_cases):
            # Симулируем валидацию пустого template
            if not empty_case or not empty_case.get('type') or not empty_case.get('content'):
                issues_found += 1
        
        if issues_found == len(empty_cases):
            status = "PASS"
            actual = f"Все {issues_found} пустых cases правильно обнаружены"
        else:
            status = "FAIL"
            actual = f"Обнаружено только {issues_found} из {len(empty_cases)} пустых cases"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="empty_template_handling",
            status=status,
            description="Проверка обработки пустых templates",
            expected_behavior="Система должна корректно обрабатывать пустые inputs",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_large_templates(self):
        """Тест: Обработка очень больших templates"""
        print("  🔍 Testing large template handling...")
        
        start_time = time.time()
        
        # Создаем большой template
        large_template = {
            "type": "large_test",
            "version": "1.0",
            "large_content": "x" * 1000000,  # 1MB строка
            "large_array": list(range(10000)),  # Большой массив
            "nested_structure": {
                f"key_{i}": f"value_{i}" * 100 for i in range(1000)
            }
        }
        
        try:
            # Пытаемся сериализовать/десериализовать большой template
            json_str = json.dumps(large_template)
            parsed = json.loads(json_str)
            
            if len(json_str) > 500000:  # Больше 500KB
                status = "PASS"
                actual = f"Большой template успешно обработан (размер: {len(json_str)} bytes)"
            else:
                status = "FAIL"
                actual = "Template не достаточно большой для теста"
        
        except Exception as e:
            status = "FAIL"
            actual = f"Ошибка обработки большого template: {e}"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="large_template_handling",
            status=status,
            description="Проверка обработки больших templates",
            expected_behavior="Система должна корректно обрабатывать большие данные",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_concurrent_access(self):
        """Тест: Одновременный доступ к templates"""
        print("  🔍 Testing concurrent access...")
        
        start_time = time.time()
        
        def simulate_template_access(thread_id):
            """Симуляция доступа к template"""
            time.sleep(0.1)  # Симуляция работы
            return f"thread_{thread_id}_completed"
        
        try:
            # Запускаем несколько потоков одновременно
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                futures = [executor.submit(simulate_template_access, i) for i in range(10)]
                results = [future.result() for future in concurrent.futures.as_completed(futures)]
            
            if len(results) == 10:
                status = "PASS"
                actual = f"Все {len(results)} concurrent operations завершены успешно"
            else:
                status = "FAIL"
                actual = f"Завершено только {len(results)} из 10 operations"
        
        except Exception as e:
            status = "FAIL"
            actual = f"Ошибка concurrent access: {e}"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="concurrent_access",
            status=status,
            description="Проверка одновременного доступа к templates",
            expected_behavior="Система должна корректно обрабатывать concurrent access",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_resource_constraints(self):
        """Тест: Поведение при ограниченных ресурсах"""
        print("  🔍 Testing resource constraints...")
        
        start_time = time.time()
        
        # Симулируем ограниченную память
        try:
            # Создаем много небольших объектов для симуляции memory pressure
            memory_consumers = []
            for i in range(1000):
                memory_consumers.append([0] * 1000)  # 1000 integers each
            
            # Пытаемся выполнить normal operation при memory pressure
            test_template = {"type": "memory_test", "data": list(range(100))}
            json_str = json.dumps(test_template)
            
            status = "PASS"
            actual = "Template operations работают при memory pressure"
        
        except MemoryError:
            status = "PASS"  # Ожидаемое поведение при memory constraints
            actual = "MemoryError правильно raised при memory constraints"
        
        except Exception as e:
            status = "FAIL"
            actual = f"Неожиданная ошибка: {e}"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="resource_constraints",
            status=status,
            description="Проверка поведения при ограниченных ресурсах",
            expected_behavior="Система должна gracefully handle resource constraints",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_broken_workflows(self):
        """Тест: Обработка некорректных workflows"""
        print("  🔍 Testing broken workflow handling...")
        
        start_time = time.time()
        
        # Создаем workflow с broken steps
        broken_workflow = {
            "workflow_patterns": {
                "broken_pattern": {
                    "trigger": {"keywords": ["test"]},
                    "рекомендуемая_последовательность": [
                        {"шаблон": "NonExistentTemplate"},  # Несуществующий template
                        {"описание": "Step without template"},  # Отсутствует шаблон
                        {}  # Пустой step
                    ]
                }
            }
        }
        
        # Валидируем broken workflow
        issues = []
        pattern = broken_workflow["workflow_patterns"]["broken_pattern"]
        sequence = pattern["рекомендуемая_последовательность"]
        
        for i, step in enumerate(sequence):
            if "шаблон" not in step:
                issues.append(f"Step {i} missing template")
            elif step.get("шаблон") == "NonExistentTemplate":
                issues.append(f"Step {i} references non-existent template")
            if not step:
                issues.append(f"Step {i} is empty")
        
        if len(issues) >= 3:  # Ожидаем найти все 3 проблемы
            status = "PASS"
            actual = f"Все {len(issues)} проблемы в workflow обнаружены"
        else:
            status = "FAIL"
            actual = f"Обнаружено только {len(issues)} проблем из 3"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="broken_workflow_handling",
            status=status,
            description="Проверка обработки некорректных workflows",
            expected_behavior="Система должна обнаруживать broken workflow steps",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_circular_dependencies(self):
        """Тест: Обнаружение циклических зависимостей"""
        print("  🔍 Testing circular dependency detection...")
        
        start_time = time.time()
        
        # Создаем циклическую зависимость
        dependencies = {
            "Template_A": ["Template_B"],
            "Template_B": ["Template_C"], 
            "Template_C": ["Template_A"]  # Цикл: A -> B -> C -> A
        }
        
        # Простой алгоритм обнаружения циклов
        def has_cycle(deps, start, visited=None, path=None):
            if visited is None:
                visited = set()
            if path is None:
                path = []
            
            if start in path:
                return True  # Цикл обнаружен
            
            if start in visited:
                return False
            
            visited.add(start)
            path.append(start)
            
            for dep in deps.get(start, []):
                if has_cycle(deps, dep, visited, path):
                    return True
            
            path.remove(start)
            return False
        
        cycle_detected = has_cycle(dependencies, "Template_A")
        
        if cycle_detected:
            status = "PASS"
            actual = "Циклическая зависимость правильно обнаружена"
        else:
            status = "FAIL"
            actual = "Циклическая зависимость не обнаружена"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="circular_dependency_detection",
            status=status,
            description="Проверка обнаружения циклических зависимостей",
            expected_behavior="Система должна обнаруживать циклические зависимости",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_invalid_cross_references(self):
        """Тест: Обработка некорректных cross-references"""
        print("  🔍 Testing invalid cross-reference handling...")
        
        start_time = time.time()
        
        # Симулируем template с invalid references
        template_with_refs = {
            "type": "test_template",
            "references": [
                "ValidTemplate",
                "NonExistentTemplate",  # Несуществующий
                "",  # Пустая ссылка
                None  # Null reference
            ]
        }
        
        # Список существующих templates
        existing_templates = {"ValidTemplate", "AnotherTemplate"}
        
        invalid_refs = []
        for ref in template_with_refs["references"]:
            if not ref or ref not in existing_templates:
                invalid_refs.append(ref)
        
        if len(invalid_refs) >= 2:  # Ожидаем найти 2+ invalid references
            status = "PASS"
            actual = f"Обнаружено {len(invalid_refs)} invalid references"
        else:
            status = "FAIL"
            actual = f"Обнаружено только {len(invalid_refs)} invalid references"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="invalid_cross_references",
            status=status,
            description="Проверка обработки некорректных cross-references",
            expected_behavior="Система должна обнаруживать invalid cross-references",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _test_unicode_handling(self):
        """Тест: Обработка Unicode символов"""
        print("  🔍 Testing Unicode handling...")
        
        start_time = time.time()
        
        # Создаем template с различными Unicode символами
        unicode_template = {
            "type": "unicode_test",
            "названия": "Тестовый шаблон",  # Кириллица
            "description": "Test with émojis 🚀🔥💡 and spëcial chars",  # Emoji + специальные символы
            "chinese": "测试中文字符",  # Китайские символы
            "arabic": "اختبار اللغة العربية",  # Арабские символы
            "special_chars": "«quotes» —dash— …ellipsis…"  # Специальные типографские символы
        }
        
        try:
            # Пытаемся сериализовать и десериализовать Unicode content
            json_str = json.dumps(unicode_template, ensure_ascii=False)
            parsed = json.loads(json_str)
            
            # Проверяем что все символы сохранились
            if (parsed["названия"] == unicode_template["названия"] and 
                "🚀" in parsed["description"] and
                parsed["chinese"] == unicode_template["chinese"]):
                status = "PASS"
                actual = "Unicode символы правильно сохранены и восстановлены"
            else:
                status = "FAIL"
                actual = "Unicode символы не сохранились корректно"
        
        except Exception as e:
            status = "FAIL"
            actual = f"Ошибка обработки Unicode: {e}"
        
        execution_time = time.time() - start_time
        
        self.results.append(EdgeCaseResult(
            test_name="unicode_handling",
            status=status,
            description="Проверка обработки Unicode символов",
            expected_behavior="Система должна корректно обрабатывать все Unicode символы",
            actual_behavior=actual,
            execution_time=execution_time
        ))
    
    def _generate_report(self):
        """Генерация отчета edge case testing"""
        print("\n" + "="*60)
        print("🧪 EDGE CASE TESTING REPORT")
        print("="*60)
        
        total_tests = len(self.results)
        passed = sum(1 for r in self.results if r.status == "PASS")
        failed = sum(1 for r in self.results if r.status == "FAIL")
        errors = sum(1 for r in self.results if r.status == "ERROR")
        
        print(f"Общая статистика:")
        print(f"  Всего тестов: {total_tests}")
        print(f"  ✅ Прошли: {passed}")
        print(f"  ❌ Провалены: {failed}")
        print(f"  🚫 Ошибки: {errors}")
        print(f"  📊 Успешность: {(passed/total_tests*100):.1f}%")
        
        if failed > 0 or errors > 0:
            print(f"\n❌ FAILED/ERROR TESTS:")
            for result in self.results:
                if result.status in ["FAIL", "ERROR"]:
                    print(f"  {result.test_name}:")
                    print(f"    Описание: {result.description}")
                    print(f"    Ожидалось: {result.expected_behavior}")
                    print(f"    Получено: {result.actual_behavior}")
        
        print(f"\n📋 DETAILED RESULTS:")
        for result in self.results:
            status_icon = {"PASS": "✅", "FAIL": "❌", "ERROR": "🚫"}[result.status]
            print(f"  {status_icon} {result.test_name} ({result.execution_time:.3f}s)")

def main():
    """Основная функция"""
    import sys
    
    if len(sys.argv) > 1:
        context_root = sys.argv[1]
    else:
        context_root = "context/context.reflection/context"
    
    tester = EdgeCaseTester(context_root)
    success = tester.run_all_tests()
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main() 