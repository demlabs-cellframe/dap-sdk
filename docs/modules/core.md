# Core Module - DAP SDK Core

## Overview

The Core Module is the foundation of DAP SDK and provides base functionality, common utilities, and platform-specific implementations.

## Module structure

```
core/
├── include/           # Header files (19 files)
│   ├── dap_binary_tree.h    # Binary search trees
│   ├── dap_cbuf.h          # Circular buffers
│   ├── dap_common.h        # Common definitions
│   ├── dap_config.h        # Configuration system
│   ├── dap_crc64.h         # CRC64 hashing
│   ├── dap_file_utils.h    # File utilities
│   ├── dap_fnmatch.h       # Filename globbing
│   ├── dap_json_rpc_errors.h # JSON-RPC errors
│   ├── dap_list.h          # Linked lists
│   ├── dap_math_convert.h  # Numeric conversions
│   ├── dap_math_ops.h      # Math operations
│   ├── dap_module.h        # Modules system
│   ├── dap_strfuncs.h      # String functions
│   ├── dap_string.h        # String helpers
│   ├── dap_time.h          # Time utilities
│   ├── dap_tsd.h           # Thread-Specific Data
│   └── portable_endian.h   # Portable endian
├── src/               # Sources
│   ├── common/        # Common utilities
│   ├── unix/          # Unix/Linux implementation
│   ├── darwin/        # macOS implementation
│   └── win32/         # Windows implementation
├── test/              # Tests
└── docs/              # Documentation
```

## Main components

### 1. Common utilities

#### dap_common.h
Core definitions and macros for the entire SDK.

```c
// Fundamental data types
typedef uint8_t dap_byte_t;
typedef uint32_t dap_uint_t;
typedef int32_t dap_int_t;

// Debug macros
#define DAP_ASSERT(condition) \
    do { if (!(condition)) { \
        dap_log(L_ERROR, "Assertion failed: %s", #condition); \
        abort(); \
    } } while(0)

// Memory management macros
#define DAP_NEW(type) ((type*)dap_malloc(sizeof(type)))
#define DAP_DELETE(ptr) do { dap_free(ptr); ptr = NULL; } while(0)
```

#### dap_list.h
Linked list implementation supporting various data types.

```c
// Create list
dap_list_t* dap_list_new(void);

// Append element
dap_list_t* dap_list_append(dap_list_t* list, void* data);

// Remove element
dap_list_t* dap_list_remove(dap_list_t* list, void* data);

// Find element
dap_list_t* dap_list_find(dap_list_t* list, void* data);

// Free list
void dap_list_free(dap_list_t* list);
```

#### dap_hash.h
**⚠️ Note: This header is not in the Core Module**

Hash functions are likely moved to the Crypto Module (`dap-sdk/crypto/`). Use crypto module functions for all hashing operations:
- `dap-sdk/crypto/include/dap_hash.h` - core hash functions
- `dap-sdk/crypto/include/dap_enc_key.h` - crypto keys and signatures

#### dap_time.h
Time handling and timestamps.

```c
// Get current time
dap_time_t dap_time_now(void);

// Time conversion
char* dap_time_to_string(dap_time_t time);
dap_time_t dap_time_from_string(const char* str);

// Time comparison
int dap_time_compare(dap_time_t t1, dap_time_t t2);

// Sleep
void dap_time_sleep(uint32_t milliseconds);
```

### 2. Platform-specific implementations

#### Unix/Linux (core/src/unix/)
```c
// Process management
pid_t dap_process_fork(void);
int dap_process_wait(pid_t pid);

// Network sockets
int dap_socket_create(int domain, int type, int protocol);
int dap_socket_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);

// File system
int dap_file_open(const char* path, int flags, mode_t mode);
ssize_t dap_file_read(int fd, void* buf, size_t count);
```

#### macOS (core/src/darwin/)
```c
// macOS-specific functions
int dap_darwin_get_system_info(dap_system_info_t* info);
int dap_darwin_set_process_name(const char* name);
```

#### Windows (core/src/win32/)
```c
// Windows-specific functions
HANDLE dap_win32_create_thread(LPTHREAD_START_ROUTINE func, LPVOID param);
DWORD dap_win32_wait_for_thread(HANDLE thread);

// Unicode support
wchar_t* dap_win32_utf8_to_wide(const char* utf8);
char* dap_win32_wide_to_utf8(const wchar_t* wide);
```

### 3. Configuration system (dap_config.h)

**Purpose**: Manage configuration files and application settings.

```c
// Configuration structure
typedef struct dap_config {
    char *section;              // Configuration section
    char *key;                  // Key
    char *value;                // Value
    struct dap_config *next;    // Next item
} dap_config_t;

// Load configuration
dap_config_t *dap_config_load(const char *filename);

// Get values
const char *dap_config_get_string(dap_config_t *config,
                                  const char *section,
                                  const char *key);

int dap_config_get_int(dap_config_t *config,
                       const char *section,
                       const char *key);

bool dap_config_get_bool(dap_config_t *config,
                         const char *section,
                         const char *key);

// Save configuration
int dap_config_save(dap_config_t *config, const char *filename);

// Free memory
void dap_config_free(dap_config_t *config);
```

### 4. Module system (dap_module.h)

**Purpose**: Dynamic loading and management of modules.

```c
// Module structure
typedef struct dap_module {
    char *name;                 // Module name
    void *handle;               // Module handle
    int (*init)(void);          // Init function
    void (*deinit)(void);       // Deinit function
    struct dap_module *next;    // Next module
} dap_module_t;

// Load module
dap_module_t *dap_module_load(const char *path);

// Unload module
int dap_module_unload(dap_module_t *module);

// Find module
dap_module_t *dap_module_find(const char *name);

// Call module function
void *dap_module_call(dap_module_t *module, const char *func_name);
```

### 5. File utilities (dap_file_utils.h)

**Purpose**: Utilities for interacting with the filesystem.

```c
// File info
typedef struct dap_file_info {
    char *name;                 // File name
    size_t size;                // File size
    time_t mtime;               // Modification time
    bool is_dir;                // Is a directory
} dap_file_info_t;

// Check if file exists
bool dap_file_exists(const char *filename);

// Get file size
size_t dap_file_size(const char *filename);

// Read file
char *dap_file_read(const char *filename, size_t *size);

// Write file
int dap_file_write(const char *filename, const void *data, size_t size);

// Create directory
int dap_dir_create(const char *path);

// List files
dap_list_t *dap_dir_list(const char *path);

// Copy file
int dap_file_copy(const char *src, const char *dst);

// Move file
int dap_file_move(const char *src, const char *dst);
```

### 6. Math operations (dap_math_ops.h)

**Purpose**: Safe math operations with overflow checks.

```c
// Safe addition
bool dap_add_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_add_i64(int64_t *result, int64_t a, int64_t b);

// Safe subtraction
bool dap_sub_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_sub_i64(int64_t *result, int64_t a, int64_t b);

// Safe multiplication
bool dap_mul_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_mul_i64(int64_t *result, int64_t a, int64_t b);

// Safe division
bool dap_div_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_div_i64(int64_t *result, int64_t a, int64_t b);

// Overflow checks
bool dap_will_add_overflow(uint64_t a, uint64_t b);
bool dap_will_mul_overflow(uint64_t a, uint64_t b);
```

### 7. Numeric conversions (dap_math_convert.h)

**Purpose**: Convert numbers between formats.

```c
// String to number
bool dap_str_to_u64(const char *str, uint64_t *result);
bool dap_str_to_i64(const char *str, int64_t *result);

// Number to string
char *dap_u64_to_str(uint64_t value);
char *dap_i64_to_str(int64_t value);

// Endian conversions
uint16_t dap_swap_u16(uint16_t value);
uint32_t dap_swap_u32(uint32_t value);
uint64_t dap_swap_u64(uint64_t value);

// BCD conversions
uint8_t dap_to_bcd(uint8_t value);
uint8_t dap_from_bcd(uint8_t value);
```

### 8. Binary search trees (dap_binary_tree.h)

**Purpose**: Implementation of BSTs for efficient data storage.

```c
// Tree node structure
typedef struct dap_binary_tree_node {
    void *key;                          // Key
    void *value;                        // Value
    struct dap_binary_tree_node *left;  // Left subtree
    struct dap_binary_tree_node *right; // Right subtree
} dap_binary_tree_node_t;

// Tree structure
typedef struct dap_binary_tree {
    dap_binary_tree_node_t *root;       // Root
    size_t size;                        // Number of elements
    int (*compare)(const void*, const void*); // Compare function
} dap_binary_tree_t;

// Create tree
dap_binary_tree_t *dap_binary_tree_new(int (*compare)(const void*, const void*));

// Insert element
bool dap_binary_tree_insert(dap_binary_tree_t *tree, void *key, void *value);

// Find element
void *dap_binary_tree_find(dap_binary_tree_t *tree, const void *key);

// Remove element
bool dap_binary_tree_remove(dap_binary_tree_t *tree, const void *key);

// Free tree
void dap_binary_tree_free(dap_binary_tree_t *tree);
```

### 9. Circular buffers (dap_cbuf.h)

**Purpose**: Circular buffer implementation for efficient data streaming.

```c
// Circular buffer structure
typedef struct dap_cbuf {
    uint8_t *buffer;     // Data buffer
    size_t size;         // Buffer size
    size_t head;         // Head index
    size_t tail;         // Tail index
    bool full;           // Full flag
} dap_cbuf_t;

// Create buffer
dap_cbuf_t *dap_cbuf_new(size_t size);

// Write data
size_t dap_cbuf_write(dap_cbuf_t *cbuf, const void *data, size_t len);

// Read data
size_t dap_cbuf_read(dap_cbuf_t *cbuf, void *data, size_t len);

// State checks
bool dap_cbuf_is_empty(dap_cbuf_t *cbuf);
bool dap_cbuf_is_full(dap_cbuf_t *cbuf);
size_t dap_cbuf_used_space(dap_cbuf_t *cbuf);
size_t dap_cbuf_free_space(dap_cbuf_t *cbuf);

// Free buffer
void dap_cbuf_free(dap_cbuf_t *cbuf);
```

## API Reference

### Initialization and cleanup

```c
// Initialize core module
int dap_core_init(void);

// Deinitialize core module
void dap_core_deinit(void);

// Check initialization
bool dap_core_is_initialized(void);
```

### Memory management

```c
// Allocation
void* dap_malloc(size_t size);
void* dap_calloc(size_t count, size_t size);
void* dap_realloc(void* ptr, size_t size);

// Free
void dap_free(void* ptr);

// Safe free
void dap_safe_free(void** ptr);
```

### Utilities

```c
// String compare
int dap_strcmp(const char* s1, const char* s2);
int dap_strncmp(const char* s1, const char* s2, size_t n);

// String copy
char* dap_strdup(const char* str);
char* dap_strndup(const char* str, size_t n);

// String formatting
int dap_snprintf(char* str, size_t size, const char* format, ...);
int dap_vsnprintf(char* str, size_t size, const char* format, va_list ap);
```

## Usage examples

### Basic usage

```c
#include "dap_common.h"
#include "dap_list.h"
#include "dap_hash.h"

int main() {
    // Initialization
    dap_core_init();
    
    // Create list
    dap_list_t* list = dap_list_new();
    
    // Append elements
    char* item1 = dap_strdup("Hello");
    char* item2 = dap_strdup("World");
    
    list = dap_list_append(list, item1);
    list = dap_list_append(list, item2);
    
    // Hash data
    uint8_t hash[32];
    dap_hash_sha256("Hello World", 11, hash);
    
    // Cleanup
    dap_list_free(list);
    dap_core_deinit();
    
    return 0;
}
```

### Working with time

```c
#include "dap_time.h"

void time_example() {
    // Get current time
    dap_time_t now = dap_time_now();
    
    // Convert to string
    char* time_str = dap_time_to_string(now);
    printf("Current time: %s\n", time_str);
    
    // Delay
    dap_time_sleep(1000); // 1 second
    
    // Free memory
    dap_free(time_str);
}
```

### Platform-specific code

```c
#include "dap_common.h"

#ifdef DAP_PLATFORM_UNIX
    #include "dap_unix.h"
#elif defined(DAP_PLATFORM_WINDOWS)
    #include "dap_win32.h"
#endif

void platform_example() {
#ifdef DAP_PLATFORM_UNIX
    // Unix-specific code
    pid_t pid = dap_process_fork();
    if (pid == 0) {
        // Child process
        printf("Child process\n");
    } else {
        // Parent process
        dap_process_wait(pid);
    }
#elif defined(DAP_PLATFORM_WINDOWS)
    // Windows-specific code
    HANDLE thread = dap_win32_create_thread(thread_func, NULL);
    dap_win32_wait_for_thread(thread);
#endif
}
```

## Testing

### Running tests

```bash
# Build with tests
cmake -DBUILD_DAP_SDK_TESTS=ON ..
make

# Run core module tests
./test/core/test_common
./test/core/test_list
./test/core/test_hash
./test/core/test_time
```

### Test example

```c
#include "dap_test.h"
#include "dap_list.h"

void test_list_operations() {
    dap_list_t* list = dap_list_new();
    
    // Append test
    list = dap_list_append(list, "item1");
    DAP_ASSERT(list != NULL);
    DAP_ASSERT(dap_list_length(list) == 1);
    
    // Find test
    dap_list_t* found = dap_list_find(list, "item1");
    DAP_ASSERT(found != NULL);
    
    // Remove test
    list = dap_list_remove(list, "item1");
    DAP_ASSERT(dap_list_length(list) == 0);
    
    dap_list_free(list);
}
```

## Performance

### Benchmarks

| Операция | Производительность |
|----------|-------------------|
| dap_malloc/free | ~10M ops/sec |
| dap_list_append | ~1M ops/sec |
| dap_hash_sha256 | ~100MB/sec |
| dap_time_now | ~10M ops/sec |

### Optimizations

- **Inlined functions**: Critical functions are inlined
- **Memory pools**: Reuse memory for frequent operations
- **SIMD optimizations**: Vector instructions for hashing
- **Lock-free structures**: Non-blocking algorithms for concurrency

## Debugging

### Logging

```c
#include "dap_log.h"

void debug_example() {
    // Various log levels
    dap_log(L_DEBUG, "Debug message: %d", 42);
    dap_log(L_INFO, "Info message");
    dap_log(L_WARNING, "Warning message");
    dap_log(L_ERROR, "Error message");
}
```

### Validation

```c
#include "dap_common.h"

void validation_example() {
    // Pointer checks
    void* ptr = dap_malloc(100);
    DAP_ASSERT(ptr != NULL);
    
    // Bounds checks
    DAP_ASSERT(size > 0 && size < MAX_SIZE);
    
    // String checks
    DAP_ASSERT(str != NULL && strlen(str) > 0);
}
```

## Conclusion

The Core Module provides a reliable foundation for all other DAP SDK modules. It ensures cross‑platform support, efficient resource management, and common utilities required for developing decentralized applications.
