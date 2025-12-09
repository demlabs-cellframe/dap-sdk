cmake_minimum_required(VERSION 3.10)

# DAP SDK Performance Tests

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Crypto Benchmarks
# ============================================================================
project(test_crypto_benchmarks)

add_executable(${PROJECT_NAME}
    test_crypto_benchmarks.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 1800
    LABELS "performance;benchmark;crypto;slow"
)

message(STATUS "[TESTS] Added performance tests")

