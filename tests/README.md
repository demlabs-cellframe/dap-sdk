# DAP SDK Test Infrastructure

This directory contains the comprehensive test infrastructure for DAP SDK, organized into different categories of tests to ensure thorough coverage and maintainability.

## Directory Structure

```
tests/
├── fixtures/              # Test fixtures and utilities
│   ├── test_data/         # Static test data files
│   ├── utilities/         # Common test helper functions
│   ├── mocks/            # Mock objects for testing
│   └── configs/          # Test configuration files
├── unit/                 # Unit tests
│   ├── core/            # Core module tests (config, json, strings, time)
│   ├── crypto/          # Crypto module tests
│   │   ├── hash/        # Hash algorithm tests
│   │   ├── sign/        # Signature algorithm tests
│   │   └── enc/         # Encryption algorithm tests
│   ├── io/              # I/O module tests
│   ├── net/             # Network module tests
│   └── global-db/       # Global database tests
├── integration/          # Integration tests between modules
├── e2e/                 # End-to-end workflow tests
├── functional/          # Functional API tests
├── regression/          # Regression tests for known issues
├── performance/         # Performance benchmarks and tests
├── security/            # Security and vulnerability tests
├── CMakeLists.txt       # Build configuration
└── README.md           # This file
```

## Test Categories

### Unit Tests (`unit/`)
- **Purpose**: Test individual functions and modules in isolation
- **Scope**: Single functions or small components
- **Examples**: Hash function tests, JSON parsing tests, key generation tests
- **Run with**: `make test_unit` or `ctest -L unit`

### Integration Tests (`integration/`)
- **Purpose**: Test interaction between different modules
- **Scope**: Multiple modules working together
- **Examples**: Crypto + Network integration, Config + Modules interaction
- **Run with**: `make test_integration` or `ctest -L integration`

### End-to-End Tests (`e2e/`)
- **Purpose**: Test complete workflows from start to finish
- **Scope**: Full system functionality
- **Examples**: Complete transaction signing workflow, Node initialization to shutdown
- **Run with**: `make test_e2e` or `ctest -L e2e`

### Functional Tests (`functional/`)
- **Purpose**: Test API functionality as used by applications
- **Scope**: Public API behavior and usability
- **Examples**: JSON API usage patterns, Crypto API workflows
- **Run with**: `ctest -L functional`

### Regression Tests (`regression/`)
- **Purpose**: Prevent reoccurrence of previously fixed bugs
- **Scope**: Specific bug scenarios and edge cases
- **Examples**: Known JSON parsing issues, Memory leak scenarios
- **Run with**: `ctest -L regression`

### Performance Tests (`performance/`)
- **Purpose**: Benchmark and validate performance characteristics
- **Scope**: Speed, memory usage, throughput measurements
- **Examples**: Hash performance benchmarks, Signature creation/verification speed
- **Run with**: `make test_performance` or `ctest -L performance`

### Security Tests (`security/`)
- **Purpose**: Validate security properties and vulnerability resistance
- **Scope**: Input validation, buffer overflow prevention, memory safety
- **Examples**: Input fuzzing, Buffer overflow tests, Memory safety validation
- **Run with**: `ctest -L security`

## Building and Running Tests

### Prerequisites
- CMake 3.10 or higher
- DAP SDK built with tests enabled (`BUILD_TESTS=ON`)
- Required dependencies: pthread, libm (on Unix systems)

### Build Tests
```bash
cd dap-sdk
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
make
```

### Run All Tests
```bash
# Using CTest (recommended)
ctest

# Using make targets
make test_all
```

### Run Specific Test Categories
```bash
# Unit tests only
make test_unit

# Integration tests only
make test_integration

# E2E tests only
make test_e2e

# Performance tests only
make test_performance

# Crypto-related tests across all categories
make test_crypto

# Using CTest labels
ctest -L unit           # Unit tests
ctest -L crypto         # All crypto tests
ctest -L performance    # Performance tests
```

### Run Individual Tests
```bash
# Run specific test binary
./bin/test_unit_crypto_sha3

# Run specific test with CTest
ctest -R test_unit_crypto_sha3
```

### Verbose Output
```bash
# Detailed test output
ctest --output-on-failure --verbose

# Debug logging (if supported by test)
DAP_LOG_LEVEL=DEBUG ctest
```

## Test Fixtures and Utilities

### Test Helpers (`fixtures/utilities/`)
Common utilities for all tests:
- `test_helpers.h/c`: Memory tracking, timing, assertions, random data generation
- Assertion macros: `DAP_TEST_ASSERT`, `DAP_TEST_ASSERT_NOT_NULL`, etc.
- Performance timing: `dap_test_timer_start()`, `dap_test_timer_stop()`
- Memory utilities: `dap_test_mem_alloc()`, `dap_test_mem_free()`

### Test Data (`fixtures/`)
- `json_samples.h`: Sample JSON data for testing
- Crypto test vectors and sample data
- Configuration templates for testing

### Usage Example
```c
#include "test_helpers.h"
#include "json_samples.h"

static bool s_my_test_function(void) {
    // Initialize test environment
    if (dap_test_sdk_init() != 0) {
        return false;
    }
    
    // Use assertions
    DAP_TEST_ASSERT_NOT_NULL(some_pointer, "Pointer validation");
    DAP_TEST_ASSERT_EQUAL(expected, actual, "Value comparison");
    
    // Performance timing
    dap_test_timer_t timer;
    dap_test_timer_start(&timer);
    // ... operation to time ...
    uint64_t elapsed = dap_test_timer_stop(&timer);
    
    // Cleanup
    dap_test_sdk_cleanup();
    return true;
}
```

## Writing New Tests

### Guidelines
1. **Follow naming conventions**: `test_[category]_[module]_[function].c`
2. **Use test helpers**: Include `test_helpers.h` and use provided utilities
3. **Proper cleanup**: Always free allocated memory and resources
4. **Clear logging**: Use descriptive log messages with appropriate levels
5. **Modular design**: Keep tests focused and independent

### Test Structure Template
```c
#include "dap_common.h"
#include "test_helpers.h"

#define LOG_TAG "test_my_module"

static bool s_test_specific_functionality(void) {
    log_it(L_DEBUG, "Testing specific functionality");
    
    // Test setup
    // ... test implementation ...
    // Assertions and validation
    // Cleanup
    
    log_it(L_DEBUG, "Specific functionality test passed");
    return true;
}

int main(void) {
    log_it(L_INFO, "Starting My Module Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool all_passed = true;
    all_passed &= s_test_specific_functionality();
    // ... more tests ...
    
    dap_test_sdk_cleanup();
    
    if (all_passed) {
        log_it(L_INFO, "All tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some tests failed!");
        return -1;
    }
}
```

### Adding Tests to CMake
1. Add your test source file to the appropriate directory
2. Update `tests/CMakeLists.txt` if needed (auto-discovery is supported)
3. Rebuild the test suite
4. Test will be automatically discovered and added to CTest

## Continuous Integration

### GitLab CI Integration
Tests are automatically run in CI/CD pipeline:
- Unit tests run on every commit
- Integration tests run on merge requests
- Performance tests run on release branches
- Security tests run weekly

### Test Reports
- CTest generates XML reports for CI integration
- Coverage reports available when built with coverage flags
- Performance benchmarks tracked over time

## Troubleshooting

### Common Issues
1. **Test timeouts**: Increase timeout in CMakeLists.txt or run with `--timeout`
2. **Memory leaks**: Use test memory utilities and check cleanup
3. **Platform differences**: Use cross-platform utilities from test helpers
4. **Missing dependencies**: Ensure all required libraries are linked

### Debug Tests
```bash
# Run with debugging
gdb ./bin/test_unit_crypto_sha3

# Memory leak detection
valgrind --leak-check=full ./bin/test_unit_crypto_sha3

# Enable debug logging
DAP_LOG_LEVEL=DEBUG ./bin/test_unit_crypto_sha3
```

## Contributing

1. **Add tests for new features**: Every new function should have corresponding tests
2. **Update existing tests**: When modifying code, update relevant tests
3. **Follow conventions**: Use established patterns and naming conventions
4. **Document test purpose**: Include clear descriptions of what each test validates
5. **Consider all categories**: Unit tests are mandatory, consider if integration/e2e tests are needed

## Performance Baselines

Current performance expectations (subject to change):
- SHA3-256: >1000 hashes/sec
- Dilithium signing: >10 signatures/sec  
- Dilithium verification: >100 verifications/sec
- JSON parsing: >1000 documents/sec

Tests will fail if performance falls below these thresholds, indicating potential regressions.

