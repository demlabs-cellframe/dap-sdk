## 6. Troubleshooting

### Issue: Test Hangs
**Symptom:** Test runs forever  
**Solution:** Add global timeout
```c
dap_test_set_global_timeout(&timeout, 30, "Tests");
```

### Issue: High CPU
**Symptom:** 100% CPU during test  
**Solution:** Increase poll interval or use pthread helpers
```c
cfg.poll_interval_ms = 500;  // Less frequent polling
```

### Issue: Mock Not Called
**Symptom:** Real function executes  
**Solution:** Check linker flags
```bash
make VERBOSE=1 | grep -- "--wrap"
```

### Issue: Wrong Return Value
**Symptom:** Mock returns unexpected value  
**Solution:** Use correct union field
```c
.return_value.i = 42      // int
.return_value.l = 0xDEAD  // pointer
.return_value.ptr = ptr   // void*
```

### Issue: Flaky Tests
**Symptom:** Sometimes pass, sometimes fail  
**Solution:** Increase timeout, add tolerance
```c
cfg.timeout_ms = 60000;  // 60 sec for network
assert(elapsed >= 90 && elapsed <= 150);  // ±50ms tolerance
```

\newpage
