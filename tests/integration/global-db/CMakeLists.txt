# Global DB Integration Tests
# These tests verify global database functionality with various drivers

project(test_global_db)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_global_db.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_link_manager
    dap_json-c
    dap_global_db
    dap_test
)

# Add database driver libraries based on build configuration
if(BUILD_WITH_GDB_DRIVER_MDBX)
    target_link_libraries(${PROJECT_NAME} mdbx-static)
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    target_link_libraries(${PROJECT_NAME} sqlite3)
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    target_link_libraries(${PROJECT_NAME} pq)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} ssl crypto z pgcommon pgport)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../io/include
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300
    LABELS "integration;global-db;database"
)

