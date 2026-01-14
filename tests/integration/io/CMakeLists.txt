cmake_minimum_required(VERSION 3.10)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

project(test_thread_pool_integration C)

# Create executable
add_executable(${PROJECT_NAME}
    test_thread_pool_integration.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (2 minutes for stress test)
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "integration;io;thread_pool"
)

message(STATUS "[INTEGRATION TEST] Configured: ${PROJECT_NAME}")

