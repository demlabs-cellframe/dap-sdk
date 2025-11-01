# DAP SDK LibraryHelpers Usage Examples

## Universal Function: `dap_link_all_sdk_modules`

This function automatically links all DAP SDK modules and their external dependencies to your target. Works for both applications and tests.

### Basic Application Example

```cmake
cmake_minimum_required(VERSION 3.10)
project(my_dap_app)

# Include DAP SDK
add_subdirectory(dap-sdk)

# Create your application
add_executable(my_app main.c)

# Link ALL DAP SDK modules + external dependencies (XKCP, Kyber, SQLite, PostgreSQL, etc.)
# This single line replaces dozens of target_link_libraries calls!
dap_link_all_sdk_modules(my_app DAP_INTERNAL_MODULES)

# That's it! Your app now has access to all DAP SDK functionality
```

### Application with Additional Libraries

```cmake
add_executable(my_advanced_app main.c networking.c)

# Link DAP SDK + your custom libraries
dap_link_all_sdk_modules(my_advanced_app DAP_INTERNAL_MODULES
    LINK_LIBRARIES my_custom_lib my_other_lib)
```

### Test Example

```cmake
add_executable(my_test test_main.c)

# Link DAP SDK + test framework
dap_link_all_sdk_modules(my_test DAP_INTERNAL_MODULES
    LINK_LIBRARIES dap_test)
```

## What Does It Do Automatically?

1. **Collects all object files** from SDK modules (dap_core, dap_crypto, dap_io, etc.)
2. **Finds external dependencies** (XKCP, Kyber, SQLite, PostgreSQL, MDBX, etc.)
3. **Links everything** in correct order
4. **Adds system libraries** (pthread, rt, dl) as needed

## Before vs After

### Before (Old Way - Error Prone!)
```cmake
target_link_libraries(my_app PRIVATE
    dap_core
    dap_core_unix
    dap_crypto
    dap_crypto_XKCP
    dap_crypto_kyber512
    dap_io
    dap_session
    dap_stream
    dap_stream_ch
    dap_client
    dap_json-c
    sqlite3
    pq
    mdbx-static
    pthread
    rt
    dl
    # Did I forget something? Probably...
)
```

### After (New Way - Simple & Correct!)
```cmake
dap_link_all_sdk_modules(my_app DAP_INTERNAL_MODULES)
# Done! Everything is linked automatically.
```

## Benefits

- ✅ **No manual tracking** of dependencies
- ✅ **Automatic external lib detection** (XKCP, Kyber, databases, etc.)
- ✅ **Works for parallel builds** (-j flag)
- ✅ **Handles transitive dependencies** correctly
- ✅ **Clean Architecture** - separation of concerns
- ✅ **DRY principle** - write once, use everywhere

## Advanced: Collect Dependencies for Custom Use

If you need just the external libraries list without linking:

```cmake
# Get list of external libraries
collect_external_libraries_from_modules("${DAP_INTERNAL_MODULES}" MY_LIBS)

# Use them as you wish
message(STATUS "External deps: ${MY_LIBS}")
target_link_libraries(my_special_target PUBLIC ${MY_LIBS})
```

## Module Developers

When creating a new DAP SDK module, use `dap_link_libraries`:

```cmake
create_object_library(my_new_module DAP_INTERNAL_MODULES ${SOURCES})

# Link dependencies - external libs are automatically propagated!
dap_link_libraries(my_new_module INTERFACE 
    dap_core 
    dap_crypto
    my_external_lib)
```

Applications using your module will automatically get `my_external_lib` via `dap_link_all_sdk_modules`.

