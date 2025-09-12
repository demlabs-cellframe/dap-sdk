# dap_config.h/c - Система конфигурации DAP SDK

## Обзор

Модуль `dap_config` предоставляет мощную и гибкую систему для работы с конфигурационными файлами в DAP SDK. Поддерживает различные типы данных, секции конфигурации, массивы и обеспечивает безопасную работу с настройками приложений.

## Основные возможности

- **Многоуровневая структура**: Поддержка секций и параметров
- **Различные типы данных**: boolean, integer, string, arrays, double
- **Глобальная и локальная конфигурация**: Поддержка нескольких конфигурационных файлов
- **Безопасность типов**: Строгая типизация с проверками
- **Расширяемость**: Легкое добавление новых типов данных
- **Кроссплатформенность**: Работа на Windows, Linux, macOS
- **Пути к файлам**: Автоматическое разрешение относительных путей

## Структура данных

### Типы конфигурационных элементов

```c
typedef enum {
    DAP_CONFIG_ITEM_UNKNOWN = '\0',  // Неизвестный тип
    DAP_CONFIG_ITEM_ARRAY   = 'a',   // Массив строк
    DAP_CONFIG_ITEM_BOOL    = 'b',   // Логическое значение
    DAP_CONFIG_ITEM_DECIMAL = 'd',   // Целое число или дробное
    DAP_CONFIG_ITEM_STRING  = 's'    // Строка
} dap_config_item_type_t;
```

### Структура конфигурации

```c
typedef struct dap_config_item {
    char type;              // Тип элемента ('a', 'b', 'd', 's')
    char *name;             // Имя параметра
    union {
        bool val_bool;      // Логическое значение
        char *val_str;      // Строковое значение
        char **val_arr;     // Массив строк
        int64_t val_int;    // Целое значение
    } val;
    UT_hash_handle hh;      // Хеш-таблица для быстрого поиска
} dap_config_item_t;

typedef struct dap_conf {
    char *path;                    // Путь к конфигурационному файлу
    dap_config_item_t *items;      // Хеш-таблица элементов
    UT_hash_handle hh;             // Для глобальной таблицы конфигураций
} dap_config_t;
```

## Глобальные переменные

### Основная конфигурация
```c
extern dap_config_t *g_config;  // Глобальная конфигурация приложения
```

## API Reference

### Инициализация и деинициализация

#### dap_config_init()

```c
int dap_config_init(const char *a_configs_path);
```

**Описание**: Инициализирует систему конфигурации с указанным путем к директории конфигурационных файлов.

**Параметры:**
- `a_configs_path` - путь к директории с конфигурационными файлами

**Возвращает:**
- `0` - успешная инициализация
- `-1` - пустой путь
- `-2` - недопустимый путь

**Пример:**
```c
#include "dap_config.h"

int main(int argc, char *argv[]) {
    // Инициализация с путем к конфигурациям
    if (dap_config_init("./configs") != 0) {
        fprintf(stderr, "Failed to initialize config system\n");
        return 1;
    }

    // Работа с конфигурацией...
    // ...

    return 0;
}
```

#### dap_config_deinit()

```c
void dap_config_deinit(void);
```

**Описание**: Деинициализирует систему конфигурации, освобождая все ресурсы.

**Пример:**
```c
// Корректное завершение работы
dap_config_deinit();
```

### Работа с конфигурационными файлами

#### dap_config_open()

```c
dap_config_t *dap_config_open(const char *a_config_filename);
```

**Описание**: Открывает и загружает конфигурационный файл.

**Параметры:**
- `a_config_filename` - имя конфигурационного файла (относительно configs_path)

**Возвращает:** Указатель на структуру конфигурации или NULL при ошибке.

**Пример:**
```c
// Открываем конфигурационный файл
dap_config_t *config = dap_config_open("application.conf");
if (!config) {
    fprintf(stderr, "Failed to open config file\n");
    return 1;
}

// Работа с конфигурацией...

// Закрываем конфигурацию
dap_config_close(config);
```

#### dap_config_close()

```c
void dap_config_close(dap_config_t *a_config);
```

**Описание**: Закрывает конфигурацию и освобождает ресурсы.

**Параметры:**
- `a_config` - указатель на конфигурацию

**Пример:**
```c
dap_config_close(config);
config = NULL;
```

### Получение пути к конфигурациям

#### dap_config_path()

```c
const char *dap_config_path(void);
```

**Описание**: Возвращает текущий путь к директории конфигурационных файлов.

**Возвращает:** Строка с путем или NULL.

**Пример:**
```c
const char *config_path = dap_config_path();
if (config_path) {
    printf("Config path: %s\n", config_path);
}
```

### Получение типа параметра

#### dap_config_get_item_type()

```c
dap_config_item_type_t dap_config_get_item_type(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name
);
```

**Описание**: Возвращает тип указанного параметра конфигурации.

**Параметры:**
- `a_config` - конфигурация
- `a_section` - секция конфигурации
- `a_item_name` - имя параметра

**Возвращает:** Тип параметра или `DAP_CONFIG_ITEM_UNKNOWN`.

**Пример:**
```c
dap_config_item_type_t type = dap_config_get_item_type(config, "database", "port");
switch (type) {
    case DAP_CONFIG_ITEM_STRING:
        printf("Port is a string\n");
        break;
    case DAP_CONFIG_ITEM_DECIMAL:
        printf("Port is a number\n");
        break;
    default:
        printf("Port type is unknown\n");
}
```

## Получение значений параметров

### Логические значения

#### dap_config_get_item_bool_default()

```c
bool dap_config_get_item_bool_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    bool a_default
);
```

**Описание**: Получает логическое значение параметра с значением по умолчанию.

**Параметры:**
- `a_config` - конфигурация
- `a_section` - секция
- `a_item_name` - имя параметра
- `a_default` - значение по умолчанию

**Возвращает:** Значение параметра или значение по умолчанию.

**Пример:**
```c
bool debug_mode = dap_config_get_item_bool_default(
    config, "application", "debug", false
);

if (debug_mode) {
    printf("Debug mode is enabled\n");
}
```

#### Упрощенная версия

```c
#define dap_config_get_item_bool(a_conf, a_path, a_item) \
    dap_config_get_item_bool_default(a_conf, a_path, a_item, false)
```

### Целые числа

#### Для различных размеров

```c
// 16-битные числа
#define dap_config_get_item_uint16(a_conf, a_path, a_item) \
    (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint16_default(a_conf, a_path, a_item, a_default) \
    (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// 32-битные числа
#define dap_config_get_item_uint32(a_conf, a_path, a_item) \
    (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint32_default(a_conf, a_path, a_item, a_default) \
    (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// 64-битные числа
#define dap_config_get_item_uint64(a_conf, a_path, a_item) \
    (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint64_default(a_conf, a_path, a_item, a_default) \
    (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// Знаковые числа
#define dap_config_get_item_int16(a_conf, a_path, a_item) \
    (int16_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)

#define dap_config_get_item_int32(a_conf, a_path, a_item) \
    (int32_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)

#define dap_config_get_item_int64(a_conf, a_path, a_item) \
    (int64_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)
```

**Пример использования:**
```c
// Получение номера порта
uint16_t port = dap_config_get_item_uint16_default(
    config, "server", "port", 8080
);

// Получение максимального размера
uint64_t max_size = dap_config_get_item_uint64_default(
    config, "limits", "max_file_size", 1048576  // 1MB default
);

// Получение timeout с отрицательным значением по умолчанию
int32_t timeout = dap_config_get_item_int32_default(
    config, "network", "timeout", -1
);
```

### Строки

#### dap_config_get_item_str_default()

```c
const char *dap_config_get_item_str_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    const char *a_default
);
```

**Описание**: Получает строковое значение параметра.

**Параметры:**
- `a_config` - конфигурация
- `a_section` - секция
- `a_item_name` - имя параметра
- `a_default` - значение по умолчанию

**Возвращает:** Указатель на строку или значение по умолчанию.

**Пример:**
```c
const char *db_host = dap_config_get_item_str_default(
    config, "database", "host", "localhost"
);

const char *log_level = dap_config_get_item_str_default(
    config, "logging", "level", "INFO"
);
```

#### Упрощенная версия

```c
#define dap_config_get_item_str(a_conf, a_path, a_item) \
    dap_config_get_item_str_default(a_conf, a_path, a_item, NULL)
```

### Пути к файлам

#### dap_config_get_item_str_path_default()

```c
char *dap_config_get_item_str_path_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    const char *a_default
);
```

**Описание**: Получает путь к файлу, автоматически разрешая относительные пути относительно директории конфигураций.

**Возвращает:** Полный путь к файлу (нужно освобождать с помощью `free()`).

**Пример:**
```c
char *log_file = dap_config_get_item_str_path_default(
    config, "logging", "file", "logs/app.log"
);

if (log_file) {
    printf("Log file path: %s\n", log_file);
    // Используем путь...
    free(log_file);
}
```

#### Упрощенные версии

```c
#define dap_config_get_item_path(a_conf, a_path, a_item) \
    dap_config_get_item_str_path_default(a_conf, a_path, a_item, NULL)

#define dap_config_get_item_path_default(a_conf, a_path, a_item, a_default) \
    dap_config_get_item_str_path_default(a_conf, a_path, a_item, a_default)
```

### Дробные числа

#### dap_config_get_item_double_default()

```c
double dap_config_get_item_double_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    double a_default
);
```

**Описание**: Получает дробное значение параметра.

**Пример:**
```c
double threshold = dap_config_get_item_double_default(
    config, "processing", "threshold", 0.75
);
```

#### Упрощенная версия

```c
#define dap_config_get_item_double(a_conf, a_path, a_item) \
    dap_config_get_item_double_default(a_conf, a_path, a_item, 0)
```

### Массивы строк

#### dap_config_get_array_str()

```c
const char** dap_config_get_array_str(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    uint16_t *a_array_length
);
```

**Описание**: Получает массив строк из конфигурации.

**Параметры:**
- `a_config` - конфигурация
- `a_section` - секция
- `a_item_name` - имя параметра
- `a_array_length` - указатель для сохранения длины массива

**Возвращает:** Указатель на массив строк (NULL-завершенный).

**Пример:**
```c
uint16_t array_length;
const char **servers = dap_config_get_array_str(
    config, "network", "servers", &array_length
);

if (servers) {
    printf("Found %d servers:\n", array_length);
    for (uint16_t i = 0; i < array_length; i++) {
        printf("  %s\n", servers[i]);
    }
    // Массив servers автоматически освобождается системой
}
```

#### dap_config_get_item_str_path_array()

```c
char **dap_config_get_item_str_path_array(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    uint16_t *a_array_length
);
```

**Описание**: Получает массив путей к файлам.

**Возвращает:** Массив полных путей (нужно освобождать с помощью `dap_config_get_item_str_path_array_free()`).

**Пример:**
```c
uint16_t paths_count;
char **config_files = dap_config_get_item_str_path_array(
    config, "includes", "files", &paths_count
);

if (config_files) {
    for (uint16_t i = 0; i < paths_count; i++) {
        printf("Config file: %s\n", config_files[i]);
    }

    // Освобождаем массив
    dap_config_get_item_str_path_array_free(config_files, paths_count);
}
```

#### Освобождение массива путей

```c
void dap_config_get_item_str_path_array_free(
    char **a_paths_array,
    uint16_t a_array_length
);
```

## Примеры использования

### Пример 1: Базовая работа с конфигурацией

```c
#include "dap_config.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    // Инициализация системы конфигурации
    if (dap_config_init("./configs") != 0) {
        fprintf(stderr, "Failed to initialize config system\n");
        return 1;
    }

    // Открываем основную конфигурацию
    dap_config_t *config = dap_config_open("application.conf");
    if (!config) {
        fprintf(stderr, "Failed to open config file\n");
        return 1;
    }

    // Получаем базовые параметры
    const char *app_name = dap_config_get_item_str_default(
        config, "application", "name", "MyApp"
    );

    uint16_t port = dap_config_get_item_uint16_default(
        config, "server", "port", 8080
    );

    bool debug = dap_config_get_item_bool_default(
        config, "application", "debug", false
    );

    printf("Application: %s\n", app_name);
    printf("Port: %d\n", port);
    printf("Debug mode: %s\n", debug ? "enabled" : "disabled");

    // Работа с путями
    char *log_file = dap_config_get_item_path_default(
        config, "logging", "file", "logs/app.log"
    );

    if (log_file) {
        printf("Log file: %s\n", log_file);
        free(log_file);
    }

    // Закрываем конфигурацию
    dap_config_close(config);

    // Деинициализация
    dap_config_deinit();

    return 0;
}
```

### Пример 2: Работа с массивами

```c
#include "dap_config.h"

void process_server_list(dap_config_t *config) {
    uint16_t server_count;
    const char **servers = dap_config_get_array_str(
        config, "network", "servers", &server_count
    );

    if (servers && server_count > 0) {
        printf("Connecting to %d servers:\n", server_count);

        for (uint16_t i = 0; i < server_count; i++) {
            printf("  Server %d: %s\n", i + 1, servers[i]);
            // Подключаемся к серверу...
        }
    } else {
        printf("No servers configured, using default\n");
    }
}

void load_config_files(dap_config_t *config) {
    uint16_t file_count;
    char **config_files = dap_config_get_item_str_path_array(
        config, "includes", "files", &file_count
    );

    if (config_files && file_count > 0) {
        printf("Loading %d additional config files:\n", file_count);

        for (uint16_t i = 0; i < file_count; i++) {
            printf("  Loading: %s\n", config_files[i]);
            // Загружаем дополнительную конфигурацию...
        }

        // Освобождаем ресурсы
        dap_config_get_item_str_path_array_free(config_files, file_count);
    }
}
```

### Пример 3: Комплексная конфигурация сервера

```c
#include "dap_config.h"
#include <stdlib.h>

typedef struct {
    const char *host;
    uint16_t port;
    const char *database;
    const char *user;
    const char *password;
    bool ssl_enabled;
    uint32_t max_connections;
    double timeout;
} db_config_t;

db_config_t *load_database_config(dap_config_t *config) {
    db_config_t *db_config = calloc(1, sizeof(db_config_t));

    if (!db_config) return NULL;

    // Загружаем параметры базы данных
    db_config->host = dap_config_get_item_str_default(
        config, "database", "host", "localhost"
    );

    db_config->port = dap_config_get_item_uint16_default(
        config, "database", "port", 5432
    );

    db_config->database = dap_config_get_item_str_default(
        config, "database", "name", "myapp"
    );

    db_config->user = dap_config_get_item_str_default(
        config, "database", "user", "app_user"
    );

    db_config->password = dap_config_get_item_str_default(
        config, "database", "password", ""
    );

    db_config->ssl_enabled = dap_config_get_item_bool_default(
        config, "database", "ssl", false
    );

    db_config->max_connections = dap_config_get_item_uint32_default(
        config, "database", "max_connections", 10
    );

    db_config->timeout = dap_config_get_item_double_default(
        config, "database", "timeout", 30.0
    );

    return db_config;
}

void print_database_config(const db_config_t *config) {
    printf("Database Configuration:\n");
    printf("  Host: %s:%d\n", config->host, config->port);
    printf("  Database: %s\n", config->database);
    printf("  User: %s\n", config->user);
    printf("  SSL: %s\n", config->ssl_enabled ? "enabled" : "disabled");
    printf("  Max connections: %u\n", config->max_connections);
    printf("  Timeout: %.1f seconds\n", config->timeout);
}

int main() {
    if (dap_config_init("./configs") != 0) {
        return 1;
    }

    dap_config_t *config = dap_config_open("database.conf");
    if (!config) {
        return 1;
    }

    db_config_t *db_config = load_database_config(config);
    if (db_config) {
        print_database_config(db_config);
        free(db_config);
    }

    dap_config_close(config);
    dap_config_deinit();

    return 0;
}
```

## Формат конфигурационных файлов

### Структура INI-файла

```ini
[application]
name = MyApplication
version = 1.0.0
debug = true

[server]
host = 0.0.0.0
port = 8080
max_connections = 100

[database]
host = localhost
port = 5432
name = myapp_db
user = app_user
password = secret123
ssl = true
max_connections = 20
timeout = 30.5

[logging]
level = DEBUG
file = logs/application.log

[network]
servers = server1.example.com, server2.example.com, server3.example.com

[includes]
files = common.conf, development.conf
```

### Поддерживаемые типы значений

1. **Строки**: Любые символы в кавычках или без
2. **Числа**: Целые и дробные числа
3. **Логические**: `true`, `false`, `1`, `0`, `yes`, `no`
4. **Массивы**: Значения через запятую
5. **Пути**: Относительные пути разрешаются автоматически

## Производительность

### Оптимизации

1. **Хеш-таблицы**: Быстрый поиск параметров O(1)
2. **Ленивая загрузка**: Параметры загружаются по требованию
3. **Кэширование**: Разобранные значения кэшируются
4. **Минимальные аллокации**: Переиспользование структур

### Бенчмарки производительности

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| `dap_config_open()` | ~5-10 ms | Загрузка типичного файла |
| `dap_config_get_item_str()` | ~1-2 μs | Получение строкового параметра |
| `dap_config_get_item_uint32()` | ~1-2 μs | Получение числового параметра |
| Поиск в массиве | ~5-10 μs | Линейный поиск |

### Факторы влияния

- **Размер файла**: Большие файлы загружаются медленнее
- **Количество параметров**: Влияет на использование памяти
- **Частота обращений**: Кэшированные значения работают быстрее
- **Тип данных**: Числовые значения обрабатываются быстрее строк

## Безопасность

### Защита от типичных проблем

```c
// ✅ Правильная обработка ошибок
const char *get_config_string_safe(dap_config_t *config,
                                   const char *section,
                                   const char *key,
                                   const char *default_val) {
    if (!config || !section || !key) {
        return default_val;
    }

    const char *value = dap_config_get_item_str_default(
        config, section, key, default_val
    );

    // Проверка на NULL для строковых значений
    return value ? value : default_val;
}

// ❌ Уязвимый код
const char *get_config_string_unsafe(dap_config_t *config,
                                     const char *section,
                                     const char *key) {
    // Не проверяем входные параметры
    // Не обрабатываем NULL значения
    return dap_config_get_item_str(config, section, key);
}
```

### Рекомендации по безопасности

1. **Валидация входных данных**: Всегда проверяйте параметры перед использованием
2. **Обработка ошибок**: Проверяйте возвращаемые значения всех функций
3. **Освобождение памяти**: Освобождайте память для путей и массивов
4. **Ограничения значений**: Проверяйте диапазоны числовых значений
5. **Санитизация путей**: Избегайте опасных символов в путях к файлам

### Работа с чувствительными данными

```c
// Для паролей и секретных данных используйте переменные окружения
const char *get_password_secure(dap_config_t *config) {
    // Сначала проверяем переменную окружения
    const char *env_password = getenv("APP_PASSWORD");
    if (env_password && *env_password) {
        return env_password;
    }

    // Fallback к конфигурационному файлу
    return dap_config_get_item_str_default(
        config, "security", "password", ""
    );
}
```

## Лучшие практики

### 1. Организация конфигурационных файлов

```ini
# Разделяйте конфигурацию по логическим секциям
[application]
name = MyApp
version = 1.0.0

[database]
host = localhost
port = 5432

[logging]
level = INFO
file = logs/app.log

[security]
ssl_enabled = true
cert_file = certs/server.crt
```

### 2. Использование значений по умолчанию

```c
// Всегда предоставляйте разумные значения по умолчанию
uint16_t port = dap_config_get_item_uint16_default(
    config, "server", "port", 8080
);

const char *host = dap_config_get_item_str_default(
    config, "server", "host", "0.0.0.0"
);

bool debug = dap_config_get_item_bool_default(
    config, "application", "debug", false
);
```

### 3. Работа с путями

```c
// Используйте автоматическое разрешение путей
char *config_file = dap_config_get_item_path(
    config, "includes", "main_config"
);

char *log_dir = dap_config_get_item_path_default(
    config, "logging", "directory", "logs"
);

if (config_file) {
    printf("Config file: %s\n", config_file);
    free(config_file);
}

if (log_dir) {
    printf("Log directory: %s\n", log_dir);
    free(log_dir);
}
```

### 4. Обработка массивов

```c
// Корректная работа с массивами
uint16_t server_count;
const char **servers = dap_config_get_array_str(
    config, "network", "servers", &server_count
);

if (servers && server_count > 0) {
    for (uint16_t i = 0; i < server_count; i++) {
        printf("Server %d: %s\n", i + 1, servers[i]);
    }
    // Массив servers освобождается автоматически
}

// Для массивов путей
uint16_t file_count;
char **files = dap_config_get_item_str_path_array(
    config, "includes", "files", &file_count
);

if (files) {
    // Работа с файлами...
    dap_config_get_item_str_path_array_free(files, file_count);
}
```

### 5. Структурирование кода

```c
// Создавайте отдельные функции для загрузки конфигурации
typedef struct {
    uint16_t port;
    const char *host;
    bool ssl_enabled;
    const char **allowed_ips;
    uint16_t ip_count;
} server_config_t;

server_config_t *load_server_config(dap_config_t *config) {
    server_config_t *srv_config = calloc(1, sizeof(server_config_t));

    srv_config->port = dap_config_get_item_uint16_default(
        config, "server", "port", 8080
    );

    srv_config->host = dap_config_get_item_str_default(
        config, "server", "host", "0.0.0.0"
    );

    srv_config->ssl_enabled = dap_config_get_item_bool_default(
        config, "server", "ssl", false
    );

    srv_config->allowed_ips = dap_config_get_array_str(
        config, "security", "allowed_ips", &srv_config->ip_count
    );

    return srv_config;
}
```

## Расширение системы

### Добавление новых типов данных

```c
// Пример добавления поддержки для пользовательских типов
typedef enum {
    CONFIG_TYPE_CUSTOM = 'c'
} custom_config_types_t;

// Функция парсинга пользовательского типа
bool parse_custom_type(const char *str, custom_type_t *result) {
    // Логика парсинга...
    return true;
}

// Использование в коде
custom_type_t custom_value;
const char *str_value = dap_config_get_item_str(
    config, "custom", "value"
);

if (str_value && parse_custom_type(str_value, &custom_value)) {
    // Используем custom_value
}
```

## Заключение

Модуль `dap_config` предоставляет мощную и гибкую систему конфигурации для DAP SDK:

- **Широкая поддержка типов данных**: boolean, integer, string, arrays, paths
- **Многоуровневая структура**: Секции и параметры для организации настроек
- **Безопасность и надежность**: Строгая типизация и проверки ошибок
- **Производительность**: Оптимизированные структуры данных и кэширование
- **Кроссплатформенность**: Работа на всех поддерживаемых платформах
- **Расширяемость**: Легкое добавление новых типов и функций

Система конфигурации DAP SDK является фундаментальным компонентом, обеспечивающим гибкую и надежную работу приложений с настройками.

Для получения дополнительной информации смотрите:
- `dap_config.h` - полный API конфигурационной системы
- Примеры в директории `examples/config/`
- Документацию по формату конфигурационных файлов
