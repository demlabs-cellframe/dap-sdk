#!/usr/bin/env python3
"""
Скрипт для конвертации unittest тестов в pytest стиль
"""

import os
import re
from pathlib import Path

def convert_test_file(file_path):
    """Конвертирует unittest файл в pytest стиль"""

    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Заменяем unittest.TestCase на обычный класс
    content = re.sub(r'class\s+(\w+)\(unittest\.TestCase\):', r'class \1:', content)

    # Заменяем self.assert* на assert
    replacements = {
        r'self\.assertEqual\(([^,]+),\s*([^)]+)\)': r'assert \1 == \2',
        r'self\.assertNotEqual\(([^,]+),\s*([^)]+)\)': r'assert \1 != \2',
        r'self\.assertTrue\(([^)]+)\)': r'assert \1',
        r'self\.assertFalse\(([^)]+)\)': r'assert not \1',
        r'self\.assertIsNone\(([^)]+)\)': r'assert \1 is None',
        r'self\.assertIsNotNone\(([^)]+)\)': r'assert \1 is not None',
        r'self\.assertIsInstance\(([^,]+),\s*([^)]+)\)': r'assert isinstance(\1, \2)',
        r'self\.assertIn\(([^,]+),\s*([^)]+)\)': r'assert \1 in \2',
        r'self\.assertGreater\(([^,]+),\s*([^)]+)\)': r'assert \1 > \2',
        r'self\.assertLess\(([^,]+),\s*([^)]+)\)': r'assert \1 < \2',
    }

    for pattern, replacement in replacements.items():
        content = re.sub(pattern, replacement, content)

    # Убираем unittest.main()
    content = re.sub(r'if __name__ == \'__main__\':\s*unittest\.main\(\)', '# Tests run via pytest', content)

    # Добавляем pytest import если его нет
    if 'import pytest' not in content:
        # Находим место после других импортов
        import_match = re.search(r'(import.*\n)*', content)
        if import_match:
            insert_pos = import_match.end()
            content = content[:insert_pos] + 'import pytest\n\n' + content[insert_pos:]

    # Заменяем setUp и tearDown на фикстуры
    content = re.sub(r'    def setUp\(self\):', '    @pytest.fixture(autouse=True)\n    def setup_method(self):', content)
    content = re.sub(r'    def tearDown\(self\):', '    @pytest.fixture(autouse=True)\n    def teardown_method(self):', content)

    return content

def update_method_signatures(content):
    """Обновляет сигнатуры методов для использования фикстур"""

    # Ищем все def test_ методы и добавляем фикстуры в параметры
    lines = content.split('\n')
    updated_lines = []

    for i, line in enumerate(lines):
        if re.match(r'    def test_', line):
            # Находим метод и его тело
            method_name = re.search(r'def (test_\w+)\(self', line)
            if method_name:
                method_name = method_name.group(1)
                # Добавляем фикстуры в зависимости от имени метода
                if 'context' in method_name or 'server' in method_name or 'tools' in method_name:
                    if 'context' in method_name:
                        line = re.sub(r'def test_\w+\(self\)', f'def {method_name}(self, context)', line)
                    elif 'server' in method_name:
                        line = re.sub(r'def test_\w+\(self\)', f'def {method_name}(self, server)', line)
                    elif 'tools' in method_name:
                        line = re.sub(r'def test_\w+\(self\)', f'def {method_name}(self, tools)', line)
                    else:
                        line = re.sub(r'def test_\w+\(self\)', f'def {method_name}(self)', line)
                else:
                    line = re.sub(r'def test_\w+\(self\)', f'def {method_name}(self)', line)

        updated_lines.append(line)

    return '\n'.join(updated_lines)

def main():
    """Основная функция"""
    tests_dir = Path(__file__).parent / 'tests'

    # Конвертируем все test_*.py файлы
    for test_file in tests_dir.glob('test_*.py'):
        if test_file.name == 'test_context.py':
            continue  # Уже конвертировали

        print(f"Конвертируем {test_file.name}...")

        # Читаем файл
        with open(test_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Конвертируем
        content = convert_test_file(str(test_file))
        content = update_method_signatures(content)

        # Сохраняем
        with open(test_file, 'w', encoding='utf-8') as f:
            f.write(content)

        print(f"✓ {test_file.name} конвертирован")

if __name__ == '__main__':
    main()
