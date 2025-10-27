# DAP SDK Mock Framework
# Reusable mock infrastructure for all Cellframe projects
cmake_minimum_required(VERSION 3.10)

project(dap_test_mocks C)

# Mock framework library
add_library(dap_test_mocks STATIC
    dap_mock_framework.c
)

target_include_directories(dap_test_mocks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/core/include
)

target_link_libraries(dap_test_mocks
    dap_core
    pthread
)

# Make this available for all Cellframe SDK and client projects
set(DAP_TEST_MOCKS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

