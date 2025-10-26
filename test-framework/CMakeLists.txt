# DAP SDK Test Framework
# Contains reusable test utilities, mocks, and fixtures
cmake_minimum_required(VERSION 3.10)

project(dap_test_framework C)

# Add mocks subdirectory
add_subdirectory(mocks)

# Test utilities (if any exist)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dap_test.c)
    add_library(dap_test STATIC
        dap_test.c
    )
    
    target_include_directories(dap_test PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/core/include
    )
    
    target_link_libraries(dap_test
        dap_core
    )
endif()

# Export paths for downstream projects
set(DAP_TEST_FRAMEWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
