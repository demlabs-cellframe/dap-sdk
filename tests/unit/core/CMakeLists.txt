cmake_minimum_required(VERSION 3.10)

<<<<<<< HEAD
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
=======
project(core_test)

set(CMAKE_C_STANDARD 11)
file(GLOB SRCS *.c)


if(UNIX)
if (OFF_CORE_CELLFRAME_SDK_TESTS_OFF MATCHES "network-monitor")
    file(GLOB PLATROFM_DEP_SRC unix/dap_cpu*.c unix/dap_process*.c unix/dap_cpu*.h unix/dap_process*.h)  
  else()
    file(GLOB PLATROFM_DEP_SRC unix/*.c unix/*.h)  
  endif()
endif()

add_executable(${PROJECT_NAME} ${SRCS} ${PLATROFM_DEP_SRC})  
target_link_libraries(core_test dap_test dap_core pthread)

# Add test framework include directory (corrected path)
target_include_directories(${PROJECT_NAME} PRIVATE ../../test)

if (DAP_USE_RPMALLOC)
add_executable(rpmalloc_test
  ../../3rdparty/rpmalloc/test/test.h
  ../../3rdparty/rpmalloc/test/thread.h 
  ../../3rdparty/rpmalloc/test/thread.c
  ../../3rdparty/rpmalloc/test/main.c
)
target_link_libraries(rpmalloc_test dap_core m pthread dl)
target_include_directories(rpmalloc_test PRIVATE ../../3rdparty/rpmalloc/test)
if (NOT PLATFORM_ARM AND VERSION MATCHES "7l")
    add_test(
        NAME rpm-test
        COMMAND rpmalloc_test
    )
else()
    message("[-] Disable rpm test")
endif()
endif(DAP_USE_RPMALLOC)

add_test(
    NAME core-test
    COMMAND core_test
)

if(UNIX)
    target_include_directories(${PROJECT_NAME} PRIVATE unix)
    target_include_directories(${PROJECT_NAME} PRIVATE unix/linux)
    # Add paths to actual Unix header files in src/unix/
    target_include_directories(${PROJECT_NAME} PRIVATE ../src/unix)
    target_include_directories(${PROJECT_NAME} PRIVATE ../src/unix/linux)
endif()
# uint256_t test suite (converted from GoogleTest/Boost to dap_test)
add_subdirectory(uint256_t)
>>>>>>> a8b8799642f830d976bc7686526ea201333815dd

