## 4. Примеры использования

### 4.1 Тест стейт-машины

```c
#include "dap_test.h"
#include "dap_test_async.h"
#include "vpn_state_machine.h"

#define LOG_TAG "test_vpn_sm"
#define TIMEOUT_SEC 30

bool check_connected(void *data) {
    return vpn_sm_get_state((vpn_sm_t*)data) == VPN_STATE_CONNECTED;
}

void test_connection() {
    vpn_sm_t *sm = vpn_sm_init();
    vpn_sm_transition(sm, VPN_EVENT_USER_CONNECT);
    
    dap_test_async_config_t cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    cfg.timeout_ms = 10000;
    cfg.operation_name = "VPN connection";
    
    bool ok = dap_test_wait_condition(check_connected, sm, &cfg);
    dap_assert_PIF(ok, "Should connect within 10 sec");
    
    vpn_sm_deinit(sm);
}

int main() {
    dap_common_init("test_vpn_sm", NULL);
    
    dap_test_global_timeout_t timeout;
    if (dap_test_set_global_timeout(&timeout, TIMEOUT_SEC, "VPN Tests")) {
        return 1;
    }
    
    test_connection();
    
    dap_test_cancel_global_timeout();
    dap_common_deinit();
    return 0;
}
```

### 4.2 Мок с callback

```c
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

\newpage
