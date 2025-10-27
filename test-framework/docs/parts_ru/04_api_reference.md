## 3. Справочник API

### 3.1 Async Testing API

#### Глобальный таймаут
```c
int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);
// Возвращает: 0 при настройке, 1 если таймаут сработал

void dap_test_cancel_global_timeout(void);
```

#### Опрос условий
```c
bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);
// Возвращает: true если условие выполнено, false при таймауте
```

#### pthread хелперы
```c
void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);
bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);
void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);
```

#### Утилиты времени
```c
uint64_t dap_test_get_time_ms(void);  // Монотонное время в мс
void dap_test_sleep_ms(uint32_t a_delay_ms);  // Кроссплатформенный sleep
```

#### Макросы
```c
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg)
// Быстрое ожидание условия
```

### 3.2 Mock Framework API

#### Объявление
```c
DAP_MOCK_DECLARE(func_name);
DAP_MOCK_DECLARE(func_name, {.return_value.i = 42});
DAP_MOCK_DECLARE(func_name, {.return_value.i = 0}, { /* callback */ });
```

#### Макросы управления
```c
DAP_MOCK_ENABLE(func_name)          // Включить мок
DAP_MOCK_DISABLE(func_name)         // Выключить мок
DAP_MOCK_RESET(func_name)           // Сбросить состояние
DAP_MOCK_SET_RETURN(func_name, value)  // Установить возвращаемое значение
DAP_MOCK_GET_CALL_COUNT(func_name)     // Получить счётчик вызовов
```

#### Конфигурация задержек
```c
DAP_MOCK_SET_DELAY_FIXED(func_name, microseconds)  // Фиксированная задержка
DAP_MOCK_SET_DELAY_FIXED_MS(func_name, milliseconds)  // В миллисекундах
DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us)  // Диапазон
DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us)  // Разброс
DAP_MOCK_CLEAR_DELAY(func_name)  // Очистить задержку
```

\newpage
