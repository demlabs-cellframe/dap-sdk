# DAP SDK - Документация

## Обзор

DAP SDK (Decentralized Application Platform Software Development Kit) - это набор инструментов для разработки децентрализованных приложений с поддержкой квантово-устойчивой криптографии и блокчейн инфраструктуры.

## Структура документации

- [Архитектура системы](./architecture.md) - Общая архитектура и принципы работы
- [Модули](./modules/) - Документация по отдельным модулям
- [API Reference](./api/) - Справочник по API
- [Примеры использования](./examples/) - Практические примеры кода
- [Скрипты запуска](./scripts/) - Скрипты для запуска MCP серверов

## Быстрый старт

### Установка

```bash
# Клонирование репозитория
git clone <repository-url>
cd dap-sdk

# Сборка
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### MCP сервер для интеграции с AI

DAP SDK включает Model Context Protocol (MCP) сервер для интеграции с AI-системами:

```bash
# Запуск MCP сервера
./docs/scripts/start_mcp_servers.sh start dap_sdk

# Или запуск всех MCP серверов
./docs/scripts/start_mcp_servers.sh start all
```

MCP сервер предоставляет инструменты для:
- Анализа криптографических алгоритмов
- Изучения сетевых модулей
- Анализа системы сборки
- Поиска примеров кода
- Анализа функций безопасности

### Базовое использование

```c
#include "dap_common.h"
#include "dap_crypto.h"

int main() {
    // Инициализация DAP SDK
    dap_init();

    // Ваш код здесь

    // Очистка ресурсов
    dap_deinit();
    return 0;
}
```

## Основные модули

### Core (Ядро)
- **Путь**: `core/`
- **Описание**: Основная функциональность DAP SDK
- **Компоненты**: Общие утилиты, платформо-специфичные реализации
- **Документация**: [Подробное описание](./modules/core.md)

### Crypto (Криптография)
- **Путь**: `crypto/`
- **Описание**: Криптографические компоненты и алгоритмы
- **Алгоритмы**: Kyber, Falcon, SPHINCS+, Dilithium, Bliss, Chipmunk
- **Документация**: [Криптографические модули](./modules/crypto.md)

### Net (Сеть)
- **Путь**: `net/`
- **Описание**: Сетевые компоненты и коммуникация
- **Серверы**: HTTP, JSON-RPC, DNS, Encryption, Notification
- **Документация**: [Сетевая архитектура](./modules/net.md)

### Global DB (Глобальная база данных)
- **Путь**: `global-db/`
- **Описание**: Система управления данными
- **Драйверы**: MDBX, PostgreSQL, SQLite

## Требования

- **Компилятор**: GCC 7.0+ или Clang 5.0+
- **CMake**: 3.10+
- **Зависимости**: 
  - libmdbx
  - json-c
  - OpenSSL (опционально)

## Лицензия

GNU General Public License v3.0

## Поддержка

- **Документация**: [Wiki](https://wiki.demlabs.net)
- **Issues**: [GitLab Issues](https://gitlab.demlabs.net/dap/dap-sdk/-/issues)
- **Сообщество**: [Telegram](https://t.me/cellframe)
