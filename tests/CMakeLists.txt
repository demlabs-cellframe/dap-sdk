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

