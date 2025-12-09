cmake_minimum_required(VERSION 3.10)

# DAP SDK Tests
project(dap_sdk_tests)

# Enable testing
enable_testing()

# Include test helpers for common functions
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dap_test_helpers.cmake)

# Note: dap_sdk_object is created in the main CMakeLists.txt before tests are added
# We just verify it exists and log status
if(TARGET dap_sdk_object)
    message(STATUS "[TESTS] Using combined object library: dap_sdk_object")
    get_target_property(DAP_SDK_OBJECT_SOURCES dap_sdk_object SOURCES)
    if(DAP_SDK_OBJECT_SOURCES)
        list(LENGTH DAP_SDK_OBJECT_SOURCES SOURCE_COUNT)
        message(STATUS "[TESTS] dap_sdk_object contains ${SOURCE_COUNT} object files")
    endif()
else()
    message(STATUS "[TESTS] dap_sdk_object not found, tests will use individual libraries")
endif()

# Add unit tests subdirectory
add_subdirectory(unit)

# Add integration tests subdirectory
add_subdirectory(integration)

# Add security tests subdirectory
add_subdirectory(security)

# Add performance tests subdirectory
add_subdirectory(performance)

# Add regression tests subdirectory
add_subdirectory(regression)

# ============================================================================
# Test-Framework Tests (AWK/Shell tests)
# ============================================================================
# These tests are not C tests, so we add them as custom test commands

# Find bash
find_program(BASH_EXECUTABLE bash)
if(NOT BASH_EXECUTABLE)
    message(WARNING "bash not found, test-framework tests will be skipped")
else()
    # Get absolute path to run.sh
    get_filename_component(TEST_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/run.sh" ABSOLUTE)
    
    # Test: test-framework unit tests
    add_test(
        NAME test_framework_unit
        COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test-framework unit
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(test_framework_unit PROPERTIES
        TIMEOUT 300
        LABELS "test-framework;unit"
    )
    
    # Test: test-framework integration tests
    add_test(
        NAME test_framework_integration
        COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test-framework integration
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(test_framework_integration PROPERTIES
        TIMEOUT 300
        LABELS "test-framework;integration"
    )
    
    # Individual test-framework unit tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test-framework/test_variable_functions.sh")
        add_test(
            NAME test_framework_variable_functions
            COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test_variable_functions
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(test_framework_variable_functions PROPERTIES
            TIMEOUT 60
            LABELS "test-framework;unit"
        )
    endif()
    
    # Individual test-framework integration tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test-framework/test_c_ifdef.sh")
        add_test(
            NAME test_framework_c_ifdef
            COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test_c_ifdef
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(test_framework_c_ifdef PROPERTIES
            TIMEOUT 60
            LABELS "test-framework;integration"
        )
    endif()
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test-framework/test_return_type_macros.sh")
        add_test(
            NAME test_framework_return_type_macros
            COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test_return_type_macros
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(test_framework_return_type_macros PROPERTIES
            TIMEOUT 60
            LABELS "test-framework;integration"
        )
    endif()
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/test-framework/test_return_type_macros_dap_tpl.sh")
        add_test(
            NAME test_framework_return_type_macros_dap_tpl
            COMMAND ${BASH_EXECUTABLE} ${TEST_RUNNER} test_return_type_macros_dap_tpl
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(test_framework_return_type_macros_dap_tpl PROPERTIES
            TIMEOUT 60
            LABELS "test-framework;integration"
        )
    endif()
    
    message(STATUS "[TESTS] Added test-framework tests (AWK/Shell)")
endif()

