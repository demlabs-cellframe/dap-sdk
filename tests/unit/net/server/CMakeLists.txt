cmake_minimum_required(VERSION 3.10)

# DAP SDK HTTP Server Unit Tests
# Includes comprehensive unit tests with full mocking

# Include DAP Mock AutoWrap helper (corrected path)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test/mocks/DAPMockAutoWrap.cmake)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: HTTP Server Module
# ============================================================================
project(test_dap_http_server)

add_executable(${PROJECT_NAME}
    test_dap_http_server.c
)

# Link all DAP SDK libraries using helper function
# This now uses STATIC libraries (not object files) to enable --wrap for mocking
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Additional includes specific to HTTP server tests
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/http/include
)

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
# Also automatically wraps all *_static libraries with --whole-archive to enable proper mocking
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 60
    LABELS "unit;net;server;http"
)

