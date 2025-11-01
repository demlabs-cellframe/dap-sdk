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
    dap_stream_ch
    dap_session
    dap_http_server
    dap_http_common
    dap_enc_server
    dap_link_manager
    dap_client
    dap_json-c
    dap_global_db
    dap_test
)

# Add database driver libraries based on build configuration
if(BUILD_WITH_GDB_DRIVER_MDBX)
    target_link_libraries(${PROJECT_NAME} mdbx-static)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_MDBX)
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    target_link_libraries(${PROJECT_NAME} sqlite3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_SQLITE)
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    target_link_libraries(${PROJECT_NAME} pq)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_CHAIN_GDB_ENGINE_PGSQL)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} ssl crypto z pgcommon pgport)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/stream/ch/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/link_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/client/include
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

