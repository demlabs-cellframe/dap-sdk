## 3. API Reference

### 3.1 Async Testing API

#### Global Timeout
```c
int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);
// Returns: 0 on setup, 1 if timeout triggered

void dap_test_cancel_global_timeout(void);
```

#### Condition Polling
```c
bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);
// Returns: true if condition met, false on timeout
```

#### pthread Helpers
```c
void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);
bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);
void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);
```

#### Time Utilities
```c
uint64_t dap_test_get_time_ms(void);  // Monotonic time in ms
void dap_test_sleep_ms(uint32_t a_delay_ms);  // Cross-platform sleep
```

#### Macros
```c
DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg)
// Quick inline condition waiting
```

### 3.2 Mock Framework API

#### Declaration
```c
DAP_MOCK_DECLARE(func_name);
DAP_MOCK_DECLARE(func_name, {.return_value.i = 42});
DAP_MOCK_DECLARE(func_name, {.return_value.i = 0}, { /* callback */ });
```

#### Control Macros
```c
DAP_MOCK_ENABLE(func_name)
DAP_MOCK_DISABLE(func_name)
DAP_MOCK_RESET(func_name)
DAP_MOCK_SET_RETURN(func_name, value)
DAP_MOCK_GET_CALL_COUNT(func_name)
```

#### Delay Configuration
```c
DAP_MOCK_SET_DELAY_FIXED(func_name, microseconds)
DAP_MOCK_SET_DELAY_FIXED_MS(func_name, milliseconds)
DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us)
DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us)
DAP_MOCK_CLEAR_DELAY(func_name)
```

#### Callback Configuration
```c
DAP_MOCK_SET_CALLBACK(func_name, callback_func, user_data)
DAP_MOCK_CLEAR_CALLBACK(func_name)
```

\newpage
