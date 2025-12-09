cmake_minimum_required(VERSION 3.10)

# DAP SDK Regression Tests

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Known Issues Regression
# ============================================================================
project(test_known_issues)

add_executable(${PROJECT_NAME}
    test_known_issues.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 600
    LABELS "regression;bugfix"
)

message(STATUS "[TESTS] Added regression tests")

