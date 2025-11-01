project(test_transport_integration)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    test_transport_integration.c
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
    TIMEOUT 180
    LABELS "integration;net;transport;client-server"
)

