cmake_minimum_required(VERSION 3.10)

# DAP SDK Integration Tests

# ============================================================================
# Test: HTTP Client Integration
# ============================================================================
project(test_http_client_integration)

add_executable(${PROJECT_NAME}
    test_http_client_integration.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_client
    dap_http_common
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../io/include
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest with INTEGRATION label
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 120
    LABELS "integration;network;http"
)

# Mark as integration test (requires network)
set_tests_properties(${PROJECT_NAME} PROPERTIES
    ENVIRONMENT "DAP_TEST_TYPE=integration"
)

