cmake_minimum_required(VERSION 3.10)

# DAP SDK Core Unit Tests

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: DAP Common Macros
# ============================================================================
project(test_dap_common)

add_executable(${PROJECT_NAME}
    test_dap_common.c
)

# Add includes
dap_test_add_includes(${PROJECT_NAME})

# Link all SDK modules + dap_test
dap_test_link_libraries(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;common;macros"
)

# ============================================================================
# Test: DAP Config
# ============================================================================
project(test_dap_config)

add_executable(${PROJECT_NAME}
    test_dap_config.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;config"
)

# ============================================================================
# Test: DAP String Functions
# ============================================================================
project(test_dap_strfuncs)

add_executable(${PROJECT_NAME}
    test_dap_strfuncs.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;strfuncs"
)

# ============================================================================
# Test: DAP Circular Buffer (Unix-specific)
# ============================================================================
if(UNIX)
    project(test_dap_circular)

    add_executable(${PROJECT_NAME}
        test_dap_circular.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES 
        TIMEOUT 300
        LABELS "unit;core;circular"
    )
endif()

# ============================================================================
# Test: DAP CPU Monitor (Linux-only, requires /proc/stat)
# ============================================================================
if(UNIX AND NOT APPLE)
    project(test_dap_cpu_monitor)

    add_executable(${PROJECT_NAME}
        test_dap_cpu_monitor.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES 
        TIMEOUT 300
        LABELS "unit;core;cpu-monitor"
    )
endif()

# ============================================================================
# Test: DAP Process Memory (Linux-only, requires /proc/self/status)
# ============================================================================
if(UNIX AND NOT APPLE)
    project(test_dap_process_memory)

    add_executable(${PROJECT_NAME}
        test_dap_process_memory.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES 
        TIMEOUT 300
        LABELS "unit;core;process-memory"
    )
endif()

# ============================================================================
# Test: DAP Arena Allocator
# ============================================================================
project(test_dap_arena)

add_executable(${PROJECT_NAME}
    test_dap_arena.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;arena;memory"
)

# ============================================================================
# Test: DAP Arena Refcount (hybrid approach)
# ============================================================================
project(test_arena_refcount)

add_executable(${PROJECT_NAME}
    test_arena_refcount.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;arena;refcount;memory"
)

# ============================================================================
# Test: DAP String Pool
# ============================================================================
project(test_dap_string_pool)

add_executable(${PROJECT_NAME}
    test_dap_string_pool.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;string_pool;memory"
)

# rpmalloc tests removed - library was optional and never used

# ============================================================================
# Test: DAP Doubly-Linked List (dap_dl)
# ============================================================================
project(test_dap_dl)

add_executable(${PROJECT_NAME}
    test_dap_dl.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;dap_dl;list"
)

# ============================================================================
# Test: DAP Hash Table (dap_ht)
# ============================================================================
project(test_dap_ht)

add_executable(${PROJECT_NAME}
    test_dap_ht.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;dap_ht;hash_table"
)

# ============================================================================
# Test: DAP Fast Hash Functions (dap_hash_fast)
# ============================================================================
project(test_dap_hash_fast)

add_executable(${PROJECT_NAME}
    test_dap_hash_fast.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;dap_hash_fast;hash"
)

# ============================================================================
# Test: DAP Memory-Mapped File I/O (dap_mmap)
# ============================================================================
if(UNIX)
    project(test_dap_mmap)

    add_executable(${PROJECT_NAME}
        test_dap_mmap.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES 
        TIMEOUT 300
        LABELS "unit;core;dap_mmap;io"
    )
endif()

# uint256_t test suite (converted from GoogleTest/Boost to dap_test)
add_subdirectory(uint256_t)
