# DAP Context Module (context/)

## Обзор

Модуль `context` DAP SDK предоставляет централизованную систему управления контекстом проекта, включая метаданные, стандарты разработки, шаблоны кода и навигационные помощники. Этот модуль не содержит исполняемого кода, а служит информационным хабом для разработчиков и инструментов.

## 🎯 Назначение

Context модуль решает следующие задачи:

- ✅ **Централизованное хранение метаданных** проекта
- ✅ **Стандартизация подходов разработки**
- ✅ **Предоставление шаблонов кода**
- ✅ **Навигационные помощники** для инструментов
- ✅ **Документация стандартов качества**

## 📁 Структура модуля

```
context/
├── context.json              # Основной контекст проекта
├── project_standards.json    # Стандарты проекта
├── coding_guidelines.json    # Правила кодирования
├── code_templates.json       # Шаблоны кода
├── structure.json            # Структура проекта
├── index.json               # Индекс модулей
├── modules/                 # Конфигурация модулей
│   ├── core.json
│   ├── crypto.json
│   ├── net.json
│   └── other.json
├── scripts/                 # Вспомогательные скрипты
│   ├── load_full_context.sh
│   ├── load_module.sh
│   └── validate_context.sh
├── human_docs/              # Документация для разработчиков
│   ├── architecture_guide.md
│   ├── coding_standards.md
│   ├── testing_guide.md
│   ├── security_practices.md
│   └── deployment_guide.md
└── tests/                   # Тесты контекста
    └── validate_context.sh
```

## 📋 Основные файлы

### `context.json` - Основной контекст проекта

```json
{
  "version": "1.0",
  "created": "2025-01-03T15:00:00Z",
  "updated": "2025-06-05T14:00:00Z",
  "project": {
    "name": "DAP SDK",
    "description": "Decentralized Application Platform Software Development Kit",
    "repository": "dap-sdk.dev",
    "focus_area": "Quantum-resistant cryptography and blockchain infrastructure"
  }
}
```

**Назначение:**
- Определение основных метаданных проекта
- Ссылки на связанные файлы контекста
- Информация о техническом стеке
- Навигационные помощники

### `project_standards.json` - Стандарты проекта

```json
{
  "documentation": {
    "language": "Russian for documentation, English for code",
    "structure": [
      "Title",
      "Description (## Описание)",
      "Module Structure (## Структура модуля)",
      "Main Components",
      "Usage Examples (## Примеры использования)",
      "Implementation Notes (## Особенности реализации)",
      "See Also (## См. также)"
    ]
  },
  "quality_requirements": {
    "code_examples": {
      "must_compile": true,
      "memory_safe": true,
      "follow_conventions": true,
      "include_error_handling": true
    }
  }
}
```

**Назначение:**
- Определение стандартов документации
- Требования к качеству кода
- Правила локализации
- Критерии приемки

### `coding_guidelines.json` - Правила кодирования

```json
{
  "naming_conventions": {
    "functions": {
      "prefix": "dap_",
      "style": "snake_case",
      "examples": ["dap_common_init()", "dap_config_get()"]
    },
    "variables": {
      "prefixes": {
        "local": "l_",
        "argument": "a_",
        "static": "s_",
        "global": "g_"
      }
    }
  },
  "memory_management": {
    "allocation": {
      "preferred": ["DAP_NEW", "DAP_NEW_Z", "DAP_MALLOC"],
      "always_check": true
    }
  }
}
```

**Назначение:**
- Стандартизация стиля кода
- Правила именования
- Безопасное управление памятью
- Стандарты логирования

## 🔧 Использование

### Автоматическая загрузка контекста

```bash
# Загрузка полного контекста проекта
./context/scripts/load_full_context.sh

# Загрузка контекста конкретного модуля
./context/scripts/load_module.sh crypto

# Валидация контекста
./context/scripts/validate_context.sh
```

### Интеграция с IDE

```json
// Настройки для VS Code
{
  "dap.context.autoLoad": true,
  "dap.context.files": [
    "context/context.json",
    "context/coding_guidelines.json",
    "context/project_standards.json"
  ],
  "dap.coding.style": "context/coding_guidelines.json"
}
```

### Работа с шаблонами кода

```json
// code_templates.json
{
  "function_template": {
    "prefix": "dap_func",
    "body": [
      "/**",
      " * @brief ${1:Brief description}",
      " * @param ${2:param_name} ${3:Parameter description}",
      " * @return ${4:Return description}",
      " */",
      "int dap_${5:function_name}(${6:parameters}) {",
      "    ${0:// Implementation}",
      "}"
    ]
  }
}
```

## 📚 Документация для разработчиков

### `human_docs/architecture_guide.md`
Руководство по архитектуре DAP SDK:
- Общий обзор системы
- Взаимодействие компонентов
- Принципы проектирования
- Масштабируемость

### `human_docs/coding_standards.md`
Подробные стандарты кодирования:
- Стиль и форматирование
- Документирование кода
- Тестирование
- Code review

### `human_docs/testing_guide.md`
Руководство по тестированию:
- Модульное тестирование
- Интеграционное тестирование
- Профилирование производительности
- Автоматизация тестирования

### `human_docs/security_practices.md`
Практики безопасности:
- Безопасное кодирование
- Аудит безопасности
- Защита от уязвимостей
- Криптографические практики

### `human_docs/deployment_guide.md`
Руководство по развертыванию:
- Сборка и установка
- Конфигурация
- Мониторинг
- Обновление

## 🧪 Валидация контекста

### Автоматическая валидация

```bash
# Полная валидация контекста
./context/tests/validate_context.sh

# Валидация отдельных компонентов
./context/tests/validate_structure.sh
./context/tests/validate_standards.sh
./context/tests/validate_guidelines.sh
```

### Ручная проверка

```bash
# Проверка структуры JSON файлов
python3 -m json.tool context/context.json

# Валидация ссылок
./scripts/validate_links.py context/

# Проверка соответствия стандартам
./scripts/check_standards.py src/ context/coding_guidelines.json
```

## 🔄 Интеграция с инструментами

### Интеграция с Git

```bash
# Pre-commit хуки для проверки стандартов
#!/bin/bash
# .git/hooks/pre-commit

# Проверка соответствия coding guidelines
./context/scripts/check_coding_standards.py

# Валидация структуры проекта
./context/scripts/validate_project_structure.py

# Проверка документации
./context/scripts/validate_documentation.py
```

### Интеграция с CI/CD

```yaml
# .github/workflows/context-validation.yml
name: Context Validation
on: [push, pull_request]

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Validate Context
      run: ./context/tests/validate_context.sh
    - name: Check Coding Standards
      run: ./context/scripts/check_standards.py src/
    - name: Validate Documentation
      run: ./context/scripts/validate_docs.py docs/
```

### Интеграция с IDE

```json
// .vscode/settings.json
{
  "dap.context.enabled": true,
  "dap.context.path": "./context",
  "editor.codeActionsOnSave": {
    "source.fixAll": true
  },
  "editor.formatOnSave": true,
  "C_Cpp.clang_format_fallbackStyle": "file",
  "C_Cpp.clang_format_style": "context/coding_guidelines.json"
}
```

## 📊 Мониторинг и статистика

### Сбор метрик

```bash
# Статистика проекта
./context/scripts/project_stats.sh

# Анализ соответствия стандартам
./context/scripts/standards_compliance.py

# Метрики качества кода
./context/scripts/code_quality_metrics.py
```

### Отчеты

```json
// Пример отчета о соответствии стандартам
{
  "timestamp": "2025-01-06T10:00:00Z",
  "project": "DAP SDK",
  "standards_compliance": {
    "coding_guidelines": {
      "score": 95.2,
      "total_files": 245,
      "compliant_files": 233,
      "violations": [
        {
          "file": "src/module.c",
          "line": 42,
          "rule": "naming_convention",
          "message": "Function name should use snake_case"
        }
      ]
    },
    "documentation": {
      "score": 87.8,
      "coverage": "92.3%",
      "missing_docs": 12
    }
  }
}
```

## 🔧 Управление контекстом

### Обновление контекста

```bash
# Обновление версий
./context/scripts/update_versions.sh

# Синхронизация с репозиторием
./context/scripts/sync_context.sh

# Архивация старых версий
./context/scripts/archive_context.sh
```

### Создание нового модуля

```bash
# Создание конфигурации нового модуля
./context/scripts/create_module.sh my_module

# Добавление в индекс
./context/scripts/update_index.sh

# Валидация
./context/tests/validate_context.sh
```

## 🎯 Лучшие практики

### Организация контекста

1. **Централизация** - весь контекст в одном месте
2. **Версионирование** - отслеживание изменений
3. **Валидация** - автоматическая проверка корректности
4. **Документация** - подробное описание всех компонентов

### Работа с командами

```bash
# Обучение новых разработчиков
./context/scripts/onboard_developer.sh new_dev@example.com

# Проверка соответствия стандартам
./context/scripts/audit_standards.sh --team=all

# Генерация отчетов о качестве
./context/scripts/generate_quality_report.sh --period=monthly
```

### Непрерывное улучшение

```json
// План улучшения качества
{
  "continuous_improvement": {
    "code_quality": {
      "target_score": 95,
      "current_score": 92.3,
      "actions": [
        "Implement automated code review",
        "Add performance benchmarks",
        "Enhance error handling"
      ]
    },
    "documentation": {
      "target_coverage": 100,
      "current_coverage": 92.3,
      "actions": [
        "Complete API documentation",
        "Add usage examples",
        "Create troubleshooting guides"
      ]
    }
  }
}
```

## 📈 Метрики и KPI

### Метрики качества

- **Соответствие стандартам кодирования:** >95%
- **Покрытие документацией:** >90%
- **Процент успешных сборок:** >98%
- **Среднее время code review:** <2 часов

### Метрики производительности

- **Время загрузки контекста:** <1 секунды
- **Время валидации:** <30 секунд
- **Размер контекста:** <10 MB
- **Количество модулей:** Автоматическое обнаружение

## 🚨 Troubleshooting

### Распространенные проблемы

#### Проблема: Контекст не загружается

```bash
# Проверка структуры файлов
ls -la context/

# Валидация JSON файлов
python3 -c "import json; json.load(open('context/context.json'))"

# Проверка прав доступа
chmod +x context/scripts/*.sh
```

#### Проблема: Нарушение стандартов кодирования

```bash
# Автоматическое исправление
./context/scripts/auto_fix_standards.sh

# Ручная проверка
./context/scripts/check_standards.py --fix src/module.c
```

#### Проблема: Устаревший контекст

```bash
# Обновление до последней версии
./context/scripts/update_context.sh

# Синхронизация с репозиторием
./context/scripts/sync_from_repo.sh
```

## 🔗 Интеграция с внешними инструментами

### Интеграция с Jira/Confluence

```json
// Конфигурация интеграции
{
  "jira_integration": {
    "endpoint": "https://company.atlassian.net",
    "project_key": "DAP",
    "documentation_space": "DAPSDK",
    "auto_sync": true,
    "sync_interval": "1h"
  }
}
```

### Интеграция с SonarQube

```xml
<!-- sonar-project.properties -->
sonar.projectKey=dap-sdk
sonar.projectName=DAP SDK
sonar.projectVersion=2.3.0
sonar.sources=src/
sonar.tests=test/
sonar.sourceEncoding=UTF-8
sonar.coverage.exclusions=**/test/**,**/examples/**
sonar.cpd.exclusions=**/generated/**
```

### Интеграция с GitLab CI

```yaml
# .gitlab-ci.yml
stages:
  - validate
  - build
  - test
  - deploy

validate_context:
  stage: validate
  script:
    - ./context/tests/validate_context.sh
    - ./context/scripts/check_standards.py src/

build:
  stage: build
  script:
    - mkdir build && cd build
    - cmake .. -DENABLE_CONTEXT_VALIDATION=ON
    - make -j$(nproc)
```

## 📚 Дополнительные ресурсы

### Документация
- [Руководство по архитектуре](human_docs/architecture_guide.md)
- [Стандарты кодирования](human_docs/coding_standards.md)
- [Руководство по тестированию](human_docs/testing_guide.md)
- [Практики безопасности](human_docs/security_practices.md)

### Инструменты
- [Скрипты управления контекстом](scripts/)
- [Шаблоны кода](code_templates.json)
- [Примеры конфигураций](examples/)

### Сообщество
- [Форум разработчиков](https://forum.cellframe.net)
- [Чат в Telegram](https://t.me/cellframe_dev)
- [GitHub Issues](https://github.com/cellframe/libdap/issues)

---

## 🎯 Заключение

Модуль `context` является центральным компонентом экосистемы разработки DAP SDK. Он обеспечивает:

- ✅ **Стандартизацию** процессов разработки
- ✅ **Централизованное управление** метаданными
- ✅ **Автоматизацию** проверки качества
- ✅ **Интеграцию** с инструментами разработки
- ✅ **Непрерывное улучшение** качества кода

**🚀 Правильная настройка и использование context модуля гарантирует высокое качество и一致ность разработки проекта DAP SDK!**



