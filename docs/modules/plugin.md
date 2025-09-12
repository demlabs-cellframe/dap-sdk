# DAP Plugin Module (dap_plugin.h)

## Обзор

Модуль `dap_plugin` предоставляет расширяемую систему плагинов для DAP SDK. Он позволяет динамически загружать и выгружать функциональные модули во время выполнения, обеспечивая:

- **Динамическую загрузку** - плагины загружаются без перезапуска приложения
- **Зависимости между плагинами** - автоматическое разрешение зависимостей
- **Типизация плагинов** - поддержка разных типов плагинов
- **Горячая замена** - обновление плагинов без остановки системы
- **Изоляция** - каждый плагин работает в своем контексте

## Архитектурная роль

Система плагинов является ключевым элементом расширяемости DAP:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK Core  │───▶│  Plugin System  │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Базовые   │             │Динамич. │
    │модули    │             │плагины  │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Статич.   │◄────────────►│Runtime  │
    │сборка    │             │загрузка │
    └─────────┘             └─────────┘
```

## Основные компоненты

### 1. Манифест плагина
```c
typedef struct dap_plugin_manifest {
    char name[64];                 // Имя плагина
    char *version;                 // Версия
    char *author;                  // Автор
    char *description;             // Описание

    char *type;                    // Тип плагина
    const char *path;              // Путь к директории
    dap_config_t *config;          // Конфигурация

    // Зависимости
    struct dap_plugin_manifest_dependence *dependencies;
    char **dependencies_names;
    size_t dependencies_count;

    // Параметры
    size_t params_count;
    char **params;

    // Настройки
    bool is_builtin;               // Встроенный плагин

    UT_hash_handle hh;             // Для хэш-таблицы
} dap_plugin_manifest_t;
```

### 2. Зависимости плагинов
```c
typedef struct dap_plugin_manifest_dependence {
    char name[64];                 // Имя зависимости
    dap_plugin_manifest_t *manifest; // Ссылка на манифест
    UT_hash_handle hh;             // Для хэш-таблицы
} dap_plugin_manifest_dependence_t;
```

### 3. Типы плагинов
```c
typedef struct dap_plugin_type_callbacks {
    dap_plugin_type_callback_load_t load;     // Callback загрузки
    dap_plugin_type_callback_unload_t unload; // Callback выгрузки
} dap_plugin_type_callbacks_t;
```

## Статусы плагинов

```c
typedef enum dap_plugin_status {
    STATUS_RUNNING,  // Плагин запущен
    STATUS_STOPPED,  // Плагин остановлен
    STATUS_NONE      // Плагин не найден
} dap_plugin_status_t;
```

## Основные функции

### Инициализация и управление

#### `dap_plugin_init()`
```c
int dap_plugin_init(const char *a_root_path);
```

Инициализирует систему плагинов.

**Параметры:**
- `a_root_path` - корневой путь для поиска плагинов

**Возвращаемые значения:**
- `0` - успешная инициализация
- `-1` - ошибка инициализации

#### `dap_plugin_deinit()`
```c
void dap_plugin_deinit();
```

Деинициализирует систему плагинов.

### Управление типами плагинов

#### `dap_plugin_type_create()`
```c
int dap_plugin_type_create(const char *a_name,
                          dap_plugin_type_callbacks_t *a_callbacks);
```

Создает новый тип плагина с callback функциями.

**Параметры:**
- `a_name` - имя типа плагина
- `a_callbacks` - структура с callback функциями

**Типы callback функций:**
```c
typedef int (*dap_plugin_type_callback_load_t)(
    dap_plugin_manifest_t *a_manifest,
    void **a_pvt_data,
    char **a_error_str);

typedef int (*dap_plugin_type_callback_unload_t)(
    dap_plugin_manifest_t *a_manifest,
    void *a_pvt_data,
    char **a_error_str);
```

### Управление жизненным циклом плагинов

#### `dap_plugin_start_all()`
```c
void dap_plugin_start_all();
```

Запускает все загруженные плагины.

#### `dap_plugin_stop_all()`
```c
void dap_plugin_stop_all();
```

Останавливает все запущенные плагины.

#### `dap_plugin_start()`
```c
int dap_plugin_start(const char *a_name);
```

Запускает конкретный плагин по имени.

**Возвращаемые значения:**
- `0` - успешный запуск
- `-1` - ошибка запуска

#### `dap_plugin_stop()`
```c
int dap_plugin_stop(const char *a_name);
```

Останавливает конкретный плагин по имени.

**Возвращаемые значения:**
- `0` - успешная остановка
- `-1` - ошибка остановки

### Получение статуса

#### `dap_plugin_status()`
```c
dap_plugin_status_t dap_plugin_status(const char *a_name);
```

Получает статус плагина по имени.

**Возвращаемые значения:**
- `STATUS_RUNNING` - плагин запущен
- `STATUS_STOPPED` - плагин остановлен
- `STATUS_NONE` - плагин не найден

## Работа с манифестами

### Инициализация манифестов

#### `dap_plugin_manifest_init()`
```c
int dap_plugin_manifest_init();
```

Инициализирует систему манифестов плагинов.

#### `dap_plugin_manifest_deinit()`
```c
void dap_plugin_manifest_deinit();
```

Деинициализирует систему манифестов.

### Управление манифестами

#### `dap_plugin_manifest_all()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_all(void);
```

Возвращает список всех загруженных манифестов.

**Возвращаемое значение:**
- Указатель на первый манифест в списке (используется uthash)

#### `dap_plugin_manifest_find()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_find(const char *a_name);
```

Ищет манифест плагина по имени.

**Возвращаемое значение:**
- Указатель на найденный манифест или NULL

### Добавление плагинов

#### `dap_plugin_manifest_add_from_file()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_add_from_file(const char *a_file_path);
```

Добавляет плагин из файла манифеста.

**Параметры:**
- `a_file_path` - путь к файлу манифеста

**Возвращаемое значение:**
- Указатель на созданный манифест или NULL при ошибке

#### `dap_plugin_manifest_add_builtin()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_add_builtin(
    const char *a_name, const char *a_type,
    const char *a_author, const char *a_version,
    const char *a_description, char **a_dependencies_names,
    size_t a_dependencies_count, char **a_params,
    size_t a_params_count);
```

Добавляет встроенный плагин программно.

**Возвращаемое значение:**
- Указатель на созданный манифест или NULL при ошибке

### Удаление плагинов

#### `dap_plugins_manifest_remove()`
```c
bool dap_plugins_manifest_remove(const char *a_name);
```

Удаляет плагин по имени.

**Возвращаемое значение:**
- `true` - успешное удаление
- `false` - ошибка удаления

## Структура файлов плагина

### Директория плагина
```
plugin_name/
├── manifest.json     # Манифест плагина
├── libplugin.so      # Бинарная библиотека
├── config/           # Конфигурационные файлы
│   └── plugin.cfg
└── data/             # Данные плагина
    └── ...
```

### Формат манифеста (JSON)
```json
{
  "name": "example_plugin",
  "version": "1.0.0",
  "author": "Developer Name",
  "description": "Example plugin for DAP SDK",
  "type": "module",
  "dependencies": ["core", "net"],
  "params": ["debug", "timeout"],
  "builtin": false
}
```

## Типы плагинов

### 1. Модульные плагины
- Расширяют функциональность базовых модулей
- Могут добавлять новые алгоритмы, протоколы
- Пример: криптографические модули, сетевые протоколы

### 2. Сервисные плагины
- Предоставляют сервисы приложениям
- Реализуют бизнес-логику
- Пример: обработка платежей, аутентификация

### 3. Драйверные плагины
- Интерфейсы к внешним системам
- Абстракция оборудования и сервисов
- Пример: драйверы баз данных, облачные сервисы

## Система зависимостей

### Разрешение зависимостей
```
Plugin A ──┐
           ├──► Dependency Resolution ──► Plugin C
Plugin B ──┘                              │
                                         ▼
                                    Plugin D
```

### Проверка зависимостей
```c
// Получение списка зависимостей
char *deps = dap_plugin_manifests_get_list_dependencies(manifest);
printf("Dependencies: %s\n", deps);
free(deps);
```

## Жизненный цикл плагина

### 1. Загрузка
```
Поиск манифеста → Проверка зависимостей → Загрузка библиотеки
    ↓
Инициализация → Регистрация callback'ов → Запуск
```

### 2. Выполнение
```
Обработка событий → Вызов функций плагина
    ↓
Взаимодействие с другими плагинами
    ↓
Предоставление сервисов
```

### 3. Выгрузка
```
Остановка сервисов → Вызов cleanup → Выгрузка библиотеки
    ↓
Освобождение ресурсов → Удаление из реестра
```

## Безопасность и изоляция

### Изоляция плагинов
- Каждый плагин работает в своем адресном пространстве
- Ограничение доступа к системным ресурсам
- Песочница для исполнения кода

### Валидация
- Проверка целостности бинарных файлов
- Верификация манифестов
- Контроль зависимостей

## Использование

### Базовая инициализация

```c
#include "dap_plugin.h"

// Инициализация системы плагинов
if (dap_plugin_init("./plugins") != 0) {
    fprintf(stderr, "Failed to initialize plugin system\n");
    return -1;
}

// Регистрация типа плагина
dap_plugin_type_callbacks_t callbacks = {
    .load = my_plugin_load_callback,
    .unload = my_plugin_unload_callback
};

dap_plugin_type_create("my_type", &callbacks);

// Запуск всех плагинов
dap_plugin_start_all();

// Основная работа приложения
// ...

// Остановка и деинициализация
dap_plugin_stop_all();
dap_plugin_deinit();
```

### Создание типа плагина

```c
int my_plugin_load_callback(dap_plugin_manifest_t *manifest,
                           void **pvt_data, char **error_str) {
    // Инициализация плагина
    *pvt_data = malloc(sizeof(my_plugin_data_t));

    // Загрузка конфигурации
    if (manifest->config) {
        // Обработка конфигурации
    }

    return 0; // Успех
}

int my_plugin_unload_callback(dap_plugin_manifest_t *manifest,
                             void *pvt_data, char **error_str) {
    // Очистка ресурсов
    free(pvt_data);
    return 0; // Успех
}
```

### Работа с манифестами

```c
// Поиск плагина
dap_plugin_manifest_t *plugin = dap_plugin_manifest_find("my_plugin");
if (plugin) {
    printf("Plugin version: %s\n", plugin->version);
    printf("Plugin type: %s\n", plugin->type);
}

// Получение всех плагинов
dap_plugin_manifest_t *current, *tmp;
HASH_ITER(hh, dap_plugin_manifest_all(), current, tmp) {
    printf("Plugin: %s (%s)\n", current->name, current->version);
}
```

### Управление жизненным циклом

```c
// Проверка статуса
dap_plugin_status_t status = dap_plugin_status("my_plugin");
switch (status) {
    case STATUS_RUNNING:
        printf("Plugin is running\n");
        break;
    case STATUS_STOPPED:
        printf("Plugin is stopped\n");
        break;
    case STATUS_NONE:
        printf("Plugin not found\n");
        break;
}

// Управление плагином
if (dap_plugin_start("my_plugin") == 0) {
    printf("Plugin started successfully\n");
}

// Остановка через некоторое время
sleep(10);
dap_plugin_stop("my_plugin");
```

## Интеграция с другими модулями

### DAP Config
- Загрузка конфигурации плагинов
- Управление параметрами
- Валидация настроек

### DAP Common
- Общие структуры данных
- Утилиты для работы с памятью
- Логирование и отладка

### DAP Time
- Управление временем жизни плагинов
- Таймеры и планировщики
- Синхронизация операций

## Лучшие практики

### 1. Проектирование плагинов
```c
// Четкое определение интерфейса
typedef struct my_plugin_interface {
    int (*init)(void *config);
    void (*cleanup)(void);
    int (*process)(void *data);
} my_plugin_interface_t;
```

### 2. Управление зависимостями
```c
// Явное объявление зависимостей
const char *dependencies[] = {
    "core",
    "net",
    "crypto"
};

// Проверка доступности зависимостей
for (size_t i = 0; i < ARRAY_SIZE(dependencies); i++) {
    if (!dap_plugin_manifest_find(dependencies[i])) {
        return -1; // Зависимость не найдена
    }
}
```

### 3. Обработка ошибок
```c
// Безопасная загрузка плагина
int load_result = dap_plugin_start(plugin_name);
if (load_result != 0) {
    log_error("Failed to load plugin %s: %s", plugin_name, strerror(errno));

    // Попытка отката
    if (fallback_plugin) {
        dap_plugin_start(fallback_plugin);
    }
}
```

### 4. Ресурсное управление
```c
// RAII паттерн для плагинов
typedef struct plugin_guard {
    char *name;
    bool loaded;
} plugin_guard_t;

void plugin_guard_cleanup(plugin_guard_t *guard) {
    if (guard->loaded) {
        dap_plugin_stop(guard->name);
    }
    free(guard->name);
}
```

## Отладка и мониторинг

### Логирование
```c
// Включение отладочного вывода
#define DAP_PLUGIN_DEBUG 1

// Логирование операций плагина
log_info("Plugin %s loaded successfully", manifest->name);
log_debug("Plugin %s config: %s", manifest->name,
          dap_config_to_string(manifest->config));
```

### Мониторинг состояния
```c
// Периодическая проверка состояния плагинов
void check_plugins_status() {
    dap_plugin_manifest_t *current, *tmp;
    HASH_ITER(hh, dap_plugin_manifest_all(), current, tmp) {
        dap_plugin_status_t status = dap_plugin_status(current->name);
        if (status != STATUS_RUNNING) {
            log_warning("Plugin %s is not running (status: %d)",
                       current->name, status);
        }
    }
}
```

## Типичные проблемы

### 1. Конфликты зависимостей
```
Симптом: Плагин не загружается из-за циклических зависимостей
Решение: Перепроектировать архитектуру зависимостей
```

### 2. Утечки памяти
```
Симптом: Рост потребления памяти при загрузке/выгрузке плагинов
Решение: Правильная реализация cleanup callback'ов
```

### 3. Thread safety
```
Симптом: Race conditions при одновременном доступе к плагинам
Решение: Использовать mutex'ы и атомарные операции
```

## Заключение

Модуль `dap_plugin` предоставляет мощную и гибкую систему для расширения функциональности DAP SDK. Его архитектура обеспечивает безопасную загрузку, управление зависимостями и горячую замену компонентов без перезапуска приложения.

