# Global DB Integration Tests
# These tests verify global database functionality with various drivers

project(test_global_db)

# Skip when no DB driver is configured
if(NOT BUILD_WITH_GDB_DRIVER_MDBX
   AND NOT BUILD_WITH_GDB_DRIVER_SQLITE
   AND NOT BUILD_WITH_GDB_DRIVER_PGSQL)
    message(STATUS "Skipping ${PROJECT_NAME}: no DB driver configured")
    return()
endif()

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    test_global_db.c
)

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

# MDBX SAFE4QEMU is a runtime-test workaround for CPU emulators. Keep AUTO as
# the default for known QEMU wrappers, but expose a cache switch for custom ARM
# runners instead of hiding this behind regex-only logic.
set(DAP_GLOBAL_DB_MDBX_SAFE4QEMU "AUTO" CACHE STRING "Use MDBX_SAFE4QEMU for GlobalDB tests: AUTO, ON, or OFF")
set_property(CACHE DAP_GLOBAL_DB_MDBX_SAFE4QEMU PROPERTY STRINGS AUTO ON OFF)

# Add database driver libraries based on build configuration (after dap_test_link_libraries)
if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(l_mdbx_safe4qemu OFF)
    if(DAP_GLOBAL_DB_MDBX_SAFE4QEMU STREQUAL "AUTO")
        if(CMAKE_CROSSCOMPILING_EMULATOR MATCHES "(^|/)(qemu-|run-arm32|run-arm64)")
            set(l_mdbx_safe4qemu ON)
        endif()
    elseif(DAP_GLOBAL_DB_MDBX_SAFE4QEMU)
        set(l_mdbx_safe4qemu ON)
    endif()
    if(TARGET mdbx-static AND l_mdbx_safe4qemu)
        target_compile_definitions(mdbx-static PRIVATE MDBX_SAFE4QEMU)
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE mdbx-static)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_MDBX)
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_SQLITE)
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    target_link_libraries(${PROJECT_NAME} PRIVATE pq)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_PGSQL)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE ssl crypto z pgcommon pgport)
    endif()
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300
    LABELS "integration;global-db;database"
)
