## 6. Troubleshooting

### Issue: Test Hangs Indefinitely
**Symptom:** Test runs forever without completing  
**Cause:** Async operation never signals completion  
**Solution:** Add global timeout protection
```c
dap_test_global_timeout_t timeout;
if (dap_test_set_global_timeout(&timeout, 30, "Tests")) {
    log_it(L_ERROR, "Test timeout!");
}
```
**Prevention:** Always use `DAP_TEST_WAIT_UNTIL` with reasonable timeout

### Issue: High CPU
**Symptom:** 100% CPU during test  
**Solution:** Increase poll interval or use pthread helpers
```c
cfg.poll_interval_ms = 500;  // Less frequent polling
```

### Issue: Mock Not Called (Real Function Executes)
**Symptom:** Real function executes instead of mock  
**Cause:** Missing linker `--wrap` flag  
**Solution:** Verify CMake configuration and linker flags
```bash
# Check if linker flags are present
make VERBOSE=1 | grep -- "--wrap"

# Should see: -Wl,--wrap=function_name
```
**Fix:** Ensure `dap_mock_autowrap(target)` is called after `add_executable()`

### Issue: Wrong Return Value
**Symptom:** Mock returns unexpected value  
**Solution:** Use correct union field
```c
.return_value.i = 42      // int
.return_value.l = 0xDEAD  // pointer
.return_value.ptr = ptr   // void*
```

### Issue: Flaky Tests (Intermittent Failures)
**Symptom:** Sometimes pass, sometimes fail  
**Cause:** Race conditions, insufficient timeouts, or timing assumptions  
**Solution:** Increase timeouts and add tolerance for timing-sensitive checks
```c
// For network operations - use generous timeout
cfg.timeout_ms = 60000;  // 60 sec for network operations

// For timing checks - use tolerance range
uint64_t elapsed = measure_time();
assert(elapsed >= 90 && elapsed <= 150);  // ±50ms tolerance

// Use variance delay for realistic simulation
DAP_MOCK_SET_DELAY_VARIANCE(func, 100000, 50000);  // 100ms ± 50ms
```

### Issue: Compilation Error "undefined reference to __wrap"
**Symptom:** Linker error about `__wrap_function_name`  
**Solution:** Ensure `dap_mock_autowrap()` is called in CMakeLists.txt
```cmake
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)
dap_mock_autowrap(my_test)
```

### Issue: Mock Callback Not Executing
**Symptom:** Mock returns configured value, but callback logic doesn't run  
**Cause:** Callback not registered or mock disabled  
**Solution:** Verify callback is set and mock is enabled
```c
// Declare with inline callback (preferred)
DAP_MOCK_DECLARE(func_name, {.enabled = true}, {
    // Your callback logic here
    return (void*)42;
});

// Or set callback at runtime
DAP_MOCK_SET_CALLBACK(func_name, my_callback, user_data);

// Ensure mock is enabled
DAP_MOCK_ENABLE(func_name);
```
**Note:** Callback return value overrides `.return_value` configuration

### Issue: Delay Not Working
**Symptom:** Mock executes instantly despite delay config  
**Solution:** Verify delay is set after mock declaration
```c
DAP_MOCK_DECLARE(func_name);
DAP_MOCK_SET_DELAY_MS(func_name, 100);  // Set after declare
```

\newpage
