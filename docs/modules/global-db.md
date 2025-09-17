# DAP Global DB Module (dap_global_db.h)

## Overview

The `dap_global_db` module is a high‑performance distributed key‑value store for DAP SDK. It provides:

- **Multiple storage drivers** - MDBX, PostgreSQL, SQLite
- **Distributed synchronization** - automatic replication between nodes
- **Clustering** - roles and permissions
- **Cryptographic protection** - signing and verification
- **Timestamps** - nanosecond precision with change history

## Architectural role

Global DB is the central data store in the DAP ecosystem:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK       │───▶│  Global DB      │
│   Applications  │    │                 │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Blockchain│             │Storage  │
    │data      │             │drivers  │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │P2P sync  │◄────────────►│Node     │
    │network   │             │clusters │
    └─────────┘             └─────────┘
```

## Core components

### 1. Database instance
```c
typedef struct dap_global_db_instance {
    uint32_t version;              // GlobalDB version
    char *storage_path;            // Storage path
    char *driver_name;             // Driver name
    dap_list_t *whitelist;         // Whitelist
    dap_list_t *blacklist;         // Blacklist
    uint64_t store_time_limit;     // Storage time limit
    dap_global_db_cluster_t *clusters; // Clusters
    dap_enc_key_t *signing_key;    // Signing key
    uint32_t sync_idle_time;       // Sync idle time
} dap_global_db_instance_t;
```

### 2. Storage object
```c
typedef struct dap_global_db_obj {
    char *key;                     // Key
    uint8_t *value;                // Value
    size_t value_len;              // Value length
    dap_nanotime_t timestamp;      // Timestamp
    bool is_pinned;                // Pinned flag
} dap_global_db_obj_t;
```

### 3. Driver store object
```c
typedef struct dap_store_obj {
    char *group;                   // Group (table‑like)
    char *key;                     // Key
    byte_t *value;                 // Value
    size_t value_len;              // Value length
    uint8_t flags;                 // Record flags
    dap_sign_t *sign;              // Cryptographic signature
    dap_nanotime_t timestamp;      // Timestamp
    uint64_t crc;                  // Checksum
    byte_t ext[];                  // Extra data
} dap_store_obj_t;
```

## Supported drivers

### MDBX (recommended)
- **High performance** - optimized B‑tree
- **ACID transactions** - atomic operations
- **MVCC** - multiversion concurrency control
- **Crash resistance**

### PostgreSQL
- **SQL compatibility** - standard queries
- **Scalability** - large datasets
- **Replication** - built‑in
- **Extensions**

### SQLite
- **Embeddable** - zero configuration
- **Simplicity** - minimal dependencies
- **Reliability** - time‑proven
- **Cross‑platform**

## Core operations

### Initialization and management

#### `dap_global_db_init()`
```c
int dap_global_db_init();
```

Initializes the Global DB system.

**Return values:**
- `0` - initialized successfully
- `-1` - initialization error

#### `dap_global_db_deinit()`
```c
void dap_global_db_deinit();
```

Deinitializes the Global DB system.

### Synchronous operations

#### Read
```c
// Get value by key
byte_t *dap_global_db_get_sync(const char *a_group, const char *a_key,
                              size_t *a_data_size, bool *a_is_pinned,
                              dap_nanotime_t *a_ts);

// Get last value in a group
byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key,
                                   size_t *a_data_size, bool *a_is_pinned,
                                   dap_nanotime_t *a_ts);

// Get all values in a group
dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group,
                                              size_t *a_objs_count);
```

#### Write
```c
// Set value
int dap_global_db_set_sync(const char *a_group, const char *a_key,
                          const void *a_value, const size_t a_value_length,
                          bool a_pin_value);

// Pin/unpin value
int dap_global_db_pin_sync(const char *a_group, const char *a_key);
int dap_global_db_unpin_sync(const char *a_group, const char *a_key);

// Delete value
int dap_global_db_del_sync(const char *a_group, const char *a_key);
```

#### Data management
```c
// Erase entire group
int dap_global_db_erase_table_sync(const char *a_group);

// Flush changes to disk
int dap_global_db_flush_sync();
```

### Asynchronous operations

#### Async read
```c
// Get value asynchronously
int dap_global_db_get(const char *a_group, const char *a_key,
                     dap_global_db_callback_result_t a_callback,
                     void *a_arg);

// Get all values asynchronously
int dap_global_db_get_all(const char *a_group, size_t l_results_page_size,
                         dap_global_db_callback_results_t a_callback,
                         void *a_arg);
```

**Callback type:**
```c
typedef void (*dap_global_db_callback_result_t)(
    dap_global_db_instance_t *a_dbi, int a_rc, const char *a_group,
    const char *a_key, const void *a_value, const size_t a_value_size,
    dap_nanotime_t a_value_ts, bool a_is_pinned, void *a_arg);
```

#### Async write
```c
// Set value asynchronously
int dap_global_db_set(const char *a_group, const char *a_key,
                     const void *a_value, const size_t a_value_length,
                     bool a_pin_value,
                     dap_global_db_callback_result_t a_callback,
                     void *a_arg);

// Set multiple values
int dap_global_db_set_multiple_zc(const char *a_group,
                                 dap_global_db_obj_t *a_values,
                                 size_t a_values_count,
                                 dap_global_db_callback_results_t a_callback,
                                 void *a_arg);
```

## Clustering and synchronization

### Cluster architecture
```
┌─────────────┐    ┌─────────────┐
│ Master Node │◄──►│ Slave Node  │
│             │    │             │
│ ┌─────────┐ │    │ ┌─────────┐ │
│ │Global DB│ │    │ │Global DB│ │
│ └─────────┘ │    │ └─────────┘ │
└─────────────┘    └─────────────┘
       │                │
       └────────────────┘
          Synchronization
```

### Node roles
- **Master** - primary node, accepts writes
- **Slave** - secondary node, receives replication
- **Witness** - integrity verifier

### Data synchronization
```c
#define DAP_GLOBAL_DB_SYNC_WAIT_TIMEOUT 5 // Sync wait timeout
```

## Cryptographic protection

### Data signing
```c
// Signing key in the instance
dap_enc_key_t *signing_key; // Used to sign all records
```

### Integrity verification
```c
// CRC checksum
uint64_t crc; // Calculated per record

// Cryptographic signature
dap_sign_t *sign; // Signature for authentication
```

### Record flags
```c
#define DAP_GLOBAL_DB_RECORD_DEL     BIT(0)  // Record deleted
#define DAP_GLOBAL_DB_RECORD_PINNED  BIT(1)  // Record pinned
#define DAP_GLOBAL_DB_RECORD_NEW     BIT(6)  // New record
#define DAP_GLOBAL_DB_RECORD_ERASE   BIT(7)  // Permanent erase
```

## Performance and optimizations

### Storage optimizations
- **Compression** - reduce storage size
- **Indexing** - fast key lookups
- **Caching** - in‑memory cache for frequent queries
- **Batch operations** - lower overhead

### Scalability
- **Horizontal scaling** - across clusters
- **Vertical scaling** - add node resources
- **Auto balancing** - load distribution

### Monitoring
```c
extern int g_dap_global_db_debug_more; // Extended logging
```

## Usage

### Basic initialization

```c
#include "dap_global_db.h"

// Initialization
if (dap_global_db_init() != 0) {
    fprintf(stderr, "Failed to initialize Global DB\n");
    return -1;
}

// Get instance
dap_global_db_instance_t *dbi = dap_global_db_instance_get_default();

// Core operations
// ... usage ...

// Deinitialization
dap_global_db_deinit();
```

### Synchronous operations

```c
// Write data
const char *group = "user_data";
const char *key = "user123";
const char *value = "user information";
size_t value_len = strlen(value);

int result = dap_global_db_set_sync(group, key, value, value_len, false);
if (result != 0) {
    fprintf(stderr, "Failed to set value\n");
}

// Read data
size_t data_size;
bool is_pinned;
dap_nanotime_t timestamp;

byte_t *data = dap_global_db_get_sync(group, key, &data_size,
                                     &is_pinned, &timestamp);
if (data) {
    printf("Value: %.*s\n", (int)data_size, data);
    free(data);
}
```

### Asynchronous operations

```c
void on_db_result(dap_global_db_instance_t *dbi, int rc,
                 const char *group, const char *key,
                 const void *value, size_t value_size,
                 dap_nanotime_t ts, bool is_pinned, void *arg) {
    if (rc == DAP_GLOBAL_DB_RC_SUCCESS) {
        printf("Async read successful: %.*s\n", (int)value_size, (char*)value);
    }
}

// Asynchronous read
dap_global_db_get("user_data", "user123", on_db_result, NULL);
```

### Working with groups

```c
// Get all records in a group
size_t count;
dap_global_db_obj_t *objs = dap_global_db_get_all_sync("user_data", &count);

for (size_t i = 0; i < count; i++) {
    printf("Key: %s, Value: %.*s\n",
           objs[i].key, (int)objs[i].value_len, objs[i].value);
}

// Free memory
dap_global_db_objs_delete(objs, count);
```

## Return codes

```c
#define DAP_GLOBAL_DB_RC_SUCCESS     0  // Success
#define DAP_GLOBAL_DB_RC_NOT_FOUND   1  // Not found
#define DAP_GLOBAL_DB_RC_PROGRESS    2  // In progress
#define DAP_GLOBAL_DB_RC_NO_RESULTS -1  // No results
#define DAP_GLOBAL_DB_RC_CRITICAL   -3  // Critical error
#define DAP_GLOBAL_DB_RC_ERROR      -6  // General error
```

## Integration with other modules

### DAP Chain
- Store blockchain data
- State caching
- Synchronization between nodes

### DAP Net
- P2P data synchronization
- Distributed storage
- Replication between nodes

### DAP Crypto
- Cryptographic data protection
- Transaction signing
- Integrity verification

## Best practices

### 1. Performance
```c
// Use async operations for high load
dap_global_db_set(group, key, value, size, pin, callback, arg);

// Group operations into transactions
dap_global_db_driver_txn_start();
// ... several operations ...
dap_global_db_driver_txn_end(true); // commit
```

### 2. Security
```c
// Always check return codes
if (dap_global_db_set_sync(group, key, value, size, pin) != 0) {
    // Error handling
}

// Pin critical data
dap_global_db_pin_sync("critical_data", "important_key");
```

### 3. Resource management
```c
// Free memory after use
if (data) free(data);

// Use flush to force writes
dap_global_db_flush_sync();
```

## Common issues

### 1. Synchronization conflicts
```
Symptom: Inconsistent data between nodes
Solution: Check cluster settings and node roles
```

### 2. Write performance
```
Symptom: Slow writes under high load
Solution: Use the MDBX driver and async operations
```

### 3. Memory leaks
```
Symptom: Increasing memory consumption
Solution: Properly free `dap_global_db_obj_t` objects
```

## Conclusion

The `dap_global_db` module provides a reliable and high‑performance data store optimized for DAP distributed applications. Its architecture ensures data integrity, security, and scalability in networked environments.

