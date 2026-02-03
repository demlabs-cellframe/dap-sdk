cmake_minimum_required(VERSION 3.10)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Thread Pool Integration
# ============================================================================
project(test_thread_pool_integration C)

add_executable(${PROJECT_NAME}
    test_thread_pool_integration.c
)

dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "integration;io;thread_pool"
)

message(STATUS "[INTEGRATION TEST] Configured: ${PROJECT_NAME}")

# ============================================================================
# Test: IO Flow Tiers Integration (Linux only)
# Tests sticky sessions via dap_io_flow_server with real reactor
# No fallbacks - requires BPF support
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    project(test_flow_tiers C)
    
    add_executable(${PROJECT_NAME}
        test_flow_tiers.c
    )
    
    # Link all DAP SDK libraries
    dap_test_link_libraries(${PROJECT_NAME})
    dap_test_add_includes(${PROJECT_NAME})
    
    # Add Linux-specific includes for BPF headers
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/io/linux
        ${CMAKE_SOURCE_DIR}/io/include
    )
    
    # Link pthread
    if(NOT ANDROID)
        target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
    endif()
    
    # Add to CTest
    add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
    
    # Set test properties
    # Note: This test requires BPF support. On systems without CAP_BPF it will skip.
    # Return code 77 indicates test was skipped (CTest convention)
    set_tests_properties(${PROJECT_NAME} PROPERTIES
        TIMEOUT 300
        LABELS "integration;io;flow;bpf;linux;requires_bpf"
        SKIP_RETURN_CODE 77
    )
    
    message(STATUS "[INTEGRATION TEST] Configured: ${PROJECT_NAME} (Linux, BPF sticky sessions)")
endif()
