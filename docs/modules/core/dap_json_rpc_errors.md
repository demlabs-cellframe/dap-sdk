# DAP JSON-RPC Errors (Ошибки JSON-RPC)

## Обзор

Модуль `dap_json_rpc_errors` предоставляет унифицированную систему обработки и управления ошибками для JSON-RPC протокола в DAP SDK. Реализует стандартизированный формат ошибок, совместимый со спецификацией JSON-RPC 2.0.

## Назначение

JSON-RPC протокол требует стандартизированной обработки ошибок для обеспечения:
- **Надежной коммуникации** между клиентом и сервером
- **Стандартизированного формата** сообщений об ошибках
- **Локализации ошибок** для различных языков
- **Логирования и отладки** сетевых взаимодействий
- **Совместимости** с JSON-RPC 2.0 спецификацией

## Основные возможности

### 📋 **Стандартизированные коды ошибок**
- Предопределенные коды для распространенных ошибок
- Возможность расширения пользовательскими кодами
- Категоризация ошибок по типам

### 🔄 **JSON сериализация/десериализация**
- Конвертация ошибок в JSON формат
- Парсинг ошибок из JSON объектов
- Поддержка массивов ошибок

### 📝 **Локализация и сообщения**
- Многоязычная поддержка сообщений
- Форматированные сообщения с параметрами
- Автоматическая генерация сообщений

### 🔍 **Поиск и управление**
- Поиск ошибок по кодам
- Управление коллекцией ошибок
- Потокобезопасные операции

## Стандартизированные коды ошибок

### Системные ошибки

```c
#define DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED 1
// Ошибка выделения памяти

#define DAP_JSON_RPC_ERR_CODE_SERIALIZATION_SIGN_TO_JSON 2
// Ошибка сериализации подписи в JSON

#define DAP_JSON_RPC_ERR_CODE_SERIALIZATION_DATUM_TO_JSON 3
// Ошибка сериализации datum в JSON

#define DAP_JSON_RPC_ERR_CODE_SERIALIZATION_ADDR_TO_JSON 4
// Ошибка сериализации адреса в JSON
```

### Диапазон пользовательских ошибок

```c
#define DAP_JSON_RPC_ERR_CODE_METHOD_ERR_START 11
// Начало диапазона для пользовательских ошибок методов
```

## Структуры данных

### Основная структура ошибки

```c
typedef struct dap_json_rpc_error {
    int code_error;    // Код ошибки
    char *msg;         // Сообщение об ошибке
    void *next;        // Следующая ошибка в списке (для UTlist)
} dap_json_rpc_error_t;
```

### JSON представление ошибки

```c
typedef struct dap_json_rpc_error_JSON {
    json_object *obj_code;  // JSON объект с кодом ошибки
    json_object *obj_msg;   // JSON объект с сообщением
} dap_json_rpc_error_JSON_t;
```

## API Функции

### Инициализация и деинициализация

```c
// Инициализация модуля ошибок
int dap_json_rpc_error_init(void);

// Деинициализация модуля ошибок
void dap_json_rpc_error_deinit(void);

// Добавление стандартных ошибок
void dap_json_rpc_add_standart_erros(void);
```

### Создание и управление JSON объектами

```c
// Создание JSON объекта ошибки
dap_json_rpc_error_JSON_t *dap_json_rpc_error_JSON_create();

// Освобождение JSON объекта ошибки
void dap_json_rpc_error_JSON_free(dap_json_rpc_error_JSON_t *a_error_json);

// Добавление данных в JSON объект ошибки
dap_json_rpc_error_JSON_t *dap_json_rpc_error_JSON_add_data(int code, const char *msg);
```

### Добавление ошибок в ответ

```c
// Добавление ошибки в JSON массив ответа
int dap_json_rpc_error_add(json_object* a_json_arr_reply, int a_code_error, const char *msg, ...);
```

### Получение ошибок

```c
// Получение всех ошибок в виде JSON объекта
json_object *dap_json_rpc_error_get();

// Поиск ошибки по коду
dap_json_rpc_error_t *dap_json_rpc_error_search_by_code(int a_code_error);

// Получение JSON представления конкретной ошибки
json_object *dap_json_rpc_error_get_json(dap_json_rpc_error_t *a_error);

// Получение JSON строки конкретной ошибки
char *dap_json_rpc_error_get_json_str(dap_json_rpc_error_t *a_error);
```

### Парсинг ошибок

```c
// Создание ошибки из JSON строки
dap_json_rpc_error_t *dap_json_rpc_create_from_json(const char *a_json);

// Создание ошибки из JSON объекта
dap_json_rpc_error_t *dap_json_rpc_create_from_json_object(json_object *a_jobj);
```

## Специализированные макросы

### Макрос для ошибок выделения памяти

```c
#define dap_json_rpc_allocation_error(a_json_arr_reply) \
    do { \
        log_it(L_CRITICAL, "%s", c_error_memory_alloc); \
        dap_json_rpc_error_add(a_json_arr_reply, \
                              DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED, \
                              "[%s] %s", LOG_TAG, c_error_memory_alloc); \
    } while (0)
```

## Использование

### Базовая обработка ошибок

```c
#include "dap_json_rpc_errors.h"

// Инициализация модуля
if (dap_json_rpc_error_init() != 0) {
    fprintf(stderr, "Failed to initialize JSON-RPC errors\n");
    return -1;
}

// Добавление стандартных ошибок
dap_json_rpc_add_standart_erros();
```

### Добавление ошибок в JSON-RPC ответ

```c
// Создание массива для ошибок
json_object *json_response = json_object_new_array();

// Добавление ошибки выделения памяти
dap_json_rpc_allocation_error(json_response);

// Добавление пользовательской ошибки
dap_json_rpc_error_add(json_response,
                      1001,
                      "Invalid parameter: %s",
                      "wallet_address");

// Получение JSON строки ответа
const char *response_str = json_object_to_json_string(json_response);
send_response(client_socket, response_str);

// Очистка
json_object_put(json_response);
```

### Парсинг ошибок из запроса

```c
// Получение JSON ошибки от клиента
const char *error_json = receive_error_from_client();

// Парсинг ошибки
dap_json_rpc_error_t *parsed_error = dap_json_rpc_create_from_json(error_json);
if (parsed_error) {
    printf("Received error: code=%d, message=%s\n",
           parsed_error->code_error, parsed_error->msg);

    // Обработка в зависимости от кода ошибки
    switch (parsed_error->code_error) {
        case DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED:
            handle_memory_error();
            break;
        case 1001:
            handle_invalid_parameter();
            break;
        default:
            handle_unknown_error(parsed_error);
            break;
    }

    // Освобождение памяти
    free(parsed_error->msg);
    free(parsed_error);
}
```

### Поиск и валидация ошибок

```c
// Поиск ошибки по коду
dap_json_rpc_error_t *error_info = dap_json_rpc_error_search_by_code(1001);
if (error_info) {
    printf("Error found: %s\n", error_info->msg);
}

// Проверка существования кода ошибки
bool is_valid_error_code(int error_code) {
    return dap_json_rpc_error_search_by_code(error_code) != NULL;
}
```

## Формат JSON ошибок

### Структура JSON-RPC ошибки

```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": 1001,
    "message": "Invalid parameter: wallet_address",
    "data": {
      "additional_info": "Detailed error description"
    }
  },
  "id": null
}
```

### Массив ошибок

```json
{
  "jsonrpc": "2.0",
  "errors": [
    {
      "code": 1,
      "message": "Memory allocation failed"
    },
    {
      "code": 1001,
      "message": "Invalid wallet address format"
    }
  ],
  "id": 123
}
```

## Категории ошибок

### Системные ошибки (-32768 до -32000)

| Код | Описание |
|-----|----------|
| -32700 | Parse error (некорректный JSON) |
| -32600 | Invalid Request (некорректная структура) |
| -32601 | Method not found |
| -32602 | Invalid params |
| -32603 | Internal error |

### Серверные ошибки (-32099 до -32000)

| Код | Описание |
|-----|----------|
| -32000 | Server error |
| -32001 | Authentication failed |
| -32002 | Permission denied |

### Прикладные ошибки (1 и выше)

| Код | Описание |
|-----|----------|
| 1 | Memory allocation error |
| 2 | Serialization error |
| 3 | Datum serialization error |
| 4 | Address serialization error |
| 11+ | User-defined errors |

## Особенности реализации

### Потокобезопасность

- **Глобальная коллекция**: Все ошибки хранятся в глобальной структуре
- **UTlist**: Использование UTlist для потокобезопасных операций
- **Синхронизация**: Требуется внешняя синхронизация при многопоточном доступе

### Управление памятью

- **Автоматическое выделение**: Функции выделяют память для структур
- **Обязательное освобождение**: Память должна освобождаться вызывающим кодом
- **JSON объекты**: Автоматическое управление счетчиком ссылок

### Производительность

- **Быстрый поиск**: O(n) поиск по коду ошибки (n обычно мало)
- **Минимальные копирования**: Использование указателей где возможно
- **Ленивая инициализация**: Стандартные ошибки добавляются по требованию

## Использование в DAP SDK

### Обработка сетевых ошибок

```c
// В сетевом обработчике JSON-RPC
json_object *process_json_rpc_request(const char *request_json) {
    json_object *response = json_object_new_object();

    // Парсинг запроса
    json_object *request = json_tokener_parse(request_json);
    if (!request) {
        // Ошибка парсинга JSON
        json_object *error_array = json_object_new_array();
        dap_json_rpc_error_add(error_array, -32700, "Parse error");
        json_object_object_add(response, "error", error_array);
        return response;
    }

    // Обработка запроса...
    // При ошибке добавляем в ответ
    if (processing_failed) {
        json_object *error_array = json_object_new_array();
        dap_json_rpc_error_add(error_array, 1001, "Processing failed: %s", error_details);
        json_object_object_add(response, "error", error_array);
    }

    json_object_put(request);
    return response;
}
```

### Логирование ошибок

```c
// Логирование ошибок с детализацией
void log_json_rpc_error(dap_json_rpc_error_t *error) {
    if (!error) return;

    // Получение JSON представления для логирования
    char *error_json = dap_json_rpc_error_get_json_str(error);
    log_it(L_ERROR, "JSON-RPC Error: %s", error_json);
    free(error_json);
}
```

### Валидация параметров

```c
// Валидация параметров метода
bool validate_wallet_params(json_object *params, json_object *error_array) {
    // Проверка наличия обязательных параметров
    if (!json_object_object_get(params, "wallet_address")) {
        dap_json_rpc_error_add(error_array, -32602,
                              "Missing required parameter: wallet_address");
        return false;
    }

    // Проверка формата адреса
    const char *address = json_object_get_string(
        json_object_object_get(params, "wallet_address"));

    if (!is_valid_wallet_address(address)) {
        dap_json_rpc_error_add(error_array, 1001,
                              "Invalid wallet address format: %s", address);
        return false;
    }

    return true;
}
```

## Связанные модули

- `dap_common.h` - Общие определения и макросы
- `dap_strfuncs.h` - Работа со строками и форматирование
- `json.h` - JSON парсинг и сериализация (libjson)
- `utlist.h` - Макросы для работы со связными списками

## Замечания по безопасности

### Валидация входных данных

```c
// Всегда проверяйте входные параметры
dap_json_rpc_error_t *create_error_from_json_safe(const char *json_str) {
    if (!json_str || strlen(json_str) == 0) {
        return NULL;
    }

    // Ограничение размера JSON строки
    if (strlen(json_str) > MAX_JSON_SIZE) {
        log_it(L_WARNING, "JSON string too large: %zu bytes", strlen(json_str));
        return NULL;
    }

    return dap_json_rpc_create_from_json(json_str);
}
```

### Управление памятью

```c
// Правильное освобождение ресурсов
void cleanup_json_rpc_error(dap_json_rpc_error_t *error) {
    if (!error) return;

    if (error->msg) {
        free(error->msg);
    }
    free(error);
}

// Для JSON объектов
json_object *response = json_object_new_object();
// ... использование ...
json_object_put(response); // Автоматическое освобождение
```

### Логирование чувствительной информации

```c
// Избегайте логирования чувствительных данных
dap_json_rpc_error_add(error_array, 1002, "Authentication failed");
// НЕ ДЕЛАТЬ: "Authentication failed for user: %s", username
```

## Отладка

### Диагностика ошибок

```c
// Функция для отладочного вывода ошибки
void debug_json_rpc_error(dap_json_rpc_error_t *error) {
    if (!error) {
        printf("Error: NULL\n");
        return;
    }

    printf("JSON-RPC Error:\n");
    printf("  Code: %d\n", error->code_error);
    printf("  Message: %s\n", error->msg ? error->msg : "NULL");

    // Получение JSON представления
    char *json_str = dap_json_rpc_error_get_json_str(error);
    printf("  JSON: %s\n", json_str);
    free(json_str);
}

// Тестирование обработки ошибок
void test_error_handling() {
    // Тест создания ошибки
    json_object *error_array = json_object_new_array();
    dap_json_rpc_error_add(error_array, 1001, "Test error");

    // Тест парсинга
    const char *json_str = json_object_to_json_string(error_array);
    dap_json_rpc_error_t *parsed = dap_json_rpc_create_from_json(json_str);

    debug_json_rpc_error(parsed);

    // Очистка
    if (parsed) {
        free(parsed->msg);
        free(parsed);
    }
    json_object_put(error_array);
}
```

Этот модуль обеспечивает надежную и стандартизированную обработку ошибок в JSON-RPC коммуникациях DAP SDK, способствуя созданию надежных и отлаживаемых распределенных приложений.

