# HTTP Client+Server Integration Tests
# These tests verify full HTTP stack integration (client + server)

# Test 1: HTTP Client-Server Integration (full request/response cycle)
# ✅ Refactored with v2.1 mock API - elegant, comprehensive, async-enabled
project(test_http_client_server)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_http_client_server.c
)

# Link both client AND server libraries (true integration test)
target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_client
    dap_http_server
    dap_http_common
    dap_enc_server
    dap_session
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/http_client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

# Enable --whole-archive for libraries with mocked functions
# This makes --wrap work for functions inside static libraries
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_http_server)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "integration;net;http;client-server"
)

# Test 2: HTTP Simple Server Integration
project(test_http_simple)

# Include DAP Mock Framework  
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_http_simple.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_io
    dap_client
    dap_http_server
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
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
    TIMEOUT 120
    LABELS "integration;net;http;simple"
)

