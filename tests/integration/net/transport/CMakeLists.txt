# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

# Common include directories for all transport tests
set(TRANSPORT_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/udp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/dns/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/transport_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../io/include
    ${CMAKE_CURRENT_SOURCE_DIR}  # For test_transport_helpers.h
)

# =======================================================================================
# Transport API Test (general API tests)
# =======================================================================================

project(test_transport_api)

add_executable(${PROJECT_NAME}
    test_transport_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}  # For test_transport_helpers.h
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 180
    LABELS "integration;net;transport;api"
)

# =======================================================================================
# Transport Integration Test (parallel testing)
# =======================================================================================

project(test_transport_integration)

add_executable(${PROJECT_NAME}
    test_transport_integration.c
    test_transport_helpers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${TRANSPORT_TEST_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/include
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 600
    LABELS "integration;net;transport;parallel;client-server"
)

