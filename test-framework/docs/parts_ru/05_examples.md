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
    dap_mock_init();
    s_test_sm = vpn_sm_init();
    assert(s_test_sm != NULL);
}

static void teardown_test(void) {
    if (s_test_sm) {
        vpn_sm_deinit(s_test_sm);
        s_test_sm = NULL;
    }
    dap_mock_deinit();
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

\newpage
