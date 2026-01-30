cmake_minimum_required(VERSION 3.10)

# Include test framework helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# Common include directories for IO tests
set(IO_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/io/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/unix
    ${CMAKE_SOURCE_DIR}/test-framework
    ${CMAKE_SOURCE_DIR}/3rdparty/uthash/src
)

# ============================================================================
# Test: IO Flow Load Balancing Tiers
# ============================================================================
project(test_io_flow_tiers)

add_executable(${PROJECT_NAME} 
    test_io_flow_tiers.c
)

# Link dependencies using helper
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 120
    LABELS "unit;io;flow;tier"
)

