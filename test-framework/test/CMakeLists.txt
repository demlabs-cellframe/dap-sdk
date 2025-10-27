# DAP SDK Test Framework - Self Tests
# Unit tests for the test framework itself
cmake_minimum_required(VERSION 3.10)

project(dap_test_framework_tests C)

# Test 1: Async Framework Tests
add_executable(test_async_framework
    test_async_framework.c
)

target_include_directories(test_async_framework PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/core/include
)

target_link_libraries(test_async_framework
    dap_test
    dap_core
    pthread
)

add_test(NAME dap_async_framework
    COMMAND test_async_framework
)

# Test 2: Mock Framework Tests
add_executable(test_mock_framework
    test_mock_framework.c
)

target_include_directories(test_mock_framework PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/core/include
)

target_link_libraries(test_mock_framework
    dap_test
    dap_test_mocks
    dap_core
    pthread
)

add_test(NAME dap_mock_framework
    COMMAND test_mock_framework
)

# Set test properties
set_tests_properties(dap_async_framework dap_mock_framework
    PROPERTIES
        TIMEOUT 60
        LABELS "framework;unit"
)

message(STATUS "Configured DAP SDK Test Framework self-tests")

