# Part I: Introduction

## 1. Overview

The DAP SDK Test Framework is a production-ready testing infrastructure designed for the Cellframe blockchain ecosystem. It provides comprehensive tools for testing asynchronous operations, mocking external dependencies, and ensuring reliable test execution across platforms.

### 1.1 What is DAP SDK Test Framework?

A complete testing solution that includes:

- **Async Testing Framework** - Tools for testing asynchronous operations with timeouts
- **Mock Framework V4** - Function mocking without code modification
- **Async Mock Execution** - Asynchronous mock callbacks with thread pool
- **Auto-Wrapper System** - Automatic linker configuration
- **Self-Tests** - 21 tests validating framework reliability

### 1.2 Why Use This Framework?

**Problem:** Testing asynchronous code is hard
- Operations complete at unpredictable times
- Network delays vary
- Tests can hang indefinitely
- External dependencies complicate testing

**Solution:** This framework provides
- [x] Timeout protection (global + per-operation)
- [x] Efficient waiting (polling + condition variables)
- [x] Dependency isolation (mocking)
- [x] Realistic simulation (delays, failures)
- [x] Thread-safe operations
- [x] Cross-platform support

### 1.3 Key Features at a Glance

| Feature | Description | Benefit |
|---------|-------------|---------|
| Global Timeout | alarm + siglongjmp | Prevents CI/CD hangs |
| Condition Polling | Configurable intervals | Efficient async waiting |
| pthread Helpers | Condition variable wrappers | Thread-safe coordination |
| Mock Framework | Linker-based (`--wrap`) | Zero technical debt |
| Async Mocks | Thread pool execution | Real async behavior simulation |
| Delays | Fixed, Range, Variance | Realistic timing simulation |
| Callbacks | Inline + Runtime | Dynamic mock behavior |
| Auto-Wrapper | Bash/PowerShell scripts | Automatic setup |
| Self-Tests | 21 comprehensive tests | Validated reliability |

### 1.4 Quick Comparison

**Traditional Approach:**
```c
// [!] Busy waiting, no timeout, CPU waste
while (!done) {
    usleep(10000);  // 10ms sleep
}
```

**With DAP Test Framework:**
```c
// [+] Efficient, timeout-protected, automatic logging
DAP_TEST_WAIT_UNTIL(done == true, 5000, "Should complete");
```

### 1.5 Target Audience

- DAP SDK developers
- Cellframe SDK contributors
- VPN Client developers
- Anyone testing async C code in Cellframe ecosystem

### 1.6 Prerequisites

**Required Knowledge:**
- C programming
- Basic understanding of async operations
- CMake basics
- pthread concepts (for advanced features)

**Required Software:**
- GCC 7+ or Clang 10+ (or MinGW on Windows)
- CMake 3.10+
- pthread library
- Linux, macOS, or Windows (partial support)

\newpage

