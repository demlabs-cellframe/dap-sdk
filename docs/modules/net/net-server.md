# DAP Net Server Module (dap_http_server.h)

## Обзор

Модуль `dap_http_server` предоставляет высокопроизводительный HTTP/HTTPS сервер для DAP SDK. Он включает в себя:

- **Многопротокольную поддержку** - HTTP/1.1, HTTPS с TLS
- **Виртуальные хосты** - поддержка множественных доменов
- **Обработчики URL** - гибкая система маршрутизации
- **Кэширование** - in-memory и disk caching
- **WebSocket поддержка** - для real-time коммуникации
- **SSL/TLS шифрование** - с поддержкой SNI

## Архитектурная роль

HTTP Server является ключевым компонентом сетевой инфраструктуры DAP:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP Net       │───▶│  HTTP Server    │
│   Модуль        │    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Транспорт │             │Приложения│
    │уровень  │             │уровень   │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │TCP/UDP  │◄────────────►│REST API │
    │сокеты   │             │WebSocket│
    └─────────┘             └─────────┘
```

## Основные структуры данных

### `dap_http_server`
```c
typedef struct dap_http_server {
    char *server_name;                    // Имя сервера
    struct dap_server *internal_server; // Внутренний DAP сервер
    dap_http_url_proc_t *url_proc;       // Обработчики URL
    void *_inheritor;                    // Для наследования
} dap_http_server_t;
```

### `dap_http_url_proc`
```c
typedef struct dap_http_url_proc {
    char url[512];                       // Шаблон URL
    struct dap_http_server *http;       // HTTP сервер

    dap_http_cache_t *cache;             // Кэш
    pthread_rwlock_t cache_rwlock;       // Блокировка кэша

    // Callback функции
    dap_http_client_callback_t new_callback;
    dap_http_client_callback_t delete_callback;
    dap_http_client_callback_t headers_read_callback;
    dap_http_client_callback_write_t headers_write_callback;
    dap_http_client_callback_t data_read_callback;

    void *internal;                      // Внутренние данные
    UT_hash_handle hh;                   // Для хэш-таблицы
} dap_http_url_proc_t;
```

## Основные функции

### Создание и управление сервером

#### `dap_http_server_create()`
```c
dap_http_server_t *dap_http_server_create(const char *a_server_name);
```

**Параметры:**
- `a_server_name` - имя сервера

**Возвращаемое значение:**
- Указатель на созданный HTTP сервер или NULL при ошибке

#### `dap_http_server_delete()`
```c
void dap_http_server_delete(dap_http_server_t *a_http_server);
```

**Параметры:**
- `a_http_server` - HTTP сервер для удаления

### Добавление обработчиков URL

#### `dap_http_server_add_proc()`
```c
bool dap_http_server_add_proc(dap_http_server_t *a_http_server,
                              const char *a_url_path,
                              dap_http_client_callback_t a_new_callback,
                              dap_http_client_callback_t a_delete_callback,
                              dap_http_client_callback_t a_headers_read_callback,
                              dap_http_client_callback_write_t a_headers_write_callback,
                              dap_http_client_callback_t a_data_read_callback);
```

**Параметры:**
- `a_http_server` - HTTP сервер
- `a_url_path` - путь URL для обработки
- `a_new_callback` - callback для новых соединений
- `a_delete_callback` - callback для закрытия соединений
- `a_headers_read_callback` - callback для чтения заголовков
- `a_headers_write_callback` - callback для записи заголовков
- `a_data_read_callback` - callback для чтения данных

**Возвращаемое значение:**
- `true` - успешное добавление
- `false` - ошибка добавления

### Работа с кэшированием

#### `dap_http_server_cache_ctl()`
```c
int dap_http_server_cache_ctl(dap_http_server_t *a_http_server,
                              const char *a_url_path,
                              dap_http_cache_ctl_command_t a_command,
                              void *a_arg);
```

**Команды управления кэшем:**
```c
typedef enum {
    DAP_HTTP_CACHE_CTL_SET_MAX_SIZE,     // Установить максимальный размер
    DAP_HTTP_CACHE_CTL_GET_MAX_SIZE,     // Получить максимальный размер
    DAP_HTTP_CACHE_CTL_CLEAR,            // Очистить кэш
    DAP_HTTP_CACHE_CTL_STATS             // Получить статистику
} dap_http_cache_ctl_command_t;
```

## Типы callback функций

### `dap_http_client_callback_t`
```c
typedef void (*dap_http_client_callback_t)(struct dap_http_client *a_client, void *a_arg);
```

**Параметры:**
- `a_client` - HTTP клиент
- `a_arg` - пользовательские аргументы

### `dap_http_client_callback_write_t`
```c
typedef size_t (*dap_http_client_callback_write_t)(struct dap_http_client *a_client,
                                                  void *a_arg, uint8_t *a_buf,
                                                  size_t a_buf_size);
```

**Параметры:**
- `a_client` - HTTP клиент
- `a_arg` - пользовательские аргументы
- `a_buf` - буфер для записи
- `a_buf_size` - размер буфера

**Возвращаемое значение:**
- Количество записанных байт

## HTTP Клиент

### Структура `dap_http_client`
```c
typedef struct dap_http_client {
    struct dap_http_url_proc *proc;       // Обработчик URL
    dap_http_t *http;                     // HTTP парсер
    void *internal;                       // Внутренние данные

    // Состояние
    bool is_alive;                        // Клиент активен
    bool is_closed;                       // Клиент закрыт

    // Данные запроса
    char *in_query_string;                // Query string
    char *in_path;                        // Путь запроса
    char *in_method;                      // HTTP метод

    // Заголовки
    dap_http_header_t *in_headers;        // Входящие заголовки
    dap_http_header_t *out_headers;       // Исходящие заголовки

    // Тело запроса/ответа
    uint8_t *in_body;                     // Тело запроса
    size_t in_body_size;                  // Размер тела запроса

    uint8_t *out_body;                    // Тело ответа
    size_t out_body_size;                 // Размер тела ответа
} dap_http_client_t;
```

## HTTP Заголовки

### `dap_http_header_t`
```c
typedef struct dap_http_header {
    char *name;                           // Имя заголовка
    char *value;                          // Значение заголовка
    UT_hash_handle hh;                    // Для хэш-таблицы
} dap_http_header_t;
```

### Функции работы с заголовками

#### `dap_http_header_parse()`
```c
dap_http_header_t *dap_http_header_parse(const char *a_header_line);
```

**Параметры:**
- `a_header_line` - строка заголовка HTTP

**Возвращаемое значение:**
- Разобранный заголовок или NULL при ошибке

#### `dap_http_header_add()`
```c
void dap_http_header_add(dap_http_header_t **a_headers, const char *a_name,
                        const char *a_value);
```

**Параметры:**
- `a_headers` - указатель на таблицу заголовков
- `a_name` - имя заголовка
- `a_value` - значение заголовка

## Кэширование

### Структура кэша
```c
typedef struct dap_http_cache {
    size_t max_size;                      // Максимальный размер
    size_t current_size;                  // Текущий размер
    dap_http_cache_item_t *items;         // Элементы кэша
    pthread_rwlock_t rwlock;              // Блокировка
} dap_http_cache_t;
```

### Управление кэшем

#### `dap_http_cache_init()`
```c
dap_http_cache_t *dap_http_cache_init(size_t a_max_size);
```

**Параметры:**
- `a_max_size` - максимальный размер кэша в байтах

**Возвращаемое значение:**
- Инициализированный кэш или NULL при ошибке

#### `dap_http_cache_get()`
```c
void *dap_http_cache_get(dap_http_cache_t *a_cache, const char *a_key,
                        size_t *a_data_size);
```

**Параметры:**
- `a_cache` - кэш
- `a_key` - ключ для поиска
- `a_data_size` - указатель для размера данных

**Возвращаемое значение:**
- Данные из кэша или NULL если не найдено

#### `dap_http_cache_set()`
```c
bool dap_http_cache_set(dap_http_cache_t *a_cache, const char *a_key,
                       const void *a_data, size_t a_data_size,
                       time_t a_ttl);
```

**Параметры:**
- `a_cache` - кэш
- `a_key` - ключ
- `a_data` - данные для кэширования
- `a_data_size` - размер данных
- `a_ttl` - время жизни в секундах

**Возвращаемое значение:**
- `true` - успешное кэширование
- `false` - ошибка

## HTTP Методы и Статусы

### Поддерживаемые HTTP методы
```c
#define DAP_HTTP_METHOD_GET     "GET"
#define DAP_HTTP_METHOD_POST    "POST"
#define DAP_HTTP_METHOD_PUT     "PUT"
#define DAP_HTTP_METHOD_DELETE  "DELETE"
#define DAP_HTTP_METHOD_HEAD    "HEAD"
#define DAP_HTTP_METHOD_OPTIONS "OPTIONS"
```

### HTTP статус коды
```c
#define DAP_HTTP_STATUS_200     200  // OK
#define DAP_HTTP_STATUS_201     201  // Created
#define DAP_HTTP_STATUS_204     204  // No Content
#define DAP_HTTP_STATUS_400     400  // Bad Request
#define DAP_HTTP_STATUS_401     401  // Unauthorized
#define DAP_HTTP_STATUS_403     403  // Forbidden
#define DAP_HTTP_STATUS_404     404  // Not Found
#define DAP_HTTP_STATUS_500     500  // Internal Server Error
```

## Использование

### Создание простого HTTP сервера

```c
#include "dap_http_server.h"

// Callback для обработки запросов
void request_handler(dap_http_client_t *client, void *arg) {
    // Чтение данных запроса
    printf("Method: %s\n", client->in_method);
    printf("Path: %s\n", client->in_path);

    // Формирование ответа
    const char *response = "<html><body>Hello DAP!</body></html>";

    // Установка заголовков ответа
    dap_http_header_add(&client->out_headers, "Content-Type",
                       "text/html; charset=utf-8");
    dap_http_header_add(&client->out_headers, "Content-Length",
                       "37");

    // Установка тела ответа
    client->out_body = (uint8_t *)strdup(response);
    client->out_body_size = strlen(response);
}

int main() {
    // Создание HTTP сервера
    dap_http_server_t *server = dap_http_server_create("my_server");
    if (!server) {
        fprintf(stderr, "Failed to create HTTP server\n");
        return -1;
    }

    // Добавление обработчика для всех URL
    if (!dap_http_server_add_proc(server, "/*",
                                  request_handler, NULL, NULL, NULL, NULL)) {
        fprintf(stderr, "Failed to add URL processor\n");
        return -1;
    }

    // Запуск сервера происходит через DAP server
    // server->internal_server уже настроен для обработки HTTP

    // Ожидание завершения
    pause();

    // Очистка
    dap_http_server_delete(server);

    return 0;
}
```

### Работа с заголовками

```c
void headers_handler(dap_http_client_t *client, void *arg) {
    // Чтение входящих заголовков
    dap_http_header_t *header = NULL;
    dap_http_header_t *headers = client->in_headers;

    HASH_ITER(hh, headers, header, tmp) {
        printf("Header: %s = %s\n", header->name, header->value);
    }

    // Добавление заголовков ответа
    dap_http_header_add(&client->out_headers, "Server", "DAP HTTP Server");
    dap_http_header_add(&client->out_headers, "X-Powered-By", "DAP SDK");
}

// Регистрация обработчиков
dap_http_server_add_proc(server, "/api/*",
                         NULL, NULL, headers_handler, NULL, request_handler);
```

### Использование кэширования

```c
// Callback для кэширования
void cache_handler(dap_http_client_t *client, void *arg) {
    const char *cache_key = client->in_path;

    // Попытка получить данные из кэша
    size_t data_size;
    void *cached_data = dap_http_cache_get(client->proc->cache,
                                          cache_key, &data_size);

    if (cached_data) {
        // Данные найдены в кэше
        client->out_body = cached_data;
        client->out_body_size = data_size;
        dap_http_header_add(&client->out_headers, "X-Cache", "HIT");
    } else {
        // Данные не найдены, генерируем ответ
        const char *response = generate_response(client);

        // Сохраняем в кэше
        dap_http_cache_set(client->proc->cache, cache_key,
                          response, strlen(response), 300); // 5 минут

        client->out_body = (uint8_t *)strdup(response);
        client->out_body_size = strlen(response);
        dap_http_header_add(&client->out_headers, "X-Cache", "MISS");
    }
}

// Настройка кэша для URL процессора
dap_http_server_cache_ctl(server, "/api/data/*",
                          DAP_HTTP_CACHE_CTL_SET_MAX_SIZE,
                          (void *)1024 * 1024); // 1MB кэш
```

## Производительность и оптимизации

### Оптимизации сервера
- **Асинхронная обработка** - неблокирующие операции
- **Connection pooling** - переиспользование соединений
- **Memory pooling** - управление памятью
- **Zero-copy operations** - минимизация копирования данных

### Кэширование
- **In-memory cache** - для частых запросов
- **LRU eviction** - вытеснение редко используемых данных
- **TTL support** - время жизни кэшированных данных
- **Thread-safe access** - безопасный многопоточный доступ

## Безопасность

### Защита от атак
- **Request size limits** - ограничение размера запросов
- **Rate limiting** - ограничение частоты запросов
- **Input validation** - валидация входных данных
- **CORS support** - защита от cross-origin атак

### HTTPS поддержка
```c
// Настройка SSL/TLS
dap_ssl_config_t ssl_config = {
    .certificate_file = "/etc/ssl/server.crt",
    .private_key_file = "/etc/ssl/server.key",
    .ca_certificate_file = "/etc/ssl/ca.crt"
};

dap_http_server_enable_ssl(server, &ssl_config);
```

## Интеграция с другими модулями

### DAP Server
- Базовый сетевой сервер
- Управление соединениями
- Обработка протоколов

### DAP Events
- Асинхронная обработка событий
- Управление потоками
- Таймеры и callbacks

### DAP Config
- Загрузка конфигурации сервера
- Настройка параметров
- Валидация настроек

## Типичные сценарии использования

### 1. REST API сервер
```c
// Обработчик для REST API
void api_handler(dap_http_client_t *client, void *arg) {
    if (strcmp(client->in_method, "GET") == 0) {
        // Обработка GET запросов
        handle_get_request(client);
    } else if (strcmp(client->in_method, "POST") == 0) {
        // Обработка POST запросов
        handle_post_request(client);
    } else {
        // Метод не поддерживается
        client->out_status = 405; // Method Not Allowed
    }
}

// Регистрация API обработчика
dap_http_server_add_proc(server, "/api/v1/*",
                         api_handler, NULL, NULL, NULL, NULL);
```

### 2. Статический файловый сервер
```c
void file_handler(dap_http_client_t *client, void *arg) {
    const char *filepath = get_filepath_from_url(client->in_path);

    if (access(filepath, F_OK) == 0) {
        // Файл существует
        serve_file(client, filepath);
        client->out_status = 200;
    } else {
        // Файл не найден
        client->out_status = 404;
        client->out_body = (uint8_t *)strdup("File not found");
        client->out_body_size = 13;
    }
}

dap_http_server_add_proc(server, "/static/*",
                         file_handler, NULL, NULL, NULL, NULL);
```

### 3. WebSocket сервер
```c
void websocket_upgrade_handler(dap_http_client_t *client, void *arg) {
    // Проверка заголовка Upgrade
    const char *upgrade = dap_http_header_find(client->in_headers, "Upgrade");

    if (upgrade && strcmp(upgrade, "websocket") == 0) {
        // Выполнение WebSocket handshake
        perform_websocket_handshake(client);
    } else {
        client->out_status = 400; // Bad Request
    }
}

dap_http_server_add_proc(server, "/ws",
                         websocket_upgrade_handler, NULL, NULL, NULL, NULL);
```

## Заключение

Модуль `dap_http_server` предоставляет полнофункциональный HTTP/HTTPS сервер с поддержкой современных веб-стандартов. Его интеграция с остальной экосистемой DAP SDK обеспечивает высокую производительность и масштабируемость для веб-приложений.

