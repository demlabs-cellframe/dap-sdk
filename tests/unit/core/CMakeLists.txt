cmake_minimum_required(VERSION 3.10)

# DAP SDK Core Unit Tests

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Test: DAP Common Macros
# ============================================================================
project(test_dap_common)

add_executable(${PROJECT_NAME}
    test_dap_common.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
)

# Automatic linker wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (5 minutes)
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 300
    LABELS "unit;core;common;macros"
)

