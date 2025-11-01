# DAP Client Transport API

## Обзор

DAP Client поддерживает различные транспортные протоколы для передачи данных. По умолчанию используется HTTP/HTTPS, но можно переключиться на другие транспорты.

## API

### Установка типа транспорта

```c
void dap_client_set_transport_type(dap_client_t *a_client, dap_stream_transport_type_t a_transport_type);
```

**Параметры:**
- `a_client` - указатель на клиент
- `a_transport_type` - тип транспорта (см. `dap_stream_transport_type_t`)

### Получение типа транспорта

```c
dap_stream_transport_type_t dap_client_get_transport_type(dap_client_t *a_client);
```

**Возвращает:** текущий тип транспорта или `DAP_STREAM_TRANSPORT_HTTP` (по умолчанию) при ошибке.

## Доступные транспорты

```c
typedef enum dap_stream_transport_type {
    DAP_STREAM_TRANSPORT_HTTP           = 0x01,  // HTTP/HTTPS (по умолчанию)
    DAP_STREAM_TRANSPORT_UDP_BASIC      = 0x02,  // Базовый UDP (ненадежный)
    DAP_STREAM_TRANSPORT_UDP_RELIABLE   = 0x03,  // UDP с гарантией доставки (ARQ)
    DAP_STREAM_TRANSPORT_UDP_QUIC_LIKE  = 0x04,  // QUIC-подобный мультиплексированный UDP
    DAP_STREAM_TRANSPORT_WEBSOCKET      = 0x05,  // WebSocket (HTTP upgrade)
    DAP_STREAM_TRANSPORT_TLS_DIRECT     = 0x06,  // Прямое TLS соединение
    DAP_STREAM_TRANSPORT_DNS_TUNNEL     = 0x07,  // Туннелирование через DNS
    DAP_STREAM_TRANSPORT_OBFS4          = 0x08   // Tor-style обфускация (obfs4)
} dap_stream_transport_type_t;
```

## Примеры использования

### Пример 1: Базовое использование HTTP (по умолчанию)

```c
dap_client_t *l_client = dap_client_new(NULL, NULL);
// По умолчанию используется HTTP
// l_client->transport_type == DAP_STREAM_TRANSPORT_HTTP
```

### Пример 2: Переключение на WebSocket

```c
dap_client_t *l_client = dap_client_new(NULL, NULL);

// Переключаемся на WebSocket
dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_WEBSOCKET);

// Настраиваем uplink
dap_stream_node_addr_t l_node_addr = {0};
dap_client_set_uplink_unsafe(l_client, &l_node_addr, "example.com", 8089);

// Подключаемся
dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, callback);
```

### Пример 3: UDP с гарантией доставки

```c
dap_client_t *l_client = dap_client_new(NULL, NULL);

// Используем надежный UDP
dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_UDP_RELIABLE);

// Настраиваем uplink
dap_stream_node_addr_t l_node_addr = {0};
dap_client_set_uplink_unsafe(l_client, &l_node_addr, "example.com", 8089);

// Подключаемся
dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, callback);
```

### Пример 4: Проверка текущего транспорта

```c
dap_client_t *l_client = dap_client_new(NULL, NULL);

dap_stream_transport_type_t l_type = dap_client_get_transport_type(l_client);

switch(l_type) {
    case DAP_STREAM_TRANSPORT_HTTP:
        log_it(L_INFO, "Using HTTP transport");
        break;
    case DAP_STREAM_TRANSPORT_WEBSOCKET:
        log_it(L_INFO, "Using WebSocket transport");
        break;
    case DAP_STREAM_TRANSPORT_UDP_RELIABLE:
        log_it(L_INFO, "Using reliable UDP transport");
        break;
    // ... и т.д.
}
```

### Пример 5: Динамическое переключение транспорта

```c
dap_client_t *l_client = dap_client_new(NULL, NULL);

// Пробуем WebSocket
dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_WEBSOCKET);
int result = dap_client_go_stage(l_client, STAGE_STREAM_CONNECTED, NULL);

if (result < 0) {
    // WebSocket не сработал, возвращаемся на HTTP
    log_it(L_WARNING, "WebSocket failed, falling back to HTTP");
    dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_HTTP);
    dap_client_go_stage(l_client, STAGE_STREAM_CONNECTED, NULL);
}
```

## Важные замечания

1. **Порядок вызовов**: Устанавливайте тип транспорта **до** вызова `dap_client_go_stage()`.

2. **Совместимость с сервером**: Убедитесь что сервер поддерживает выбранный транспорт.

3. **Портыумолчанию**:
   - HTTP/HTTPS: обычно 8089
   - WebSocket: обычно 8089 (с HTTP upgrade)
   - UDP: может быть любой порт, настраивается на сервере

4. **Регистрация транспортов**: Некоторые транспорты требуют предварительной регистрации через `dap_stream_transport_register()`.

5. **Обратная совместимость**: По умолчанию используется HTTP для обеспечения обратной совместимости с существующими серверами.

## Диагностика

Для отладки транспортного слоя можно включить детальное логирование:

```c
dap_log_level_set(L_DEBUG);
```

Это выведет информацию о:
- Установке типа транспорта
- Процессе подключения
- Обмене данными
- Ошибках транспортного уровня

## См. также

- `dap_stream_transport.h` - описание транспортного слоя
- `dap_client.h` - основной API клиента
- `dap_stream.h` - потоковый протокол

