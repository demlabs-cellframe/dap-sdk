# dap_config.h/c - DAP SDK Configuration System

## Overview

The `dap_config` module provides a powerful and flexible system for working with configuration files in the DAP SDK. It supports various data types, configuration sections, arrays, and ensures safe handling of application settings.

## Key features

- **Multi‑level structure**: Sections and parameters
- **Data types**: boolean, integer, string, arrays, double
- **Global and local configs**: Multiple configuration files
- **Type safety**: Strict typing with checks
- **Extensibility**: Easy to add new data types
- **Cross‑platform**: Windows, Linux, macOS
- **File paths**: Automatic resolution of relative paths

## Data structures

### Configuration item types

```c
typedef enum {
    DAP_CONFIG_ITEM_UNKNOWN = '\0',  // Unknown type
    DAP_CONFIG_ITEM_ARRAY   = 'a',    // String array
    DAP_CONFIG_ITEM_BOOL    = 'b',    // Boolean value
    DAP_CONFIG_ITEM_DECIMAL = 'd',    // Integer or floating‑point
    DAP_CONFIG_ITEM_STRING  = 's'     // String
} dap_config_item_type_t;
```

### Configuration structure

```c
typedef struct dap_config_item {
    char type;              // Item type ('a', 'b', 'd', 's')
    char *name;             // Parameter name
    union {
        bool val_bool;      // Boolean value
        char *val_str;      // String value
        char **val_arr;     // Array of strings
        int64_t val_int;    // Integer value
    } val;
    UT_hash_handle hh;      // Hash handle for quick lookup
} dap_config_item_t;

typedef struct dap_conf {
    char *path;                    // Path to configuration file
    dap_config_item_t *items;      // Hash table of items
    UT_hash_handle hh;             // For global configuration table
} dap_config_t;
```

## Global variables

### Main configuration
```c
extern dap_config_t *g_config;  // Global application configuration
```

## API Reference

### Initialization and deinitialization

#### dap_config_init()

```c
int dap_config_init(const char *a_configs_path);
```

**Description**: Initializes the configuration system with the given configuration directory path.

**Parameters:**
- `a_configs_path` - path to the configuration files directory

**Returns:**
- `0` - initialized successfully
- `-1` - empty path
- `-2` - invalid path

**Example:**
```c
#include "dap_config.h"

int main(int argc, char *argv[]) {
    // Initialize with configuration path
    if (dap_config_init("./configs") != 0) {
        fprintf(stderr, "Failed to initialize config system\n");
        return 1;
    }

    // Work with configuration...
    // ...

    return 0;
}
```

#### dap_config_deinit()

```c
void dap_config_deinit(void);
```

**Description**: Deinitializes the configuration system and frees all resources.

**Example:**
```c
// Proper shutdown
dap_config_deinit();
```

### Working with configuration files

#### dap_config_open()

```c
dap_config_t *dap_config_open(const char *a_config_filename);
```

**Description**: Opens and loads a configuration file.

**Parameters:**
- `a_config_filename` - configuration filename (relative to configs_path)

**Returns:** Pointer to configuration structure or NULL on error.

**Example:**
```c
// Open configuration file
dap_config_t *config = dap_config_open("application.conf");
if (!config) {
    fprintf(stderr, "Failed to open config file\n");
    return 1;
}

// Work with configuration...

// Close configuration
dap_config_close(config);
```

#### dap_config_close()

```c
void dap_config_close(dap_config_t *a_config);
```

**Description**: Closes a configuration and frees resources.

**Parameters:**
- `a_config` - configuration pointer

**Example:**
```c
dap_config_close(config);
config = NULL;
```

### Getting the configs path

#### dap_config_path()

```c
const char *dap_config_path(void);
```

**Description**: Returns the current path to the configuration directory.

**Returns:** Path string or NULL.

**Example:**
```c
const char *config_path = dap_config_path();
if (config_path) {
    printf("Config path: %s\n", config_path);
}
```

### Getting an item's type

#### dap_config_get_item_type()

```c
dap_config_item_type_t dap_config_get_item_type(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name
);
```

**Description**: Returns the type of the specified configuration item.

**Parameters:**
- `a_config` - configuration
- `a_section` - configuration section
- `a_item_name` - item name

**Returns:** Item type or `DAP_CONFIG_ITEM_UNKNOWN`.

**Example:**
```c
dap_config_item_type_t type = dap_config_get_item_type(config, "database", "port");
switch (type) {
    case DAP_CONFIG_ITEM_STRING:
        printf("Port is a string\n");
        break;
    case DAP_CONFIG_ITEM_DECIMAL:
        printf("Port is a number\n");
        break;
    default:
        printf("Port type is unknown\n");
}
```

## Getting item values

### Booleans

#### dap_config_get_item_bool_default()

```c
bool dap_config_get_item_bool_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    bool a_default
);
```

**Description**: Gets a boolean configuration value with a default.

**Parameters:**
- `a_config` - configuration
- `a_section` - section
- `a_item_name` - item name
- `a_default` - default value

**Returns:** Item value or default value.

**Example:**
```c
bool debug_mode = dap_config_get_item_bool_default(
    config, "application", "debug", false
);

if (debug_mode) {
    printf("Debug mode is enabled\n");
}
```

#### Short form

```c
#define dap_config_get_item_bool(a_conf, a_path, a_item) \
    dap_config_get_item_bool_default(a_conf, a_path, a_item, false)
```

### Integers

#### Various sizes

```c
// 16-bit numbers
#define dap_config_get_item_uint16(a_conf, a_path, a_item) \
    (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint16_default(a_conf, a_path, a_item, a_default) \
    (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// 32-bit numbers
#define dap_config_get_item_uint32(a_conf, a_path, a_item) \
    (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint32_default(a_conf, a_path, a_item, a_default) \
    (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// 64-bit numbers
#define dap_config_get_item_uint64(a_conf, a_path, a_item) \
    (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)

#define dap_config_get_item_uint64_default(a_conf, a_path, a_item, a_default) \
    (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

// Signed numbers
#define dap_config_get_item_int16(a_conf, a_path, a_item) \
    (int16_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)

#define dap_config_get_item_int32(a_conf, a_path, a_item) \
    (int32_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)

#define dap_config_get_item_int64(a_conf, a_path, a_item) \
    (int64_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)
```

**Example:**
```c
// Get port number
uint16_t port = dap_config_get_item_uint16_default(
    config, "server", "port", 8080
);

// Get max size
uint64_t max_size = dap_config_get_item_uint64_default(
    config, "limits", "max_file_size", 1048576  // 1MB default
);

// Get timeout with negative default
int32_t timeout = dap_config_get_item_int32_default(
    config, "network", "timeout", -1
);
```

### Strings

#### dap_config_get_item_str_default()

```c
const char *dap_config_get_item_str_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    const char *a_default
);
```

**Description**: Gets a string configuration value.

**Parameters:**
- `a_config` - configuration
- `a_section` - section
- `a_item_name` - item name
- `a_default` - default value

**Returns:** Pointer to string or default value.

**Example:**
```c
const char *db_host = dap_config_get_item_str_default(
    config, "database", "host", "localhost"
);

const char *log_level = dap_config_get_item_str_default(
    config, "logging", "level", "INFO"
);
```

#### Short form

```c
#define dap_config_get_item_str(a_conf, a_path, a_item) \
    dap_config_get_item_str_default(a_conf, a_path, a_item, NULL)
```

### File paths

#### dap_config_get_item_str_path_default()

```c
char *dap_config_get_item_str_path_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    const char *a_default
);
```

**Description**: Gets a file path, automatically resolving relative paths against the configuration directory.

**Returns:** Full file path (must be freed with `free()`).

**Example:**
```c
char *log_file = dap_config_get_item_str_path_default(
    config, "logging", "file", "logs/app.log"
);

if (log_file) {
    printf("Log file path: %s\n", log_file);
    // Use the path...
    free(log_file);
}
```

#### Short forms

```c
#define dap_config_get_item_path(a_conf, a_path, a_item) \
    dap_config_get_item_str_path_default(a_conf, a_path, a_item, NULL)

#define dap_config_get_item_path_default(a_conf, a_path, a_item, a_default) \
    dap_config_get_item_str_path_default(a_conf, a_path, a_item, a_default)
```

### Doubles

#### dap_config_get_item_double_default()

```c
double dap_config_get_item_double_default(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    double a_default
);
```

**Description**: Gets a floating‑point configuration value.

**Example:**
```c
double threshold = dap_config_get_item_double_default(
    config, "processing", "threshold", 0.75
);
```

#### Short form

```c
#define dap_config_get_item_double(a_conf, a_path, a_item) \
    dap_config_get_item_double_default(a_conf, a_path, a_item, 0)
```

### String arrays

#### dap_config_get_array_str()

```c
const char** dap_config_get_array_str(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    uint16_t *a_array_length
);
```

**Description**: Gets an array of strings from configuration.

**Parameters:**
- `a_config` - configuration
- `a_section` - section
- `a_item_name` - item name
- `a_array_length` - pointer to store array length

**Returns:** Pointer to NULL‑terminated array of strings.

**Example:**
```c
uint16_t array_length;
const char **servers = dap_config_get_array_str(
    config, "network", "servers", &array_length
);

if (servers) {
    printf("Found %d servers:\n", array_length);
    for (uint16_t i = 0; i < array_length; i++) {
        printf("  %s\n", servers[i]);
    }
    // The 'servers' array is automatically freed by the system
}
```

#### dap_config_get_item_str_path_array()

```c
char **dap_config_get_item_str_path_array(
    dap_config_t *a_config,
    const char *a_section,
    const char *a_item_name,
    uint16_t *a_array_length
);
```

**Description**: Gets an array of file paths.

**Returns:** Array of full paths (must be freed with `dap_config_get_item_str_path_array_free()`).

**Example:**
```c
uint16_t paths_count;
char **config_files = dap_config_get_item_str_path_array(
    config, "includes", "files", &paths_count
);

if (config_files) {
    for (uint16_t i = 0; i < paths_count; i++) {
        printf("Config file: %s\n", config_files[i]);
    }

    // Free the array
    dap_config_get_item_str_path_array_free(config_files, paths_count);
}
```

#### Freeing the paths array

```c
void dap_config_get_item_str_path_array_free(
    char **a_paths_array,
    uint16_t a_array_length
);
```

## Usage examples

### Example 1: Basic configuration usage

```c
#include "dap_config.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
// Initialize the configuration system
    if (dap_config_init("./configs") != 0) {
        fprintf(stderr, "Failed to initialize config system\n");
        return 1;
    }

// Open main configuration
    dap_config_t *config = dap_config_open("application.conf");
    if (!config) {
        fprintf(stderr, "Failed to open config file\n");
        return 1;
    }

// Get base parameters
    const char *app_name = dap_config_get_item_str_default(
        config, "application", "name", "MyApp"
    );

    uint16_t port = dap_config_get_item_uint16_default(
        config, "server", "port", 8080
    );

    bool debug = dap_config_get_item_bool_default(
        config, "application", "debug", false
    );

    printf("Application: %s\n", app_name);
    printf("Port: %d\n", port);
    printf("Debug mode: %s\n", debug ? "enabled" : "disabled");

// Work with paths
    char *log_file = dap_config_get_item_path_default(
        config, "logging", "file", "logs/app.log"
    );

    if (log_file) {
        printf("Log file: %s\n", log_file);
        free(log_file);
    }

// Close configuration
    dap_config_close(config);

// Deinitialize
    dap_config_deinit();

    return 0;
}
```

### Example 2: Working with arrays

```c
#include "dap_config.h"

void process_server_list(dap_config_t *config) {
    uint16_t server_count;
    const char **servers = dap_config_get_array_str(
        config, "network", "servers", &server_count
    );

    if (servers && server_count > 0) {
        printf("Connecting to %d servers:\n", server_count);

        for (uint16_t i = 0; i < server_count; i++) {
            printf("  Server %d: %s\n", i + 1, servers[i]);
            // Connect to the server...
        }
    } else {
        printf("No servers configured, using default\n");
    }
}

void load_config_files(dap_config_t *config) {
    uint16_t file_count;
    char **config_files = dap_config_get_item_str_path_array(
        config, "includes", "files", &file_count
    );

    if (config_files && file_count > 0) {
        printf("Loading %d additional config files:\n", file_count);

        for (uint16_t i = 0; i < file_count; i++) {
            printf("  Loading: %s\n", config_files[i]);
            // Load additional configuration...
        }

        // Free resources
        dap_config_get_item_str_path_array_free(config_files, file_count);
    }
}
```

### Example 3: Complex server configuration

```c
#include "dap_config.h"
#include <stdlib.h>

typedef struct {
    const char *host;
    uint16_t port;
    const char *database;
    const char *user;
    const char *password;
    bool ssl_enabled;
    uint32_t max_connections;
    double timeout;
} db_config_t;

db_config_t *load_database_config(dap_config_t *config) {
    db_config_t *db_config = calloc(1, sizeof(db_config_t));

    if (!db_config) return NULL;

    // Load database parameters
    db_config->host = dap_config_get_item_str_default(
        config, "database", "host", "localhost"
    );

    db_config->port = dap_config_get_item_uint16_default(
        config, "database", "port", 5432
    );

    db_config->database = dap_config_get_item_str_default(
        config, "database", "name", "myapp"
    );

    db_config->user = dap_config_get_item_str_default(
        config, "database", "user", "app_user"
    );

    db_config->password = dap_config_get_item_str_default(
        config, "database", "password", ""
    );

    db_config->ssl_enabled = dap_config_get_item_bool_default(
        config, "database", "ssl", false
    );

    db_config->max_connections = dap_config_get_item_uint32_default(
        config, "database", "max_connections", 10
    );

    db_config->timeout = dap_config_get_item_double_default(
        config, "database", "timeout", 30.0
    );

    return db_config;
}

void print_database_config(const db_config_t *config) {
    printf("Database Configuration:\n");
    printf("  Host: %s:%d\n", config->host, config->port);
    printf("  Database: %s\n", config->database);
    printf("  User: %s\n", config->user);
    printf("  SSL: %s\n", config->ssl_enabled ? "enabled" : "disabled");
    printf("  Max connections: %u\n", config->max_connections);
    printf("  Timeout: %.1f seconds\n", config->timeout);
}

int main() {
    if (dap_config_init("./configs") != 0) {
        return 1;
    }

    dap_config_t *config = dap_config_open("database.conf");
    if (!config) {
        return 1;
    }

    db_config_t *db_config = load_database_config(config);
    if (db_config) {
        print_database_config(db_config);
        free(db_config);
    }

    dap_config_close(config);
    dap_config_deinit();

    return 0;
}
```

## Configuration file format

### INI structure

```ini
[application]
name = MyApplication
version = 1.0.0
debug = true

[server]
host = 0.0.0.0
port = 8080
max_connections = 100

[database]
host = localhost
port = 5432
name = myapp_db
user = app_user
password = secret123
ssl = true
max_connections = 20
timeout = 30.5

[logging]
level = DEBUG
file = logs/application.log

[network]
servers = server1.example.com, server2.example.com, server3.example.com

[includes]
files = common.conf, development.conf
```

### Supported value types

1. **Strings**: Any characters, quoted or not
2. **Numbers**: Integers and floating‑point
3. **Booleans**: `true`, `false`, `1`, `0`, `yes`, `no`
4. **Arrays**: Comma‑separated values
5. **Paths**: Relative paths resolved automatically

## Performance

### Optimizations

1. **Hash tables**: Fast O(1) lookups
2. **Lazy loading**: Load parameters on demand
3. **Caching**: Parsed values are cached
4. **Minimal allocations**: Structure reuse

### Performance benchmarks

| Operation | Throughput | Note |
|-----------|-----------|------|
| `dap_config_open()` | ~5-10 ms | Load a typical file |
| `dap_config_get_item_str()` | ~1-2 μs | Get string parameter |
| `dap_config_get_item_uint32()` | ~1-2 μs | Get numeric parameter |
| Array search | ~5-10 μs | Linear search |

### Factors

- **File size**: Larger files load slower
- **Number of items**: Impacts memory usage
- **Access frequency**: Cached values are faster
- **Data types**: Numeric values are faster than strings

## Security

### Protection against common issues

```c
// ✅ Correct error handling
const char *get_config_string_safe(dap_config_t *config,
                                   const char *section,
                                   const char *key,
                                   const char *default_val) {
    if (!config || !section || !key) {
        return default_val;
    }

    const char *value = dap_config_get_item_str_default(
        config, section, key, default_val
    );

    // NULL check for string values
    return value ? value : default_val;
}

// ❌ Vulnerable code
const char *get_config_string_unsafe(dap_config_t *config,
                                     const char *section,
                                     const char *key) {
    // Missing input validation
    // Does not handle NULL values
    return dap_config_get_item_str(config, section, key);
}
```

### Security recommendations

1. **Input validation**: Always validate parameters
2. **Error handling**: Check return values of all functions
3. **Memory management**: Free memory for paths and arrays
4. **Value limits**: Validate numeric ranges
5. **Path sanitization**: Avoid dangerous characters in file paths

### Handling sensitive data

```c
// For passwords and secrets, prefer environment variables
const char *get_password_secure(dap_config_t *config) {
    // Check environment variable first
    const char *env_password = getenv("APP_PASSWORD");
    if (env_password && *env_password) {
        return env_password;
    }

    // Fallback to configuration file
    return dap_config_get_item_str_default(
        config, "security", "password", ""
    );
}
```

## Best practices

### 1. Organizing configuration files

```ini
# Split configuration into logical sections
[application]
name = MyApp
version = 1.0.0

[database]
host = localhost
port = 5432

[logging]
level = INFO
file = logs/app.log

[security]
ssl_enabled = true
cert_file = certs/server.crt
```

### 2. Using default values

```c
// Always provide reasonable default values
uint16_t port = dap_config_get_item_uint16_default(
    config, "server", "port", 8080
);

const char *host = dap_config_get_item_str_default(
    config, "server", "host", "0.0.0.0"
);

bool debug = dap_config_get_item_bool_default(
    config, "application", "debug", false
);
```

### 3. Working with paths

```c
// Use automatic path resolution
char *config_file = dap_config_get_item_path(
    config, "includes", "main_config"
);

char *log_dir = dap_config_get_item_path_default(
    config, "logging", "directory", "logs"
);

if (config_file) {
    printf("Config file: %s\n", config_file);
    free(config_file);
}

if (log_dir) {
    printf("Log directory: %s\n", log_dir);
    free(log_dir);
}
```

### 4. Handling arrays

```c
// Correct handling of arrays
uint16_t server_count;
const char **servers = dap_config_get_array_str(
    config, "network", "servers", &server_count
);

if (servers && server_count > 0) {
    for (uint16_t i = 0; i < server_count; i++) {
        printf("Server %d: %s\n", i + 1, servers[i]);
    }
    // The 'servers' array is freed automatically
}

// For arrays of paths
uint16_t file_count;
char **files = dap_config_get_item_str_path_array(
    config, "includes", "files", &file_count
);

if (files) {
    // Work with files...
    dap_config_get_item_str_path_array_free(files, file_count);
}
```

### 5. Structuring code

```c
// Create dedicated functions for loading configuration
typedef struct {
    uint16_t port;
    const char *host;
    bool ssl_enabled;
    const char **allowed_ips;
    uint16_t ip_count;
} server_config_t;

server_config_t *load_server_config(dap_config_t *config) {
    server_config_t *srv_config = calloc(1, sizeof(server_config_t));

    srv_config->port = dap_config_get_item_uint16_default(
        config, "server", "port", 8080
    );

    srv_config->host = dap_config_get_item_str_default(
        config, "server", "host", "0.0.0.0"
    );

    srv_config->ssl_enabled = dap_config_get_item_bool_default(
        config, "server", "ssl", false
    );

    srv_config->allowed_ips = dap_config_get_array_str(
        config, "security", "allowed_ips", &srv_config->ip_count
    );

    return srv_config;
}
```

## Extending the system

### Adding new data types

```c
// Example of adding support for custom types
typedef enum {
    CONFIG_TYPE_CUSTOM = 'c'
} custom_config_types_t;

// Custom type parser
bool parse_custom_type(const char *str, custom_type_t *result) {
    // Parsing logic...
    return true;
}

// Usage in code
custom_type_t custom_value;
const char *str_value = dap_config_get_item_str(
    config, "custom", "value"
);

if (str_value && parse_custom_type(str_value, &custom_value)) {
    // Use custom_value
}
```

## Conclusion

The `dap_config` module provides a powerful and flexible configuration system for the DAP SDK:

- **Wide data type support**: boolean, integer, string, arrays, paths
- **Multi‑level structure**: Sections and parameters for organization
- **Safety and reliability**: Strict typing and error checks
- **Performance**: Optimized data structures and caching
- **Cross‑platform**: All supported platforms
- **Extensibility**: Easy to add new types and functions

The DAP SDK configuration system is a foundational component that ensures flexible and reliable management of application settings.

For more information see:
- `dap_config.h` - full configuration system API
- Examples in `examples/config/`
- Configuration file format docs
