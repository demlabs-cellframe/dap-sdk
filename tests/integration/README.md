# DAP SDK Integration Tests

## Overview

This directory contains **integration tests** for DAP SDK components. Unlike unit tests (which use mocks), integration tests verify real interactions between components and may require:

- Real network connectivity
- Running services/servers
- File system access
- Database connections
- Multi-process/thread scenarios

## Test Categories

### 1. Network Integration Tests
- HTTP client with real endpoints
- WebSocket connections
- Stream transport with actual protocols
- Multi-hop routing

### 2. Crypto Integration Tests
- Full encryption/decryption cycles
- Certificate chain validation
- Key exchange protocols

### 3. Database Integration Tests
- Global DB operations
- Transaction processing
- Replication scenarios

### 4. End-to-End Tests
- Complete protocol flows
- Service initialization sequences
- Complex state machine scenarios

## Running Integration Tests

```bash
# Build integration tests
cmake -DBUILD_DAP_TESTS=ON ..
make

# Run all integration tests
ctest -L integration

# Run specific integration test
./tests/integration/test_http_integration

# Run with network timeout
TEST_TIMEOUT=30 ./tests/integration/test_websocket_integration
```

## Test Requirements

### Network Tests
- Internet connectivity required
- May use public test servers (httpbin.org, echo.websocket.org)
- Configurable test endpoints via environment variables

### Service Tests
- May require running DAP node instances
- May need specific port availability
- Configuration files in `fixtures/configs/`

## Writing Integration Tests

### Template Structure

```c
#include "dap_test_helpers.h"
#include "dap_test_async.h"

// Setup: Initialize real components
static void setup_integration_test(void) {
    // Initialize real DAP events, workers, etc.
    dap_events_init();
    dap_events_start();
}

// Teardown: Cleanup real resources
static void teardown_integration_test(void) {
    dap_events_stop_all();
    dap_events_deinit();
}

// Test with real network/service
static void test_real_http_connection(void) {
    setup_integration_test();
    
    TEST_INFO("Testing real HTTP connection...");
    
    // Use real DAP client API (no mocks)
    dap_client_http_t *client = dap_client_http_request(...);
    
    // Wait for async completion
    DAP_TEST_WAIT_UNTIL(result_ready, 30000, "HTTP request");
    
    TEST_ASSERT(client->http_resp_code == 200, "Expected 200 OK");
    
    teardown_integration_test();
    TEST_SUCCESS("Real HTTP connection works");
}
```

### Best Practices

1. **Test Isolation**: Each test should be independent
2. **Timeouts**: Always use timeouts for network operations
3. **Error Handling**: Test should handle network failures gracefully
4. **Environment Checks**: Skip tests if required services unavailable
5. **Cleanup**: Always cleanup resources (no leaks)

### Mocking Policy

Integration tests should:
- ✅ Use real network protocols
- ✅ Use real file I/O
- ✅ Use real crypto operations
- ❌ NOT mock core DAP SDK components
- ⚠️ MAY mock external services (if unavailable)

## Integration vs Unit Tests

| Aspect | Unit Tests | Integration Tests |
|--------|-----------|-------------------|
| **Location** | `module/test/` or `tests/unit/` | `tests/integration/` |
| **Mocking** | Heavy (mocks for all dependencies) | Minimal (real components) |
| **Speed** | Fast (<1s per test) | Slower (may take seconds) |
| **Dependencies** | None (fully isolated) | May need network, services |
| **Run Frequency** | Every commit | Pre-merge, nightly |
| **Failures** | Always reproducible | May fail due to network |

## Test Fixtures

Integration test fixtures in `../fixtures/`:
- `test_servers.json` - Test endpoint configurations
- `test_certs/` - Test certificates for TLS
- `test_configs/` - DAP node test configurations

## Continuous Integration

Integration tests in CI:
- Run on merge requests (with retry on network failure)
- Run nightly with full coverage
- May be skipped if test services unavailable
- Results logged separately from unit tests

## Troubleshooting

### Test Timeout
```bash
# Increase timeout
TEST_TIMEOUT=60 ./test_integration
```

### Network Issues
```bash
# Check connectivity
ping httpbin.org

# Use alternative endpoint
TEST_ENDPOINT=http://localhost:8080 ./test_integration
```

### Debug Output
```bash
# Enable verbose logging
DAP_LOG_LEVEL=DEBUG ./test_integration
```

## See Also

- [Unit Tests README](../unit/README.md)
- [Test Fixtures](../fixtures/README.md)
- [DAP Test Framework](../../test-framework/docs/README.md)

