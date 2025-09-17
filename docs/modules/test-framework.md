# DAP Test Framework Module (dap_test.h)

## Overview

The `dap_test_framework` module provides a unified testing system for the DAP SDK. It includes:

- **Unit-test macros** - convenient assertion helpers
- **Performance benchmarks** - execution time measurement
- **Test data generators** - random data creation for tests
- **Colored output** - visual indication of test status
- **Automation** - CI/CD integration

## Architectural role

The Test Framework is an integral part of the DAP SDK QA process:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK       │───▶│ Test Framework  │
│   Modules       │    └─────────────────┘
└─────────────────┘             │
         │                     │
    ┌────▼────┐           ┌────▼────┐
    │Unit     │           │Benchmarks│
    │tests    │           │& Perf.   │
    └─────────┘           └─────────┘
         │                     │
    ┌────▼────┐           ┌────▼────┐
    │CI/CD     │◄──────────►│Reports   │
    │integration│          │results   │
    └─────────┘           └─────────┘
```

## Main components

### 1. Core testing macros

#### `dap_assert(expr, testname)`
```c
#define dap_assert(expr, testname) { \
    if(expr) { \
        printf("\t%s%s PASS.%s\n", TEXT_COLOR_GRN, testname, TEXT_COLOR_RESET); \
        fflush(stdout); \
    } else { \
        printf("\t%s%s FAILED!%s\n", TEXT_COLOR_RED, testname, TEXT_COLOR_RESET); \
        fflush(stdout); \
        abort(); } }
```

**Parameters:**
- `expr` - boolean expression to verify
- `testname` - test name for output

**Example:**
```c
dap_assert(result == expected, "Basic arithmetic test");
```

#### `dap_assert_PIF(expr, msg)`
```c
#define dap_assert_PIF(expr, msg) { \
    if(expr) {} \
    else { \
    printf("\t%s%s FAILED!%s\n", TEXT_COLOR_RED, msg, TEXT_COLOR_RESET); \
    fflush(stdout); \
    abort(); } }
```

**Notes:**
- PIF = "Print If Failed" — prints only on failure
- Suitable for checks in loops

**Example:**
```c
for (int i = 0; i < 1000; i++) {
    int result = complex_calculation(i);
    dap_assert_PIF(result > 0, "Complex calculation failed");
}
```

### 2. Helper macros

#### `dap_test_msg(...)`
```c
#define dap_test_msg(...) { \
    printf("\t%s", TEXT_COLOR_WHT); \
    printf(__VA_ARGS__); \
    printf("%s\n", TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Purpose:**
- Output debug info during testing
- Does not affect the test result

**Example:**
```c
dap_test_msg("Processing item %d of %d", current, total);
```

#### `dap_pass_msg(testname)`
```c
#define dap_pass_msg(testname) { \
    printf("\t%s%s PASS.%s\n", TEXT_COLOR_GRN, testname, TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Purpose:**
- Manually mark a test as passed
- Useful when automatic verification is not possible

#### `dap_fail(msg)`
```c
#define dap_fail(msg) {\
    printf("\t%s%s!%s\n", TEXT_COLOR_RED, msg, TEXT_COLOR_RESET); \
    fflush(stdout); \
    abort();}
```

**Purpose:**
- Immediately terminate the test with an error
- For critical failures that require stopping

### 3. Module macros

#### `dap_print_module_name(module_name)`
```c
#define dap_print_module_name(module_name) { \
    printf("%s%s passing the tests... %s\n", TEXT_COLOR_CYN, module_name, TEXT_COLOR_RESET); \
    fflush(stdout); }
```

**Purpose:**
- Print a header for a module test group
- Visual separation of tests by module

**Example:**
```c
dap_print_module_name("Crypto Module Tests");
dap_assert(test_sha3(), "SHA3 hash function");
dap_assert(test_aes_encrypt(), "AES encryption");
```

### 4. String utilities

#### `dap_str_equals(str1, str2)`
```c
#define dap_str_equals(str1, str2) strcmp(str1, str2) == 0
```

#### `dap_strn_equals(str1, str2, count)`
```c
#define dap_strn_equals(str1, str2, count) strncmp(str1, str2, count) == 0
```

**Purpose:**
- Convenient string comparison helpers
- Integration with testing macros

**Example:**
```c
dap_assert(dap_str_equals(result, "expected"), "String comparison");
```

## Benchmarking system

### Time measurement functions

#### `benchmark_test_time()`
```c
int benchmark_test_time(void (*func_name)(void), int repeat);
```

**Parameters:**
- `func_name` - function to test
- `repeat` - number of repetitions

**Returns:**
- Execution time in milliseconds

**Example:**
```c
int time_ms = benchmark_test_time(my_function, 1000);
benchmark_mgs_time("My function performance", time_ms);
```

#### `benchmark_test_rate()`
```c
float benchmark_test_rate(void (*func_name)(void), float sec);
```

**Parameters:**
- `func_name` - function to test
- `sec` - minimum execution time in seconds

**Returns:**
- Calls per second (rate)

**Example:**
```c
float rate = benchmark_test_rate(my_function, 2.0);
benchmark_mgs_rate("My function throughput", rate);
```

### Result output functions

#### `benchmark_mgs_time()`
```c
void benchmark_mgs_time(const char *text, int dt);
```

**Output:**
- "Operation completed in 150 msec."
- "Operation completed in 2.45 sec."

#### `benchmark_mgs_rate()`
```c
void benchmark_mgs_rate(const char *test_name, float rate);
```

**Output:**
- "My function throughput: 1500 times/sec."
- "Data processing: 45.67 times/sec."

## Test data generator

### `generate_random_byte_array()`
```c
void generate_random_byte_array(uint8_t* array, const size_t size);
```

**Parameters:**
- `array` - pointer to array to fill
- `size` - array size in bytes

**Purpose:**
- Generate random data for testing
- Initialize arrays with random values

**Example:**
```c
#define TEST_DATA_SIZE 1024
uint8_t test_data[TEST_DATA_SIZE];
generate_random_byte_array(test_data, TEST_DATA_SIZE);
```

## Color scheme

```c
#define TEXT_COLOR_RED   "\x1B[31m"  // Red - errors/failures
#define TEXT_COLOR_GRN   "\x1B[32m"  // Green - success
#define TEXT_COLOR_YEL   "\x1B[33m"  // Yellow - warnings
#define TEXT_COLOR_BLU   "\x1B[34m"  // Blue - info
#define TEXT_COLOR_MAG   "\x1B[35m"  // Magenta - headers
#define TEXT_COLOR_CYN   "\x1B[36m"  // Cyan - modules
#define TEXT_COLOR_WHT   "\x1B[37m"  // White - debug
#define TEXT_COLOR_RESET "\x1B[0m"   // Reset
```

## Time functions

### `get_cur_time_msec()`
```c
int get_cur_time_msec(void);
```

**Returns:**
- Current time in milliseconds

### `get_cur_time_nsec()`
```c
uint64_t get_cur_time_nsec(void);
```

**Returns:**
- Current time in nanoseconds

**Purpose:**
- Accurate execution time measurement
- Test synchronization
- Unique identifiers

## Test function structure

### Basic test template

```c
void test_my_function() {
    // Prepare data
    int input = 42;
    int expected = 84;

    // Execute function under test
    int result = my_function(input);

    // Verify result
    dap_assert(result == expected, "My function basic test");
}

void test_my_function_edge_cases() {
    // Testing edge cases
    dap_assert(my_function(0) == 0, "Zero input");
    dap_assert(my_function(INT_MAX) == INT_MAX * 2, "Max int overflow");
    dap_assert(my_function(INT_MIN) == INT_MIN * 2, "Min int overflow");
}

void test_my_function_performance() {
    // Performance benchmarking
    int time_ms = benchmark_test_time([]() {
        volatile int result = my_function(1000);
        (void)result; // Suppress warning
    }, 1000);

    benchmark_mgs_time("My function performance", time_ms);
    dap_assert(time_ms < 100, "Performance test");
}
```

### Module test

```c
void run_my_module_tests() {
    dap_print_module_name("My Module Tests");

    test_my_function();
    test_my_function_edge_cases();
    test_my_function_performance();

    // Additional tests
    test_my_function_memory_leaks();
    test_my_function_concurrency();
}
```

## CI/CD integration

### Automated test run

```bash
# In Makefile or CMakeLists.txt
test:
    ./run_tests
    @echo "Tests completed with code: $$?"

# In CI script
run_tests:
    @echo "Running DAP SDK tests..."
    @make test
    @if [ $$? -ne 0 ]; then \
        echo "Tests failed!"; \
        exit 1; \
    fi
```

### Parsing results

```python
# parse_test_results.py
import re

def parse_test_output(output):
    results = {
        'passed': 0,
        'failed': 0,
        'modules': []
    }

    for line in output.split('\n'):
        if 'PASS.' in line:
            results['passed'] += 1
        elif 'FAILED!' in line:
            results['failed'] += 1
        elif line.startswith('\x1B[36m'):  # Cyan - module
            module = re.search(r'\x1B\[36m(.+?)\x1B\[0m', line)
            if module:
                results['modules'].append(module.group(1))

    return results
```

## Testing best practices

### 1. Test structure

```c
// test_my_module.c
#include "dap_test.h"
#include "my_module.h"

// Main function tests
void test_basic_functionality() {
    // Test main functionality
}

// Edge case tests
void test_edge_cases() {
    // Test extreme values
}

// Performance tests
void test_performance() {
    // Measure performance
}

// Memory tests
void test_memory_usage() {
    // Check for memory leaks
}

// Main test entry
int main() {
    dap_print_module_name("My Module");

    test_basic_functionality();
    test_edge_cases();
    test_performance();
    test_memory_usage();

    return 0;
}
```

### 2. Using data generators

```c
void test_with_random_data() {
    const size_t TEST_SIZE = 1000;
    uint8_t test_data[TEST_SIZE];

    // Generate random data
    generate_random_byte_array(test_data, TEST_SIZE);

    // Test with various input data
    for (size_t i = 0; i < 100; i++) {
        size_t offset = rand() % (TEST_SIZE - 10);
        process_data(&test_data[offset], 10);
        dap_assert_PIF(validate_result(), "Random data processing");
    }
}
```

### 3. Benchmarking

```c
void comprehensive_benchmark() {
    printf("Running comprehensive benchmark...\n");

    // Time test
    int time_ms = benchmark_test_time(test_function, 10000);
    benchmark_mgs_time("10k iterations", time_ms);

    // Throughput test
    float rate = benchmark_test_rate(test_function, 5.0);
    benchmark_mgs_rate("Throughput test", rate);

    // Compare with baseline
    dap_assert(time_ms < BASELINE_TIME, "Performance regression check");
}
```

### 4. Error handling

```c
void test_error_conditions() {
    // Test correct error handling
    dap_assert(my_function(NULL) == ERROR_INVALID_PARAM,
               "NULL parameter handling");

    dap_assert(my_function("") == ERROR_EMPTY_STRING,
               "Empty string handling");

    // Test recovery after errors
    int result = my_function("valid_input");
    dap_assert(result == SUCCESS, "Recovery after error");
}
```

## Debugging and diagnostics

### Enabling verbose output

```c
// At the start of the test
dap_test_msg("Starting test with parameters: input=%d, expected=%d",
             input, expected);

// During execution
for (int i = 0; i < iterations; i++) {
    if (i % 100 == 0) {
        dap_test_msg("Progress: %d/%d iterations", i, iterations);
    }
    // ... test logic
}
```

### Generating reports

```c
void generate_test_report() {
    printf("\n=== Test Report ===\n");
    printf("Total tests run: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", failed_tests);
    printf("Success rate: %.2f%%\n",
           (float)passed_tests / total_tests * 100);

    if (failed_tests > 0) {
        printf("\nFailed tests:\n");
        for (int i = 0; i < failed_tests; i++) {
            printf("- %s\n", failed_test_names[i]);
        }
    }
}
```

## Integration with other modules

### DAP Common
- Use common data structures
- Work with memory and strings
- Logging results

### DAP Config
- Load test configurations
- Parameterize test scenarios
- Configure test conditions

### DAP Time
- Test synchronization
- Measure time intervals
- Create timeouts

## Common issues

### 1. Flaky tests

```c
// Problem: test depends on external factors
void unstable_test() {
    int result = network_call(); // May depend on network
    dap_assert(result == SUCCESS, "Network test"); // Flaky
}

// Solution: test isolation
void stable_test() {
    // Mock or stub for network_call
    mock_network_response(SUCCESS);
    int result = network_call();
    dap_assert(result == SUCCESS, "Network test");
}
```

### 2. Memory leaks

```c
// Problem: memory is not freed
void memory_leak_test() {
    for (int i = 0; i < 1000; i++) {
        char *data = malloc(1024);
        process_data(data);
        // Forgot free(data)!
    }
}

// Solution: proper memory management
void fixed_memory_test() {
    for (int i = 0; i < 1000; i++) {
        char *data = malloc(1024);
        process_data(data);
        free(data); // Free memory
    }
}
```

### 3. Data races

```c
// Problem: concurrent access to shared resources
static int shared_counter = 0;

void concurrent_test() {
    shared_counter++;
    dap_assert(shared_counter == 1, "Counter test"); // Race!
}

// Solution: synchronization
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void safe_concurrent_test() {
    pthread_mutex_lock(&mutex);
    shared_counter++;
    dap_assert(shared_counter == 1, "Counter test");
    pthread_mutex_unlock(&mutex);
}
```

## Conclusion

The `dap_test_framework` module provides a complete toolkit for testing the DAP SDK. Its simplicity combined with powerful benchmarking and data‑generation capabilities makes it ideal for ensuring the quality and performance of distributed applications.
