#!/usr/bin/env python3
"""
Comprehensive Template Validation Suite
Автоматическая валидация всех templates в SLC системе
"""

import json
import os
import sys
import subprocess
import re
from pathlib import Path
from typing import Dict, List, Tuple, Any
import time
from dataclasses import dataclass

@dataclass
class ValidationResult:
    """Результат валидации template"""
    template_name: str
    status: str  # "PASS", "FAIL", "WARNING"
    issues: List[str]
    execution_time: float
    details: Dict[str, Any]

class TemplateValidator:
    """Comprehensive validator для всех templates"""
    
    def __init__(self, context_root: str):
        self.context_root = Path(context_root)
        self.results: List[ValidationResult] = []
        self.stats = {
            "total_templates": 0,
            "passed": 0,
            "failed": 0,
            "warnings": 0,
            "total_time": 0
        }
    
    def validate_all(self) -> bool:
        """Запуск полной валидации всех templates"""
        print("🔍 Запуск Comprehensive Template Validation Suite...")
        
        start_time = time.time()
        
        # 1. Структурная валидация
        self._validate_json_structure()
        self._validate_markdown_structure() 
        self._validate_cross_references()
        
        # 2. Содержательная валидация
        self._validate_workflow_completeness()
        self._validate_examples()
        self._validate_dependencies()
        
        # 3. Performance testing
        self._validate_performance()
        
        # 4. Edge case testing
        self._run_edge_case_tests()
        
        self.stats["total_time"] = time.time() - start_time
        
        # Генерация отчета
        self._generate_report()
        
        return self.stats["failed"] == 0
    
    def _validate_json_structure(self):
        """Валидация JSON структуры всех templates"""
        print("📋 Валидация JSON структуры...")
        
        json_files = list(self.context_root.rglob("*.json"))
        
        for json_file in json_files:
            start_time = time.time()
            issues = []
            
            try:
                with open(json_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                
                # Проверка обязательных полей
                if 'type' not in data:
                    issues.append("Отсутствует обязательное поле 'type'")
                
                if 'version' not in data:
                    issues.append("Отсутствует поле 'version'")
                
                # Специфические проверки для different types
                if data.get('type') == 'universal_template_module':
                    self._validate_template_module(data, issues)
                elif data.get('type') == 'workflow_recommendation_engine':
                    self._validate_workflow_engine(data, issues)
                
            except json.JSONDecodeError as e:
                issues.append(f"JSON синтаксическая ошибка: {e}")
            except Exception as e:
                issues.append(f"Ошибка валидации: {e}")
            
            execution_time = time.time() - start_time
            status = "FAIL" if issues else "PASS"
            
            result = ValidationResult(
                template_name=str(json_file.relative_to(self.context_root)),
                status=status,
                issues=issues,
                execution_time=execution_time,
                details={"file_type": "json", "size": json_file.stat().st_size}
            )
            
            self.results.append(result)
            self._update_stats(status)
    
    def _validate_template_module(self, data: Dict, issues: List[str]):
        """Специфическая валидация для template modules"""
        required_fields = ['template_info', 'основные_принципы']
        
        for field in required_fields:
            if field not in data:
                issues.append(f"Отсутствует обязательное поле '{field}' для template module")
        
        # Проверка template_info структуры
        if 'template_info' in data:
            template_info = data['template_info']
            if 'name' not in template_info:
                issues.append("template_info должен содержать 'name'")
            if 'description' not in template_info:
                issues.append("template_info должен содержать 'description'")
    
    def _validate_workflow_engine(self, data: Dict, issues: List[str]):
        """Валидация workflow recommendation engine"""
        required_sections = ['workflow_patterns', 'интеллектуальные_предложения']
        
        for section in required_sections:
            if section not in data:
                issues.append(f"Workflow engine должен содержать '{section}'")
        
        # Проверка workflow patterns
        if 'workflow_patterns' in data:
            for pattern_name, pattern_data in data['workflow_patterns'].items():
                if 'trigger' not in pattern_data:
                    issues.append(f"Workflow pattern '{pattern_name}' должен содержать 'trigger'")
                if 'рекомендуемая_последовательность' not in pattern_data:
                    issues.append(f"Workflow pattern '{pattern_name}' должен содержать последовательность")
    
    def _validate_markdown_structure(self):
        """Валидация Markdown файлов"""
        print("📝 Валидация Markdown структуры...")
        
        md_files = list(self.context_root.rglob("*.md"))
        
        for md_file in md_files:
            start_time = time.time()
            issues = []
            
            try:
                with open(md_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                # Проверка заголовков
                if not content.startswith('#'):
                    issues.append("Файл должен начинаться с заголовка")
                
                # Проверка broken links (basic check)
                links = re.findall(r'\[([^\]]+)\]\(([^)]+)\)', content)
                for link_text, link_url in links:
                    if link_url.startswith('http'):
                        continue  # Skip external links for now
                    
                    # Check internal links
                    if not link_url.startswith('#'):  # Not an anchor
                        link_path = md_file.parent / link_url
                        if not link_path.exists():
                            issues.append(f"Broken internal link: {link_url}")
                
                # Проверка структуры заголовков
                headers = re.findall(r'^(#+)\s+(.+)$', content, re.MULTILINE)
                if headers:
                    prev_level = 0
                    for header_markup, header_text in headers:
                        level = len(header_markup)
                        if level > prev_level + 1:
                            issues.append(f"Пропущен уровень заголовка перед '{header_text}'")
                        prev_level = level
                
            except Exception as e:
                issues.append(f"Ошибка чтения файла: {e}")
            
            execution_time = time.time() - start_time
            status = "FAIL" if issues else "PASS"
            
            result = ValidationResult(
                template_name=str(md_file.relative_to(self.context_root)),
                status=status,
                issues=issues,
                execution_time=execution_time,
                details={"file_type": "markdown", "size": md_file.stat().st_size}
            )
            
            self.results.append(result)
            self._update_stats(status)
    
    def _validate_cross_references(self):
        """Проверка cross-references между templates"""
        print("🔗 Валидация cross-references...")
        
        # Collect all template references
        all_templates = set()
        references = {}
        
        # Scan all files for template references
        for file_path in self.context_root.rglob("*"):
            if file_path.is_file() and file_path.suffix in ['.json', '.md']:
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        content = f.read()
                    
                    # Extract template names mentioned
                    template_mentions = re.findall(r'([A-Z][a-zA-Z\s]+(?:Enhanced|Optimization|Documentation|Matrix|Engine))', content)
                    
                    if template_mentions:
                        references[str(file_path.relative_to(self.context_root))] = template_mentions
                
                except Exception:
                    continue
        
        # Check for broken references (simplified check)
        issues = []
        known_templates = {
            "C Development Enhanced",
            "Live Documentation", 
            "Universal Optimization",
            "Cross-Domain Matrix",
            "Workflow Recommendation Engine"
        }
        
        for file_path, mentions in references.items():
            for mention in mentions:
                if mention not in known_templates and "template" in mention.lower():
                    issues.append(f"Возможная broken reference в {file_path}: {mention}")
        
        result = ValidationResult(
            template_name="cross_references",
            status="FAIL" if issues else "PASS",
            issues=issues,
            execution_time=0.1,
            details={"references_checked": len(references)}
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _validate_workflow_completeness(self):
        """Проверка completeness workflow patterns"""
        print("🔄 Валидация workflow completeness...")
        
        workflow_file = self.context_root / "tools" / "workflow_recommendation_engine.json"
        issues = []
        
        if not workflow_file.exists():
            issues.append("Workflow recommendation engine file не найден")
        else:
            try:
                with open(workflow_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                
                patterns = data.get('workflow_patterns', {})
                
                # Проверка каждого pattern на completeness
                for pattern_name, pattern_data in patterns.items():
                    if 'рекомендуемая_последовательность' not in pattern_data:
                        issues.append(f"Pattern {pattern_name} не содержит последовательность")
                        continue
                    
                    sequence = pattern_data['рекомендуемая_последовательность']
                    for step in sequence:
                        if 'шаблон' not in step:
                            issues.append(f"Step в {pattern_name} не содержит шаблон")
                        if 'описание' not in step:
                            issues.append(f"Step в {pattern_name} не содержит описание")
                
            except Exception as e:
                issues.append(f"Ошибка чтения workflow file: {e}")
        
        result = ValidationResult(
            template_name="workflow_completeness",
            status="FAIL" if issues else "PASS",
            issues=issues,
            execution_time=0.1,
            details={"patterns_checked": len(patterns) if 'patterns' in locals() else 0}
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _validate_examples(self):
        """Валидация примеров в templates"""
        print("💡 Валидация примеров...")
        
        # This would normally compile/test code examples
        # For now, just check that examples exist
        issues = []
        
        # Check C Development Enhanced template for examples
        c_dev_files = list(self.context_root.rglob("*c_development*"))
        
        example_count = 0
        for file_path in c_dev_files:
            if file_path.is_file():
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        content = f.read()
                    
                    # Count code blocks
                    code_blocks = re.findall(r'```[a-zA-Z]*\n(.*?)\n```', content, re.DOTALL)
                    example_count += len(code_blocks)
                
                except Exception:
                    continue
        
        if example_count == 0:
            issues.append("Не найдено примеров кода в templates")
        
        result = ValidationResult(
            template_name="examples_validation",
            status="WARNING" if issues else "PASS",
            issues=issues,
            execution_time=0.1,
            details={"examples_found": example_count}
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _validate_dependencies(self):
        """Проверка dependencies между templates"""
        print("🔗 Валидация dependencies...")
        
        # Check navigation matrix for consistency
        matrix_file = self.context_root / "docs" / "матрица_взаимосвязи_шаблонов.md"
        issues = []
        
        if not matrix_file.exists():
            issues.append("Template interconnection matrix не найден")
        else:
            try:
                with open(matrix_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                # Basic check for workflow patterns
                if "Pattern 1:" not in content:
                    issues.append("Не найдены workflow patterns в matrix")
                
                if "Navigation Hints" not in content and "Навигационные Подсказки" not in content:
                    issues.append("Не найдены navigation hints в matrix")
                
            except Exception as e:
                issues.append(f"Ошибка чтения matrix file: {e}")
        
        result = ValidationResult(
            template_name="dependencies_validation",
            status="FAIL" if issues else "PASS",
            issues=issues,
            execution_time=0.1,
            details={"matrix_exists": matrix_file.exists()}
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _validate_performance(self):
        """Performance testing templates"""
        print("⚡ Performance testing...")
        
        issues = []
        start_time = time.time()
        
        # Simulate template loading performance
        json_files = list(self.context_root.rglob("*.json"))
        
        total_load_time = 0
        for json_file in json_files:
            file_start = time.time()
            try:
                with open(json_file, 'r', encoding='utf-8') as f:
                    json.load(f)
                file_load_time = time.time() - file_start
                total_load_time += file_load_time
                
                # Check if any file takes too long
                if file_load_time > 0.5:  # 500ms threshold
                    issues.append(f"Slow loading file: {json_file.name} ({file_load_time:.2f}s)")
                    
            except Exception as e:
                issues.append(f"Load error {json_file.name}: {e}")
        
        execution_time = time.time() - start_time
        
        result = ValidationResult(
            template_name="performance_test",
            status="WARNING" if issues else "PASS",
            issues=issues,
            execution_time=execution_time,
            details={
                "total_files": len(json_files),
                "total_load_time": total_load_time,
                "average_load_time": total_load_time / len(json_files) if json_files else 0
            }
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _run_edge_case_tests(self):
        """Edge case testing"""
        print("🧪 Edge case testing...")
        
        issues = []
        
        # Test 1: Empty inputs handling
        try:
            # Simulate template with missing required fields
            empty_template = {}
            # This would normally test template generation with empty input
            # For now, just flag that edge case testing is needed
            issues.append("Edge case testing implementation needed")
        except Exception:
            pass
        
        # Test 2: Large input handling
        # Test 3: Concurrent access
        # Test 4: Resource constraints
        
        result = ValidationResult(
            template_name="edge_case_tests",
            status="WARNING",  # Always warning since implementation is pending
            issues=issues,
            execution_time=0.1,
            details={"test_cases_planned": 4, "implemented": 0}
        )
        
        self.results.append(result)
        self._update_stats(result.status)
    
    def _update_stats(self, status: str):
        """Обновление статистики"""
        self.stats["total_templates"] += 1
        if status == "PASS":
            self.stats["passed"] += 1
        elif status == "FAIL":
            self.stats["failed"] += 1
        else:  # WARNING
            self.stats["warnings"] += 1
    
    def _generate_report(self):
        """Генерация отчета валидации"""
        print("\n" + "="*60)
        print("📊 COMPREHENSIVE TEMPLATE VALIDATION REPORT")
        print("="*60)
        
        print(f"Общая статистика:")
        print(f"  Всего проверено: {self.stats['total_templates']}")
        print(f"  ✅ Прошли: {self.stats['passed']}")
        print(f"  ⚠️  Предупреждения: {self.stats['warnings']}")
        print(f"  ❌ Провалены: {self.stats['failed']}")
        print(f"  🕐 Общее время: {self.stats['total_time']:.2f}s")
        
        if self.stats['failed'] > 0:
            print(f"\n❌ FAILED VALIDATIONS:")
            for result in self.results:
                if result.status == "FAIL":
                    print(f"  {result.template_name}:")
                    for issue in result.issues:
                        print(f"    - {issue}")
        
        if self.stats['warnings'] > 0:
            print(f"\n⚠️  WARNINGS:")
            for result in self.results:
                if result.status == "WARNING":
                    print(f"  {result.template_name}:")
                    for issue in result.issues:
                        print(f"    - {issue}")
        
        print(f"\n🎯 Успешность: {(self.stats['passed'] / self.stats['total_templates'] * 100):.1f}%")
        
        # Рекомендации
        print(f"\n💡 РЕКОМЕНДАЦИИ:")
        if self.stats['failed'] > 0:
            print("  - Исправить критические ошибки перед продакшеном")
        if self.stats['warnings'] > 0:
            print("  - Рассмотреть предупреждения для улучшения качества")
        if self.stats['passed'] == self.stats['total_templates']:
            print("  - Все проверки пройдены! Система готова к использованию")

def main():
    """Основная функция"""
    if len(sys.argv) > 1:
        context_root = sys.argv[1]
    else:
        context_root = "context/context.reflection/context"
    
    if not os.path.exists(context_root):
        print(f"❌ Ошибка: Директория {context_root} не найдена")
        sys.exit(1)
    
    validator = TemplateValidator(context_root)
    success = validator.validate_all()
    
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main() 