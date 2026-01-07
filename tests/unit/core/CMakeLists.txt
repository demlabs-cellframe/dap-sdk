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
# Test: DAP CPU Monitor (Unix-specific)
# ============================================================================
if(UNIX)
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
# Test: DAP Process Memory (Unix-specific)
# ============================================================================
if(UNIX)
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

