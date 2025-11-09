# DAP SDK Test Framework
# Contains reusable test utilities, mocks, and fixtures
cmake_minimum_required(VERSION 3.10)

project(dap_test_framework C)


# Test utilities library
set(DAP_TEST_SOURCES "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dap_test.c)
    list(APPEND DAP_TEST_SOURCES dap_test.c)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dap_test_async.c)
    list(APPEND DAP_TEST_SOURCES dap_test_async.c)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dap_mock.c)
    list(APPEND DAP_TEST_SOURCES dap_mock.c)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dap_mock_async.c)
    list(APPEND DAP_TEST_SOURCES dap_mock_async.c)
endif()

if(DAP_TEST_SOURCES)
    add_library(dap_test STATIC
        ${DAP_TEST_SOURCES}
    )
    
    target_include_directories(dap_test PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/core/include
    )
    
    target_link_libraries(dap_test
        dap_core
        pthread
    )
    
    # Export for downstream projects
    set(DAP_TEST_LIBRARY dap_test PARENT_SCOPE)
endif()

# Export paths for downstream projects
set(DAP_TEST_FRAMEWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

# Note: Self-tests for test framework are now in tests/unit/test-framework/
# They are built via tests/unit/CMakeLists.txt
