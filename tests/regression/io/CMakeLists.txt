# Regression tests for IO module
cmake_minimum_required(VERSION 3.10)

# CBPF Sticky Sessions test (Linux only)
if(LINUX)
    set(TEST_NAME test_cbpf_sticky_sessions)
    
    add_executable(${TEST_NAME}
        test_cbpf_sticky_sessions.c
    )
    
    target_link_libraries(${TEST_NAME} PRIVATE
        dap_test
        dap_io
        dap_core
        pthread
    )
    
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "regression;io;cbpf;linux"
        TIMEOUT 120
    )
    
    message(STATUS "Added regression test: ${TEST_NAME}")
endif()

# Packet Routing Regression test - tests packet routing WITHOUT FSM/encryption
# Verifies sticky sessions and detects packet "mixing" between workers.
# Uses POSIX sockets/pthread APIs directly.
if(NOT WIN32)
    set(TEST_NAME test_packet_routing_regression)

    add_executable(${TEST_NAME}
        test_packet_routing_regression.c
    )

    target_link_libraries(${TEST_NAME} PRIVATE
        dap_test
        dap_io
        dap_core
        pthread
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "regression;io;routing;multiclient"
        TIMEOUT 120
    )

    message(STATUS "Added regression test: ${TEST_NAME}")
else()
    message(STATUS "Skipping test_packet_routing_regression on WIN32")
endif()
