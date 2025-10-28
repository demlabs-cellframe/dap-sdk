# Типы тестов в DAP SDK

## 📖 Определения

### Unit Tests (Юнит тесты)
**Тесты отдельных модулей в полной изоляции**

✅ **Характеристики:**
- Тестируют **один модуль** изолированно
- Все зависимости **мокируются**
- Нет реальных сетевых соединений
- Нет реальной файловой системы
- Нет реальной базы данных
- Быстрые (миллисекунды)
- Детерминированные (всегда один результат)

❌ **НЕ используют:**
- `dap_events` (реальная event система)
- Реальные TCP/UDP сокеты
- Реальные HTTP соединения
- Реальные файлы

✅ **Используют:**
- `dap_mock` - мокирование функций
- `dap_mock_async` - эмуляция async без dap_events
- Синтетические данные
- Stub возвращаемые значения

### Integration Tests (Интеграционные тесты)
**Тесты взаимодействия нескольких модулей**

✅ **Характеристики:**
- Тестируют **интеграцию** модулей
- Используют **реальную** инфраструктуру
- Реальные TCP/UDP соединения
- Реальные HTTP запросы/ответы
- Локальный сервер поднимается для теста
- Медленнее (секунды)
- Могут иметь race conditions

✅ **Используют:**
- `dap_events` - реальная event система
- Реальные сокеты (127.0.0.1)
- Реальный HTTP server + client
- Настоящий протокол

❌ **НЕ используют:**
- Моки (только в крайних случаях)
- Внешние зависимости (интернет)
- Production серверы

## 🎯 Примеры

### ❌ НЕПРАВИЛЬНО: "Integration" test с httpbin.org

```c
// ЭТО НЕ INTEGRATION TEST!
// Это external dependency test
void test_http_client(void) {
    // Обращение к внешнему серверу
    dap_client_http_request("httpbin.org", 80, ...);
    
    // ПРОБЛЕМЫ:
    // - Требует интернет
    // - Может упасть из-за сети
    // - Не тестирует наш сервер
    // - Тестирует только клиент
}
```

### ✅ ПРАВИЛЬНО: Integration test с локальным сервером

```c
// TRUE INTEGRATION TEST
// Тестирует клиент + сервер
void test_http_client_server(void) {
    // 1. Поднять локальный HTTP сервер
    dap_server_t *server = dap_server_new(NULL, "127.0.0.1", 18080, ...);
    dap_http_simple_proc_add(server, "/test", handler, NULL);
    
    // 2. Клиент обращается к ЛОКАЛЬНОМУ серверу
    dap_client_http_request("127.0.0.1", 18080, "/test", ...);
    
    // ТЕСТИРУЕМ:
    // ✅ Клиент → Сервер взаимодействие
    // ✅ Реальный TCP стек
    // ✅ Реальный HTTP протокол
    // ✅ Обе стороны коммуникации
    
    // 3. Cleanup
    dap_server_delete(server);
}
```

### ✅ ПРАВИЛЬНО: Unit test HTTP client с моками

```c
// PURE UNIT TEST
// Тестирует ТОЛЬКО клиент
void test_http_client_unit(void) {
    // Инициализируем mock async (НЕ dap_events!)
    dap_mock_async_init(1);
    
    // Мокируем сетевые функции
    DAP_MOCK_DECLARE_CUSTOM(socket, { .async = true });
    DAP_MOCK_DECLARE_CUSTOM(connect, { .async = true });
    DAP_MOCK_DECLARE_CUSTOM(send, { .async = true });
    DAP_MOCK_DECLARE_CUSTOM(recv, { .async = true });
    
    // Тестируем клиент с моками
    dap_client_http_request(...);
    
    // ТЕСТИРУЕМ:
    // ✅ Логику клиента
    // ✅ Обработку ответов
    // ✅ Error handling
    // ✅ State management
    // ❌ НЕ тестируем сервер
    // ❌ НЕ тестируем сеть
    
    dap_mock_async_deinit();
}
```

## 📁 Структура тестов

```
dap-sdk/
├── net/
│   ├── client/
│   │   ├── test/                    # СТАРЫЕ тесты (deprecated)
│   │   │   └── test_http_client.c   # Unit с моками
│   │   └── ...
│   └── server/
│       ├── test/                    # СТАРЫЕ тесты (deprecated)
│       └── ...
│
└── tests/                           # НОВАЯ структура
    ├── unit/                        # 🎯 Unit Tests
    │   └── net/
    │       ├── client/
    │       │   ├── test_http_client.c        # Unit с моками
    │       │   ├── test_http_client_mocks.c
    │       │   └── test_http_client_mocks.h
    │       ├── server/
    │       │   └── test_http_server.c        # Unit с моками
    │       └── stream/
    │           └── test_stream.c             # Unit с моками
    │
    ├── integration/                 # 🎯 Integration Tests
    │   └── net/
    │       ├── client/
    │       │   └── test_http_client_integration.c  # Client + LOCAL Server
    │       ├── server/
    │       │   └── test_http_server_integration.c  # Server lifecycle
    │       └── stream/
    │           └── test_stream_integration.c       # Stream client + server
    │
    └── fixtures/
        ├── dap_test_helpers.h
        └── dap_common_mocks.h
```

## 🔍 Как определить тип теста?

### Вопрос 1: Используется ли `dap_events_init()`?
- ❌ **Нет** → Unit test
- ✅ **Да** → Integration test

### Вопрос 2: Есть ли реальные сетевые соединения?
- ❌ **Нет** (все мокируется) → Unit test
- ✅ **Да** (реальные TCP sockets) → Integration test

### Вопрос 3: Поднимается ли локальный сервер?
- ❌ **Нет** → Unit test
- ✅ **Да** → Integration test

### Вопрос 4: Сколько модулей тестируется?
- **Один** (с моками) → Unit test
- **Несколько** (реальное взаимодействие) → Integration test

## 📊 Сравнительная таблица

| Аспект | Unit Test | Integration Test |
|--------|-----------|------------------|
| **Скорость** | Очень быстро (<100ms) | Медленно (1-10s) |
| **Изоляция** | Полная (все мокируется) | Частичная (реальные модули) |
| **Детерминизм** | 100% | Могут быть race conditions |
| **Инфраструктура** | `dap_mock`, `dap_mock_async` | `dap_events`, реальные сокеты |
| **Сеть** | Мокируется | Реальная (127.0.0.1) |
| **Сервер** | Мокируется | Поднимается локально |
| **Когда запускать** | При каждом коммите | Перед merge, nightly |
| **Что тестируется** | Логика одного модуля | Взаимодействие модулей |
| **Failures** | Всегда воспроизводимы | Могут быть transient |

## 📝 Правила именования

### Unit Tests
```
test_<module>_unit.c
test_<module>_<feature>.c
test_<module>_mocks.c/h
```

**Примеры:**
- `test_http_client.c` (в `/tests/unit/net/client/`)
- `test_http_client_mocks.c`
- `test_stream_transport.c`

### Integration Tests  
```
test_<module1>_<module2>_integration.c
test_<system>_integration.c
```

**Примеры:**
- `test_http_client_integration.c` (клиент + локальный сервер)
- `test_stream_integration.c` (stream client + server)
- `test_websocket_integration.c` (WS client + WS server)

## ✅ Best Practices

### Unit Tests

1. **Мокируй все внешние зависимости**
   ```c
   DAP_MOCK_DECLARE(socket);
   DAP_MOCK_DECLARE(connect);
   DAP_MOCK_DECLARE(send);
   DAP_MOCK_DECLARE(recv);
   ```

2. **Используй `dap_mock_async` для async APIs**
   ```c
   dap_mock_async_init(1);
   DAP_MOCK_DECLARE_CUSTOM(async_func, { .async = true });
   ```

3. **Один тест = одна функция/сценарий**
   ```c
   test_http_client_connect_success()
   test_http_client_connect_timeout()
   test_http_client_parse_response_200()
   ```

### Integration Tests

1. **Всегда поднимай локальный сервер**
   ```c
   s_http_server = dap_server_new(NULL, "127.0.0.1", 18080, ...);
   ```

2. **Не полагайся на внешние сервисы**
   ```c
   // ❌ ПЛОХО
   dap_client_http_request("httpbin.org", ...);
   
   // ✅ ХОРОШО
   dap_client_http_request("127.0.0.1", TEST_PORT, ...);
   ```

3. **Cleanup после каждого теста**
   ```c
   teardown() {
       dap_server_delete(s_http_server);
       dap_events_deinit();
   }
   ```

4. **Используй уникальные порты**
   ```c
   #define TEST_SERVER_PORT 18080  // Не конфликтует с production
   ```

## 🚀 Миграция старых тестов

### Этап 1: Идентификация
Просмотреть существующие тесты в `net/*/test/` и определить тип:

```bash
# Найти тесты с dap_events_init (integration)
grep -r "dap_events_init" net/*/test/

# Найти тесты с моками (unit)
grep -r "DAP_MOCK" net/*/test/
```

### Этап 2: Категоризация
- Если мокирует сеть → Unit test → `tests/unit/net/`
- Если использует dap_events → Integration test → `tests/integration/net/`

### Этап 3: Рефакторинг

**Unit test:**
1. Добавить моки для всех внешних зависимостей
2. Заменить `dap_events_init()` на `dap_mock_async_init()`
3. Убрать реальные сетевые вызовы
4. Переместить в `tests/unit/net/<module>/`

**Integration test:**
1. Добавить локальный сервер
2. Изменить адреса с external на `127.0.0.1`
3. Добавить setup/teardown для сервера
4. Переместить в `tests/integration/net/<module>/`

### Этап 4: Удаление старых
После проверки новых тестов:
```bash
rm -rf net/client/test/
rm -rf net/server/test/
```

## 📚 Дополнительные материалы

- [Unit Tests README](unit/README.md) - Подробное руководство по unit тестам
- [Integration Tests README](integration/README.md) - Руководство по integration тестам
- [DAP Mock Framework](../test-framework/docs/README.md) - Документация по мокам
- [DAP Mock Async](../test-framework/docs/DAP_MOCK_ASYNC.md) - Async моки для unit тестов

