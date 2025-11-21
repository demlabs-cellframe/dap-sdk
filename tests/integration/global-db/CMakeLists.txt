# Global DB Integration Tests
# These tests verify global database functionality with various drivers

project(test_global_db)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    test_global_db.c
)

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add database driver libraries based on build configuration (after dap_test_link_libraries)
if(BUILD_WITH_GDB_DRIVER_MDBX)
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
