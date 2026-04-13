cmake_minimum_required(VERSION 3.10)
project(uint256_test C)

set(CMAKE_C_STANDARD 11)

# Single test executable for all uint256 test suites
add_executable(${PROJECT_NAME}
    uint256_tests.c
)

target_link_libraries(${PROJECT_NAME}
    dap_test
    dap_core
    dap_math
    pthread
    m
)

# Link macOS system frameworks (required by dap_network_monitor in dap_core)
if(APPLE)
    target_link_libraries(${PROJECT_NAME}
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ../../include/
    ../../test/
)

# Register as single test suite (not individual tests)
add_test(NAME uint256_test_suite COMMAND ${PROJECT_NAME})
