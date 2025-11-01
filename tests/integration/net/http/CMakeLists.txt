# HTTP Client+Server Integration Tests
# These tests verify full HTTP stack integration (client + server)

# Test 1: HTTP Client-Server Integration (full request/response cycle)
# ✅ Refactored with v2.1 mock API - elegant, comprehensive, async-enabled
project(test_http_client_server)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    test_http_client_server.c
)

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "integration;net;http;client-server"
)

# Test 2: HTTP Simple Server Integration
project(test_http_simple)

# Include DAP Mock Framework  
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    test_http_simple.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Add fixtures include path
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
)

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "integration;net;http;simple"
)

