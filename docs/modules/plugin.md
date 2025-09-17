# DAP Plugin Module (dap_plugin.h)

## Overview

The `dap_plugin` module provides an extensible plugin system for DAP SDK. It allows dynamically loading and unloading functional modules at runtime, enabling:

- **Dynamic loading** - plugins load without restarting the application
- **Plugin dependencies** - automatic dependency resolution
- **Typed plugins** - support for various plugin types
- **Hot swap** - update plugins without stopping the system
- **Isolation** - each plugin runs in its own context

## Architectural role

The plugin system is a key element of DAP extensibility:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK Core  │───▶│  Plugin System  │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Base      │             │Dynamic  │
    │modules   │             │plugins  │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Static    │◄────────────►│Runtime  │
    │build     │             │loading  │
    └─────────┘             └─────────┘
```

## Key components

### 1. Plugin manifest
```c
typedef struct dap_plugin_manifest {
    char name[64];                 // Plugin name
    char *version;                 // Version
    char *author;                  // Author
    char *description;             // Description

    char *type;                    // Plugin type
    const char *path;              // Directory path
    dap_config_t *config;          // Configuration

    // Dependencies
    struct dap_plugin_manifest_dependence *dependencies;
    char **dependencies_names;
    size_t dependencies_count;

    // Parameters
    size_t params_count;
    char **params;

    // Settings
    bool is_builtin;               // Built-in plugin

    UT_hash_handle hh;             // Hash table handle
} dap_plugin_manifest_t;
```

### 2. Plugin dependencies
```c
typedef struct dap_plugin_manifest_dependence {
    char name[64];                 // Dependency name
    dap_plugin_manifest_t *manifest; // Manifest reference
    UT_hash_handle hh;             // Hash table handle
} dap_plugin_manifest_dependence_t;
```

### 3. Plugin types
```c
typedef struct dap_plugin_type_callbacks {
    dap_plugin_type_callback_load_t load;     // Load callback
    dap_plugin_type_callback_unload_t unload; // Unload callback
} dap_plugin_type_callbacks_t;
```

## Plugin statuses

```c
typedef enum dap_plugin_status {
    STATUS_RUNNING,  // Plugin is running
    STATUS_STOPPED,  // Plugin is stopped
    STATUS_NONE      // Plugin not found
} dap_plugin_status_t;
```

## Core functions

### Initialization and management

#### `dap_plugin_init()`
```c
int dap_plugin_init(const char *a_root_path);
```

Initializes the plugin system.

**Parameters:**
- `a_root_path` - root path for plugin discovery

**Return values:**
- `0` - initialized successfully
- `-1` - initialization error

#### `dap_plugin_deinit()`
```c
void dap_plugin_deinit();
```

Deinitializes the plugin system.

### Plugin type management

#### `dap_plugin_type_create()`
```c
int dap_plugin_type_create(const char *a_name,
                          dap_plugin_type_callbacks_t *a_callbacks);
```

Creates a new plugin type with callbacks.

**Parameters:**
- `a_name` - plugin type name
- `a_callbacks` - callbacks structure

**Callback types:**
```c
typedef int (*dap_plugin_type_callback_load_t)(
    dap_plugin_manifest_t *a_manifest,
    void **a_pvt_data,
    char **a_error_str);

typedef int (*dap_plugin_type_callback_unload_t)(
    dap_plugin_manifest_t *a_manifest,
    void *a_pvt_data,
    char **a_error_str);
```

### Plugin lifecycle management

#### `dap_plugin_start_all()`
```c
void dap_plugin_start_all();
```

Starts all loaded plugins.

#### `dap_plugin_stop_all()`
```c
void dap_plugin_stop_all();
```

Stops all running plugins.

#### `dap_plugin_start()`
```c
int dap_plugin_start(const char *a_name);
```

Starts a specific plugin by name.

**Return values:**
- `0` - started successfully
- `-1` - start failed

#### `dap_plugin_stop()`
```c
int dap_plugin_stop(const char *a_name);
```

Stops a specific plugin by name.

**Return values:**
- `0` - stopped successfully
- `-1` - stop failed

### Getting status

#### `dap_plugin_status()`
```c
dap_plugin_status_t dap_plugin_status(const char *a_name);
```

Gets plugin status by name.

**Return values:**
- `STATUS_RUNNING` - plugin is running
- `STATUS_STOPPED` - plugin is stopped
- `STATUS_NONE` - plugin not found

## Working with manifests

### Manifest initialization

#### `dap_plugin_manifest_init()`
```c
int dap_plugin_manifest_init();
```

Initializes the plugin manifest subsystem.

#### `dap_plugin_manifest_deinit()`
```c
void dap_plugin_manifest_deinit();
```

Deinitializes the manifest subsystem.

### Manifest management

#### `dap_plugin_manifest_all()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_all(void);
```

Returns the list of all loaded manifests.

**Return value:**
- Pointer to the first manifest in the list (uthash)

#### `dap_plugin_manifest_find()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_find(const char *a_name);
```

Finds a plugin manifest by name.

**Return value:**
- Pointer to the found manifest or NULL

### Adding plugins

#### `dap_plugin_manifest_add_from_file()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_add_from_file(const char *a_file_path);
```

Adds a plugin from a manifest file.

**Parameters:**
- `a_file_path` - path to the manifest file

**Return value:**
- Pointer to the created manifest or NULL on error

#### `dap_plugin_manifest_add_builtin()`
```c
dap_plugin_manifest_t *dap_plugin_manifest_add_builtin(
    const char *a_name, const char *a_type,
    const char *a_author, const char *a_version,
    const char *a_description, char **a_dependencies_names,
    size_t a_dependencies_count, char **a_params,
    size_t a_params_count);
```

Adds a built‑in plugin programmatically.

**Return value:**
- Pointer to the created manifest or NULL on error

### Removing plugins

#### `dap_plugins_manifest_remove()`
```c
bool dap_plugins_manifest_remove(const char *a_name);
```

Removes a plugin by name.

**Return value:**
- `true` - removed successfully
- `false` - removal failed

## Plugin file structure

### Plugin directory
```
plugin_name/
├── manifest.json     # Plugin manifest
├── libplugin.so      # Binary library
├── config/           # Configuration files
│   └── plugin.cfg
└── data/             # Plugin data
    └── ...
```

### Manifest format (JSON)
```json
{
  "name": "example_plugin",
  "version": "1.0.0",
  "author": "Developer Name",
  "description": "Example plugin for DAP SDK",
  "type": "module",
  "dependencies": ["core", "net"],
  "params": ["debug", "timeout"],
  "builtin": false
}
```

## Plugin types

### 1. Module plugins
- Extend base module functionality
- May add new algorithms, protocols
- Example: crypto modules, network protocols

### 2. Service plugins
- Provide services to applications
- Implement business logic
- Example: payment processing, authentication

### 3. Driver plugins
- Interfaces to external systems
- Hardware/service abstraction
- Example: database drivers, cloud services

## Dependency system

### Dependency resolution
```
Plugin A ──┐
           ├──► Dependency Resolution ──► Plugin C
Plugin B ──┘                              │
                                         ▼
                                    Plugin D
```

### Checking dependencies
```c
// Get dependency list
char *deps = dap_plugin_manifests_get_list_dependencies(manifest);
printf("Dependencies: %s\n", deps);
free(deps);
```

## Plugin lifecycle

### 1. Load
```
Find manifest → Check dependencies → Load library
    ↓
Initialization → Register callbacks → Start
```

### 2. Execution
```
Event processing → Plugin function calls
    ↓
Interaction with other plugins
    ↓
Service provisioning
```

### 3. Unload
```
Stop services → Cleanup → Unload library
    ↓
Free resources → Remove from registry
```

## Security and isolation

### Plugin isolation
- Each plugin operates in its own address space
- Restricted access to system resources
- Execution sandboxing

### Validation
- Verify binary integrity
- Manifest verification
- Dependency control

## Usage

### Basic initialization

```c
#include "dap_plugin.h"

// Initialize plugin system
if (dap_plugin_init("./plugins") != 0) {
    fprintf(stderr, "Failed to initialize plugin system\n");
    return -1;
}

// Register plugin type
dap_plugin_type_callbacks_t callbacks = {
    .load = my_plugin_load_callback,
    .unload = my_plugin_unload_callback
};

dap_plugin_type_create("my_type", &callbacks);

// Start all plugins
dap_plugin_start_all();

// Main application logic
// ...

// Stop and deinitialize
dap_plugin_stop_all();
dap_plugin_deinit();
```

### Creating a plugin type

```c
int my_plugin_load_callback(dap_plugin_manifest_t *manifest,
                           void **pvt_data, char **error_str) {
    // Plugin initialization
    *pvt_data = malloc(sizeof(my_plugin_data_t));

    // Load configuration
    if (manifest->config) {
        // Process configuration
    }

    return 0; // Success
}

int my_plugin_unload_callback(dap_plugin_manifest_t *manifest,
                             void *pvt_data, char **error_str) {
    // Cleanup resources
    free(pvt_data);
    return 0; // Success
}
```

### Working with manifests

```c
// Find plugin
dap_plugin_manifest_t *plugin = dap_plugin_manifest_find("my_plugin");
if (plugin) {
    printf("Plugin version: %s\n", plugin->version);
    printf("Plugin type: %s\n", plugin->type);
}

// Iterate all plugins
dap_plugin_manifest_t *current, *tmp;
HASH_ITER(hh, dap_plugin_manifest_all(), current, tmp) {
    printf("Plugin: %s (%s)\n", current->name, current->version);
}
```

### Lifecycle management

```c
// Check status
dap_plugin_status_t status = dap_plugin_status("my_plugin");
switch (status) {
    case STATUS_RUNNING:
        printf("Plugin is running\n");
        break;
    case STATUS_STOPPED:
        printf("Plugin is stopped\n");
        break;
    case STATUS_NONE:
        printf("Plugin not found\n");
        break;
}

// Manage plugin
if (dap_plugin_start("my_plugin") == 0) {
    printf("Plugin started successfully\n");
}

// Stop after some time
sleep(10);
dap_plugin_stop("my_plugin");
```

## Integration with other modules

### DAP Config
- Load plugin configuration
- Manage parameters
- Validate settings

### DAP Common
- Common data structures
- Memory utilities
- Logging and debugging

### DAP Time
- Plugin lifetime management
- Timers and schedulers
- Operation synchronization

## Best practices

### 1. Plugin design
```c
// Clearly define the interface
typedef struct my_plugin_interface {
    int (*init)(void *config);
    void (*cleanup)(void);
    int (*process)(void *data);
} my_plugin_interface_t;
```

### 2. Dependency management
```c
// Explicitly declare dependencies
const char *dependencies[] = {
    "core",
    "net",
    "crypto"
};

// Check dependency availability
for (size_t i = 0; i < ARRAY_SIZE(dependencies); i++) {
    if (!dap_plugin_manifest_find(dependencies[i])) {
        return -1; // Dependency not found
    }
}
```

### 3. Error handling
```c
// Safe plugin loading
int load_result = dap_plugin_start(plugin_name);
if (load_result != 0) {
    log_error("Failed to load plugin %s: %s", plugin_name, strerror(errno));

    // Attempt rollback
    if (fallback_plugin) {
        dap_plugin_start(fallback_plugin);
    }
}
```

### 4. Resource management
```c
// RAII pattern for plugins
typedef struct plugin_guard {
    char *name;
    bool loaded;
} plugin_guard_t;

void plugin_guard_cleanup(plugin_guard_t *guard) {
    if (guard->loaded) {
        dap_plugin_stop(guard->name);
    }
    free(guard->name);
}
```

## Debugging and monitoring

### Logging
```c
// Enable debug output
#define DAP_PLUGIN_DEBUG 1

// Log plugin operations
log_info("Plugin %s loaded successfully", manifest->name);
log_debug("Plugin %s config: %s", manifest->name,
          dap_config_to_string(manifest->config));
```

### State monitoring
```c
// Periodic plugin status check
void check_plugins_status() {
    dap_plugin_manifest_t *current, *tmp;
    HASH_ITER(hh, dap_plugin_manifest_all(), current, tmp) {
        dap_plugin_status_t status = dap_plugin_status(current->name);
        if (status != STATUS_RUNNING) {
            log_warning("Plugin %s is not running (status: %d)",
                       current->name, status);
        }
    }
}
```

## Common issues

### 1. Dependency conflicts
```
Symptom: Plugin fails to load due to cyclic dependencies
Solution: Redesign dependency architecture
```

### 2. Memory leaks
```
Symptom: Memory usage grows during plugin load/unload
Solution: Proper implementation of cleanup callbacks
```

### 3. Thread safety
```
Symptom: Race conditions on concurrent plugin access
Solution: Use mutexes and atomic operations
```

## Conclusion

The `dap_plugin` module provides a powerful and flexible system for extending DAP SDK functionality. Its architecture ensures safe loading, dependency management, and hot swapping of components without application restarts.

