# DAP SDK MCP Server

Model Context Protocol Server для анализа DAP SDK (Decentralized Application Platform Software Development Kit).

## 📖 Описание

Этот сервер предоставляет инструменты для анализа и работы с DAP SDK через Model Context Protocol (MCP) для интеграции с AI-системами, такими как Claude Desktop и Cursor IDE.

## 🏗️ Архитектура

```
mcp_server/
├── core/                    # Основные модули
│   ├── __init__.py
│   ├── context.py          # Контекст DAP SDK проекта
│   ├── tools.py            # Инструменты анализа
│   └── server.py           # MCP сервер
├── tests/                  # Тесты (50 тестов)
│   ├── __init__.py
│   ├── conftest.py         # Конфигурация pytest
│   ├── test_context.py     # Тесты контекста
│   ├── test_tools.py       # Тесты инструментов
│   ├── test_server.py      # Тесты сервера
│   ├── test_integration.py # Интеграционные тесты
│   └── test_performance.py # Тесты производительности
├── utils/                  # Вспомогательные модули
│   ├── __init__.py
│   └── helpers.py          # Вспомогательные функции
├── mcp_server_ctl         # 🔧 Скрипт управления сервером
├── run_server.py          # Точка входа
├── run_server_daemon.py   # Демон для фонового запуска
├── pyproject.toml         # Конфигурация проекта
└── README.md              # Этот файл
```

## 🛠️ Функциональность

### Доступные инструменты

1. **`search_documentation`** - Поиск по документации DAP SDK и CellFrame SDK
2. **`get_coding_style_guide`** - Получение руководства по стилю кодирования DAP SDK
3. **`get_module_structure_info`** - Информация о структуре и назначении модулей
4. **`search_functions_and_apis`** - Поиск функций и API с рекомендациями по использованию
5. **`find_code_examples`** - Поиск примеров кода по запросу
6. **`get_project_overview`** - Получение общего обзора проектов DAP SDK и CellFrame SDK
7. **`show_sdk_architecture`** - Показать архитектуру SDK с описанием слоев и компонентов
8. **`get_build_and_integration_help`** - Помощь по сборке, установке, интеграции и использованию

## 📦 Установка

### Предварительные требования

```bash
# Установка MCP SDK
pip install mcp

# Или через pipx (рекомендуется)
pipx install mcp
```

### Из исходников

```bash
cd /home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server
pip install -e .
```

### С зависимостями для разработки

```bash
pip install -e ".[dev]"
```

## 🚀 Запуск сервера

### ⚠️ Важно: Используйте только скрипт управления!

**ВСЕГДА** используйте `mcp_server_ctl` для управления сервером. Прямой запуск через python **ЗАПРЕЩЕН** согласно правилам СЛК системы.

### Основные команды

```bash
cd /home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server

# 🚀 Запуск сервера
./mcp_server_ctl start

# 🚀 Запуск с указанием пути к проекту
./mcp_server_ctl start /path/to/dap-sdk-project

# 📊 Проверка статуса
./mcp_server_ctl status

# 📋 Просмотр логов
./mcp_server_ctl logs

# 🔄 Перезапуск сервера
./mcp_server_ctl restart

# 🛑 Остановка сервера
./mcp_server_ctl stop

# 🧪 Запуск тестов
./mcp_server_ctl test
```

### Пример запуска

```bash
$ ./mcp_server_ctl start
🚀 Запуск MCP сервера DAP SDK...
📁 Проект: /home/naeper/work/cellframe-node/dap-sdk
📝 Логи: /home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server/mcp_server.log
✅ Сервер успешно запущен (PID: 12345)
📊 Проверить статус: ./mcp_server_ctl status
📋 Посмотреть логи: ./mcp_server_ctl logs
```

## 🖥️ Интеграция с Claude Desktop

### Настройка конфигурации

Создайте или отредактируйте файл конфигурации Claude Desktop:

**macOS**: `~/Library/Application Support/Claude/claude_desktop_config.json`
**Windows**: `%APPDATA%\Claude\claude_desktop_config.json`
**Linux**: `~/.config/claude/claude_desktop_config.json`

```json
{
  "mcpServers": {
    "dap-sdk": {
      "command": "python3",
      "args": [
        "/home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server/run_server.py",
        "/home/naeper/work/cellframe-node/dap-sdk"
      ],
      "env": {
        "PYTHONPATH": "/home/naeper/.local/share/pipx/venvs/mcp/lib/python3.13/site-packages:/home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server"
      }
    }
  }
}
```

### Использование в Claude Desktop

1. **Запустите сервер** через скрипт управления:
   ```bash
   ./mcp_server_ctl start
   ```

2. **Перезапустите Claude Desktop** для загрузки конфигурации

3. **Проверьте подключение** - в интерфейсе Claude должны появиться инструменты DAP SDK

4. **Используйте инструменты**:
   ```
   Проанализируй криптографические алгоритмы в DAP SDK
   
   Найди примеры кода в проекте
   
   Покажи обзор проекта DAP SDK
   ```

## 💻 Интеграция с Cursor IDE

### Настройка через .cursorrules

Добавьте в файл `.cursorrules` в корне проекта:

```
# DAP SDK MCP Server Integration
При работе с DAP SDK используй MCP сервер для анализа:

1. Сервер управляется через: /home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server/mcp_server_ctl
2. Доступные инструменты:
   - analyze_crypto_algorithms - анализ криптографии
   - analyze_network_modules - анализ сети  
   - analyze_build_system - анализ сборки
   - find_code_examples - поиск примеров
   - analyze_security_features - анализ безопасности
   - get_project_overview - обзор проекта

3. Всегда проверяй статус сервера: ./mcp_server_ctl status
4. При ошибках смотри логи: ./mcp_server_ctl logs
```

### Настройка MCP подключения в Cursor

1. **Откройте настройки Cursor** (Cmd/Ctrl + ,)

2. **Найдите раздел "Extensions"** или "MCP Servers"

3. **Добавьте новый MCP сервер**:
   ```json
   {
     "name": "DAP SDK",
     "command": "python3",
     "args": [
       "/home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server/run_server.py"
     ],
     "env": {
       "PYTHONPATH": "/home/naeper/.local/share/pipx/venvs/mcp/lib/python3.13/site-packages:/home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server"
     }
   }
   ```

### Использование в Cursor

1. **Запустите MCP сервер**:
   ```bash
   cd /home/naeper/work/cellframe-node/dap-sdk/docs/mcp_server
   ./mcp_server_ctl start
   ```

2. **Используйте в чате Cursor**:
   ```
   @dap-sdk покажи архитектуру SDK
   
   @dap-sdk найди функции для работы с кошельками
   
   @dap-sdk как собрать проект с SSL поддержкой?
   
   @dap-sdk покажи стиль кодирования
   
   @dap-sdk найди примеры HTTP сервера
   ```

## 🧪 Тестирование

### Запуск всех тестов

```bash
./mcp_server_ctl test
```

### Ручной запуск через pytest

```bash
# Все тесты
pytest tests/ -v

# С покрытием
pytest tests/ --cov=mcp_server --cov-report=html

# Только быстрые тесты
pytest tests/ -m "not slow"

# Только интеграционные тесты
pytest tests/ -m integration

# Тесты производительности
pytest tests/ -m performance
```

### Структура тестов

- **`test_context.py`** (6 тестов) - тестирование контекста DAP SDK
- **`test_tools.py`** (9 тестов) - тестирование инструментов анализа
- **`test_server.py`** (14 тестов) - тестирование MCP сервера
- **`test_integration.py`** (12 тестов) - интеграционные тесты
- **`test_performance.py`** (9 тестов) - тесты производительности

**Всего: 50 тестов** ✅

## 🔧 Разработка

### Структура модулей

#### `core/context.py`
Класс `DAPSDKContext` управляет контекстом проекта:
- Пути к основным директориям (crypto, net, core, examples)
- Списки модулей для каждого типа
- Валидация структуры проекта

#### `core/tools.py`
Класс `DAPMCPTools` содержит инструменты анализа:
- Анализ криптографических алгоритмов
- Анализ сетевых модулей
- Анализ системы сборки
- Поиск примеров кода
- Анализ функций безопасности

#### `core/server.py`
Класс `DAPMCPServer` реализует MCP сервер:
- Обработка списка инструментов
- Выполнение инструментов
- Обработка ошибок
- Интеграция с MCP протоколом

### Добавление нового инструмента

1. **Добавить метод в `DAPMCPTools`**:
   ```python
   async def analyze_new_feature(self) -> Dict[str, Any]:
       """Анализ новой функциональности"""
       # Ваша реализация
       return result
   ```

2. **Зарегистрировать в `DAPMCPServer.handle_list_tools()`**:
   ```python
   Tool(
       name="analyze_new_feature",
       description="Анализ новой функциональности DAP SDK",
       inputSchema={"type": "object", "properties": {}, "required": []}
   )
   ```

3. **Добавить обработку в `DAPMCPServer.handle_call_tool()`**:
   ```python
   elif name == "analyze_new_feature":
       result = await self.tools.analyze_new_feature()
       return [TextContent(type="text", text=json.dumps(result, indent=2, ensure_ascii=False))]
   ```

4. **Написать тесты** в соответствующем файле

### Стиль кода

```bash
# Форматирование
black .
isort .

# Проверка типов
mypy .
```

## 🔍 Мониторинг и отладка

### Проверка состояния

```bash
# Статус сервера
./mcp_server_ctl status

# Последние логи
./mcp_server_ctl logs | tail -20

# Мониторинг в реальном времени
./mcp_server_ctl logs
```

### Типичные проблемы

#### Сервер не запускается
1. Проверьте зависимости: `pip list | grep mcp`
2. Проверьте права доступа: `ls -la mcp_server_ctl`
3. Посмотрите логи: `./mcp_server_ctl logs`

#### Сервер падает
1. Проверьте логи на ошибки
2. Убедитесь что путь к проекту корректный
3. Проверьте что порты не заняты

#### Claude Desktop не видит сервер
1. Проверьте конфигурацию `claude_desktop_config.json`
2. Убедитесь что сервер запущен: `./mcp_server_ctl status`
3. Перезапустите Claude Desktop

#### Cursor не подключается
1. Проверьте настройки MCP в Cursor
2. Убедитесь что пути корректные
3. Проверьте переменные окружения

## 📊 Производительность

### Бенчмарки

- **Анализ криптографии**: < 1 секунды
- **Поиск примеров**: < 2 секунд  
- **Анализ сборки**: < 0.5 секунды
- **Обзор проекта**: < 0.1 секунды

### Оптимизация

- Результаты кешируются для повторных запросов
- Асинхронная обработка для параллельных операций
- Ленивая загрузка больших файлов

## 🔒 Безопасность

- Сервер работает только с локальными файлами
- Нет сетевых подключений наружу
- Валидация всех путей к файлам
- Ограничение доступа к системным директориям

## 📄 Лицензия

MIT License

## 👥 Авторы

DAP SDK Team

## 🆘 Поддержка

При проблемах:

1. **Проверьте статус**: `./mcp_server_ctl status`
2. **Посмотрите логи**: `./mcp_server_ctl logs`
3. **Запустите тесты**: `./mcp_server_ctl test`
4. **Перезапустите сервер**: `./mcp_server_ctl restart`

## 🔗 Полезные ссылки

- [Model Context Protocol](https://modelcontextprotocol.io/)
- [Claude Desktop](https://claude.ai/desktop)
- [Cursor IDE](https://cursor.sh/)
- [DAP SDK Documentation](../README.md)