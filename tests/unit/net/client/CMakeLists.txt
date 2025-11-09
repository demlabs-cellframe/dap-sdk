cmake_minimum_required(VERSION 3.10)

# DAP SDK HTTP Client Unit Tests
project(test_http_client_unit)

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME} 
    test_http_client.c
    test_http_client_mocks.c
    test_http_client_mocks.h
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 60
    LABELS "unit;net;client"
)

# ============================================================================
# Test: DAP Client Module
# ============================================================================
project(test_dap_client)

add_executable(${PROJECT_NAME}
    test_dap_client.c
    test_dap_client_mocks.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 60
    LABELS "unit;net;client"
)

