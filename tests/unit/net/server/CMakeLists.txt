cmake_minimum_required(VERSION 3.10)

# DAP SDK HTTP Server Unit Tests
# Includes comprehensive unit tests with full mocking

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Test: HTTP Server Module
# ============================================================================
project(test_dap_http_server)

add_executable(${PROJECT_NAME}
    test_dap_http_server.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_http_server
    dap_http_common
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../io/include
)

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

# Wrap static libraries for mocking internal functions
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_http_server dap_io dap_core)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 60
    LABELS "unit;net;server;http"
)

