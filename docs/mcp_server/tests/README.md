# Тесты MCP сервера DAP SDK

Этот каталог содержит полный набор тестов для MCP сервера DAP SDK.

## Структура тестов

```
tests/
├── __init__.py              # Пакет тестов
├── conftest.py              # Конфигурация pytest и фикстуры
├── test_context.py          # Тесты класса DAPSDKContext
├── test_tools.py            # Тесты класса DAPMCPTools
├── test_server.py          # Тесты класса DAPMCPServer
├── test_integration.py      # Интеграционные тесты
├── test_performance.py      # Тесты производительности
└── README.md               # Этот файл
```

## Типы тестов

### 1. Модульные тесты (`test_context.py`, `test_tools.py`, `test_server.py`)
- Тестируют отдельные компоненты изолированно
- Быстрые и надежные
- Покрывают все публичные методы и edge cases

### 2. Интеграционные тесты (`test_integration.py`)
- Тестируют взаимодействие между компонентами
- Проверяют end-to-end сценарии
- Включают тесты обработки ошибок

### 3. Тесты производительности (`test_performance.py`)
- Измеряют время выполнения операций
- Тестируют масштабируемость
- Проверяют использование памяти

## Запуск тестов

### Все тесты
```bash
cd /home/naeper/work/cellframe-node/dap-sdk/docs/scripts
python -m pytest tests/
```

### Только модульные тесты
```bash
python -m pytest tests/test_context.py tests/test_tools.py tests/test_server.py
```

### Только интеграционные тесты
```bash
python -m pytest tests/test_integration.py
```

### Только тесты производительности
```bash
python -m pytest tests/test_performance.py
```

### С дополнительной информацией
```bash
python -m pytest tests/ -v
```

### С покрытием кода
```bash
python -m pytest tests/ --cov=mcp_server --cov-report=html
```

### Только быстрые тесты (исключая медленные)
```bash
python -m pytest tests/ -m "not slow"
```

## Структура тестов

### test_context.py
- ✅ Инициализация контекста
- ✅ Содержимое списков модулей
- ✅ Конвертация путей
- ✅ Обработка различных типов путей

### test_tools.py
- ✅ Анализ криптографических алгоритмов
- ✅ Анализ сетевых модулей
- ✅ Анализ системы сборки
- ✅ Поиск примеров кода
- ✅ Извлечение описаний из комментариев
- ✅ Анализ функций безопасности
- ✅ Обработка отсутствующих файлов

### test_server.py
- ✅ Получение списка инструментов
- ✅ Вызов всех инструментов
- ✅ Обработка неизвестных инструментов
- ✅ Обработка ошибок
- ✅ Структура ответов
- ✅ Схемы входных данных

### test_integration.py
- ✅ Полный рабочий процесс
- ✅ Взаимодействие компонентов
- ✅ Обработка ошибок в интеграции
- ✅ Консистентность данных
- ✅ Валидация структуры проекта

### test_performance.py
- ✅ Производительность анализа
- ✅ Масштабируемость
- ✅ Конкурентное выполнение
- ✅ Использование памяти
- ✅ Обработка больших файлов

## Фикстуры

В `conftest.py` определены следующие фикстуры:

- `temp_dir`: Временная директория для тестов
- `server`: Экземпляр DAPMCPServer
- `context`: Экземпляр DAPSDKContext
- `tools`: Экземпляр DAPMCPTools
- `sample_project_structure`: Пример структуры проекта
- `large_project_structure`: Большая структура для нагрузочных тестов

## Маркеры

- `@pytest.mark.slow`: Медленные тесты (тесты производительности)
- `@pytest.mark.integration`: Интеграционные тесты
- `@pytest.mark.performance`: Тесты производительности

## Требования

- Python 3.7+
- pytest
- pytest-asyncio (для асинхронных тестов)
- pytest-cov (для покрытия кода)

## Установка зависимостей для тестов

```bash
pip install pytest pytest-asyncio pytest-cov
```

## Запуск с профилированием

```bash
python -m pytest tests/ --profile-svg
```

## Непрерывная интеграция

Тесты можно запускать в CI/CD пайплайнах:

```yaml
- name: Run tests
  run: |
    cd dap-sdk/docs/scripts
    python -m pytest tests/ --cov=mcp_server --cov-report=xml
```

## Отчеты о покрытии

После запуска тестов с `--cov` генерируется отчет о покрытии кода:
- HTML отчет: `htmlcov/index.html`
- XML отчет для CI: `coverage.xml`
