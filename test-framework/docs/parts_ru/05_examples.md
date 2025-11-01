## 4. Полные примеры

### 4.1 Тест стейт-машины (Пример из реального проекта)

Пример из `cellframe-srv-vpn-client/tests/unit/test_vpn_state_handlers.c`:

```c
#include "dap_test.h"
#include "dap_mock.h"
#include "vpn_state_machine.h"
#include "vpn_state_handlers_internal.h"

#define LOG_TAG "test_vpn_state_handlers"

// Объявление моков с простой конфигурацией
DAP_MOCK_DECLARE(dap_net_tun_deinit);
DAP_MOCK_DECLARE(dap_chain_node_client_close_mt);
DAP_MOCK_DECLARE(vpn_wallet_close);

// Мок с конфигурацией возвращаемого значения
DAP_MOCK_DECLARE(dap_chain_node_client_connect_mt, {
    .return_value.l = 0xDEADBEEF
});

static vpn_sm_t *s_test_sm = NULL;

static void setup_test(void) {
    // Примечание: dap_mock_init() вызывается авто, здесь не нужен
    s_test_sm = vpn_sm_init();
    assert(s_test_sm != NULL);
}

static void teardown_test(void) {
    if (s_test_sm) {
        vpn_sm_deinit(s_test_sm);
        s_test_sm = NULL;
    }
    // Опционально: dap_mock_deinit() для сброса моков между тестами
}

void test_state_disconnected_cleanup(void) {
    log_it(L_INFO, "ТЕСТ: state_disconnected_entry() очистка");
    
    setup_test();
    
    // Настройка состояния с ресурсами
    s_test_sm->tun_handle = (void*)0x12345678;
    s_test_sm->wallet = (void*)0xABCDEF00;
    s_test_sm->node_client = (void*)0x22222222;
    
    // Включение моков
    DAP_MOCK_ENABLE(dap_net_tun_deinit);
    DAP_MOCK_ENABLE(vpn_wallet_close);
    DAP_MOCK_ENABLE(dap_chain_node_client_close_mt);
    
    // Вызов обработчика состояния
    state_disconnected_entry(s_test_sm);
    
    // Проверка выполнения очистки
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_deinit) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(vpn_wallet_close) == 1);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_chain_node_client_close_mt) == 1);
    
    teardown_test();
    log_it(L_INFO, "[+] УСПЕХ");
}

int main() {
    dap_common_init("test_vpn_state_handlers", NULL);
    
    test_state_disconnected_cleanup();
    
    log_it(L_INFO, "Все тесты ПРОЙДЕНЫ [OK]");
    dap_common_deinit();
    return 0;
}
```

### 4.2 Мок с callback

```c
#include "dap_mock.h"

DAP_MOCK_DECLARE(dap_hash_fast, {.return_value.i = 0}, {
    if (a_arg_count >= 2) {
        uint8_t *data = (uint8_t*)a_args[0];
        size_t size = (size_t)a_args[1];
        uint32_t hash = 0;
        for (size_t i = 0; i < size; i++) {
            hash += data[i];
        }
        return (void*)(intptr_t)hash;
    }
    return (void*)0;
});

void test_hash() {
    uint8_t data[] = {1, 2, 3};
    uint32_t hash = dap_hash_fast(data, 3);
    assert(hash == 6);  // Callback суммирует байты
}
```

### 4.3 Мок с задержками выполнения

Пример из `dap-sdk/net/client/test/test_http_client_mocks.h`:

```c
#include "dap_mock.h"

// Мок с задержкой variance: симулирует реалистичные колебания сети
// 100мс ± 50мс = диапазон 50-150мс
#define HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY ((dap_mock_config_t){ \
    .enabled = true, \
    .delay = { \
        .type = DAP_MOCK_DELAY_VARIANCE, \
        .variance = { \
            .center_us = 100000,   /* центр 100мс */ \
            .variance_us = 50000   /* разброс ±50мс */ \
        } \
    } \
})

// Объявление мока с симуляцией сетевой задержки
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_full, 
                        HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY);

// Мок без задержки для операций очистки (мгновенное выполнение)
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_close_unsafe, {
    .enabled = true,
    .delay = {.type = DAP_MOCK_DELAY_NONE}
});
```

### 4.4 Пользовательская линкер-обертка (Продвинутый уровень)

Пример из `test_http_client_mocks.c` с использованием `DAP_MOCK_WRAPPER_CUSTOM`:

```c
#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"
#include "dap_client_http.h"

// Объявление мока (регистрация во фреймворке)
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_async, 
                        HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY);

// Реализация пользовательской обертки с полным контролем
// DAP_MOCK_WRAPPER_CUSTOM генерирует:
// - сигнатуру функции __wrap_dap_client_http_request_async
// - массив void* args для фреймворка моков
// - Автоматическое выполнение задержки
// - Запись вызова
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request_async,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_path),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg)
) {
    // Пользовательская логика мока - симуляция асинхронного HTTP поведения
    // Это напрямую вызывает callback'и на основе конфигурации мока
    
    if (g_mock_http_response.should_fail && a_error_callback) {
        // Симуляция ошибочного ответа
        a_error_callback(g_mock_http_response.error_code, a_callbacks_arg);
    } else if (a_response_callback) {
        // Симуляция успешного ответа с настроенными данными
        a_response_callback(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            g_mock_http_response.headers,
            a_callbacks_arg,
            g_mock_http_response.status_code
        );
    }
    // Примечание: настроенная задержка выполняется автоматически перед этим кодом
}
```

**CMakeLists.txt:**
```cmake
# Подключение auto-wrap помощника
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_http_client 
    test_http_client_mocks.c 
    test_http_client_mocks.h
    test_main.c
)

target_link_libraries(test_http_client
    dap_test     # Тест-фреймворк с моками
    dap_core     # Библиотека DAP core
    pthread      # Поддержка многопоточности
)

# Автогенерация --wrap флагов линкера сканированием всех исходников
dap_mock_autowrap(test_http_client)
```

### 4.5 Динамическое поведение мока

```c
// Мок, который меняет поведение на основе счетчика вызовов
// Симулирует нестабильную сеть: ошибка 2 раза, затем успех
DAP_MOCK_DECLARE(flaky_network_send, {.return_value.i = 0}, {
    int call_count = DAP_MOCK_GET_CALL_COUNT(flaky_network_send);
    
    // Ошибка в первых 2 вызовах (симуляция сетевых проблем)
    if (call_count < 2) {
        log_it(L_DEBUG, "Симуляция сетевого сбоя (попытка %d)", call_count + 1);
        return (void*)(intptr_t)-1;  // Код ошибки
    }
    
    // Успех с 3-го и последующих вызовов
    log_it(L_DEBUG, "Сетевой вызов успешен");
    return (void*)(intptr_t)0;  // Код успеха
});

void test_retry_logic() {
    // Тест функции с повторными попытками при ошибке
    int result = send_with_retry(data, 3);  // Максимум 3 попытки
    
    // Должен завершиться успешно на 3-й попытке
    assert(result == 0);
    assert(DAP_MOCK_GET_CALL_COUNT(flaky_network_send) == 3);
    
    log_it(L_INFO, "[+] Логика повторных попыток работает корректно");
}
```

### 4.6 Мокирование в статических библиотеках

Пример теста, который мокирует функции внутри статической библиотеки `dap_stream`:

**CMakeLists.txt:**
```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(test_stream_mocks
    test_stream_mocks.c
    test_stream_mocks_wrappers.c
)

target_link_libraries(test_stream_mocks
    dap_test
    dap_stream       # Статическая библиотека - функции внутри нужно мокировать
    dap_net
    dap_core
    pthread
)

target_include_directories(test_stream_mocks PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework
    ${CMAKE_SOURCE_DIR}/dap-sdk/core/include
)

# Шаг 1: Автогенерация --wrap флагов из исходников теста
dap_mock_autowrap(test_stream_mocks)

# Шаг 2: Оборачивание статической библиотеки --whole-archive
# Это заставляет линкер включить все символы из dap_stream,
# включая внутренние функции, которые нужно мокировать
dap_mock_autowrap_with_static(test_stream_mocks dap_stream)
```

**test_stream_mocks.c:**
```c
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_stream.h"
#include "dap_common.h"
#include <assert.h>

#define LOG_TAG "test_stream_mocks"

// Мокируем функцию, которая используется внутри dap_stream
DAP_MOCK_DECLARE(dap_net_tun_write, {
    .return_value.i = 0,  // Успешная запись
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 10000  // 10ms задержка
    }
});

// Оборачиваем функцию для мокирования
DAP_MOCK_WRAPPER_CUSTOM(int, dap_net_tun_write,
    PARAM(int, a_fd),
    PARAM(const void*, a_buf),
    PARAM(size_t, a_len)
) {
    // Логика мока - симулируем успешную запись
    log_it(L_DEBUG, "Mock: dap_net_tun_write called (fd=%d, len=%zu)", a_fd, a_len);
    return 0;
}

void test_stream_write_with_mock(void) {
    log_it(L_INFO, "TEST: Stream write with mocked tun_write");
    
    // Создаём стрим (dap_stream использует dap_net_tun_write внутри)
    dap_stream_t *stream = dap_stream_create(...);
    assert(stream != NULL);
    
    // Выполняем запись - должна использовать мок dap_net_tun_write
    int result = dap_stream_write(stream, "test data", 9);
    
    // Проверяем что мок был вызван
    assert(result == 0);
    assert(DAP_MOCK_GET_CALL_COUNT(dap_net_tun_write) > 0);
    
    dap_stream_delete(stream);
    log_it(L_INFO, "[+] Test passed");
}

int main() {
    dap_common_init("test_stream_mocks", NULL);
    
    test_stream_write_with_mock();
    
    dap_common_deinit();
    return 0;
}
```

**Ключевые моменты:**
1. `dap_mock_autowrap()` должно быть вызвано **до** `dap_mock_autowrap_with_static()`
2. Укажите все статические библиотеки, в которых нужно мокировать функции
3. `--whole-archive` может увеличить размер исполняемого файла
4. Работает только с GCC, Clang и MinGW

### 4.7 Асинхронное выполнение моков

Пример демонстрации async mock callback'ов с thread pool:

```c
#include "dap_mock.h"
#include "dap_mock_async.h"
#include "dap_test_async.h"

// Async мок для HTTP запроса с задержкой 50ms
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request, {
    .enabled = true,
    .async = true,  // Выполнять в worker потоке
    .delay = {
        .type = DAP_MOCK_DELAY_FIXED,
        .fixed_us = 50000  // 50ms реалистичная сетевая латентность
    }
});

// Mock обертка - выполняется асинхронно
DAP_MOCK_WRAPPER_CUSTOM(int, dap_client_http_request,
    PARAM(const char*, a_url),
    PARAM(http_callback_t, a_callback),
    PARAM(void*, a_arg)
) {
    // Этот код выполняется в worker потоке после задержки 50ms
    const char *response = "{\"status\":\"ok\",\"data\":\"test\"}";
    a_callback(response, 200, a_arg);
    return 0;
}

static volatile bool s_callback_executed = false;
static volatile int s_http_status = 0;

static void http_response_callback(const char *body, int status, void *arg) {
    s_http_status = status;
    s_callback_executed = true;
    log_it(L_INFO, "HTTP ответ получен: status=%d", status);
}

void test_async_http_request(void) {
    log_it(L_INFO, "TEST: Async HTTP request");
    
    // Инициализировать async mock систему с 1 worker потоком
    dap_mock_async_init(1);
    
    s_callback_executed = false;
    s_http_status = 0;
    
    // Вызвать HTTP запрос - мок выполнится асинхронно
    int result = dap_client_http_request(
        "http://test.com/api",
        http_response_callback,
        NULL
    );
    
    assert(result == 0);
    log_it(L_DEBUG, "HTTP запрос инициирован, ждём callback...");
    
    // Ждать завершения async мока (до 5 секунд)
    DAP_TEST_WAIT_UNTIL(s_callback_executed, 5000, "HTTP callback");
    
    // Проверка
    assert(s_callback_executed);
    assert(s_http_status == 200);
    
    // Альтернатива: ждать все async моки
    bool all_completed = dap_mock_async_wait_all(5000);
    assert(all_completed);
    
    log_it(L_INFO, "[+] Async mock тест пройден");
    
    // Очистка async системы
    dap_mock_async_deinit();
}

// Пример fast-forward: тест без реальных задержек
void test_async_with_flush(void) {
    dap_mock_async_init(1);
    
    s_callback_executed = false;
    
    // Запланировать async задачу с большой задержкой
    dap_client_http_request("http://test.com", http_response_callback, NULL);
    
    // Вместо ожидания 50ms, выполнить немедленно
    dap_mock_async_flush();  // "Промотать" время
    
    // Callback уже выполнен
    assert(s_callback_executed);
    
    log_it(L_INFO, "[+] Fast-forward тест пройден");
    dap_mock_async_deinit();
}
```

**Преимущества Async Моков:**
- Реалистичная симуляция сетевой/IO латентности
- Не требуется полная инфраструктура `dap_events` в unit тестах
- Потокобезопасное выполнение
- Детерминированное тестирование с `flush()`
- Отслеживание статистики с `get_pending_count()` / `get_completed_count()`

\newpage
