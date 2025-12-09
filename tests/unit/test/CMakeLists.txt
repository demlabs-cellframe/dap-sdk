# DAP SDK Test Framework - Self Tests
# Unit tests for the test framework itself
cmake_minimum_required(VERSION 3.10)

# Include DAP Mock AutoWrap helper (corrected path)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../module/test/mocks/DAPMockAutoWrap.cmake)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test 1: Async Mock Tests
# ============================================================================
project(test_async)

add_executable(${PROJECT_NAME}
    test_async.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

add_test(NAME dap_async
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(dap_async PROPERTIES
    TIMEOUT 60
    LABELS "mock;unit"
)

# ============================================================================
# Test 2: Mock Framework Tests
# ============================================================================
project(test_mock)

add_executable(${PROJECT_NAME}
    test_mock.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Enable mock autowrap for test_mock_framework
# This generates linker --wrap flags for DAP_MOCK_DECLARE macros
dap_mock_autowrap(${PROJECT_NAME})

add_test(NAME dap_mock
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(dap_mock PROPERTIES
        TIMEOUT 60
        LABELS "mock;unit"
)

message(STATUS "Configured DAP SDK Test Framework self-tests")

