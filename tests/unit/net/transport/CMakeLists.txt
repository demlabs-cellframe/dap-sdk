cmake_minimum_required(VERSION 3.10)

# DAP SDK Transport Server Unit Tests
# Unit tests for each transport server implementation

# Include test framework helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

# Common include directories for all transport tests
set(TRANSPORT_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/udp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/dns/include
)

# ============================================================================
# Test: HTTP Transport Server
# ============================================================================
project(test_transport_http)

add_executable(${PROJECT_NAME}
    test_transport_http.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "unit;net;transport;http"
)

# ============================================================================
# Test: WebSocket Transport Server
# ============================================================================
project(test_transport_websocket)

add_executable(${PROJECT_NAME}
    test_transport_websocket.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "unit;net;transport;websocket"
)

# ============================================================================
# Test: UDP Transport Server
# ============================================================================
project(test_transport_udp)

add_executable(${PROJECT_NAME}
    test_transport_udp.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "unit;net;transport;udp"
)

# ============================================================================
# Test: DNS Transport Server
# ============================================================================
project(test_transport_dns)

add_executable(${PROJECT_NAME}
    test_transport_dns.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "unit;net;transport;dns"
)

