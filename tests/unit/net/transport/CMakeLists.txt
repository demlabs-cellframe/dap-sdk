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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/server/include
)

# ============================================================================
# Test: HTTP Transport Server
# ============================================================================
project(test_transport_http)

add_executable(${PROJECT_NAME}
    test_transport_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.c
    # Add header file for mock scanner (it won't be compiled, just scanned for DAP_MOCK_DECLARE)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h
)
# Mark header file as header-only (not compiled)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h PROPERTIES HEADER_FILE_ONLY TRUE)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
# Link with dap_http_client and other libs needed by dap_transport_test_mocks.c
target_link_libraries(${PROJECT_NAME} PRIVATE dap_http_client dap_http_server dap_http_common dap_enc_server)

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Use dap_mock_autowrap_with_static for static libraries to ensure --wrap works
# This is needed because --wrap doesn't work for symbols inside static libraries
# without --whole-archive flag
# Note: dap_server functions are in dap_io, dap_net_server_listen_addr_add_with_callback is in dap_net_server_common
# enc_http_add_proc is in dap_enc_server
# dap_stream_add_proc_http and dap_stream_ctl_add_proc are in dap_stream
# dap_http_client functions are in dap_http_client
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_transport_http dap_http_server dap_net_server_common dap_io dap_enc_server dap_stream dap_http_client)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.c
    # Add header file for mock scanner (it won't be compiled, just scanned for DAP_MOCK_DECLARE)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h
)
# Mark header file as header-only (not compiled)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h PROPERTIES HEADER_FILE_ONLY TRUE)

# Link dependencies explicitly (like test_stream_transport_full)
target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_stream_ch
    dap_session
    dap_http_server
    dap_http_common
    dap_http_client
    dap_enc_server
    dap_link_manager
    dap_client
    dap_global_db
    dap_transport_websocket
    dap_transport_http
    dap_net_transport
    dap_net_server_common
    dap_test
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/transport/http/include
    ${TRANSPORT_TEST_INCLUDE_DIRS}
)

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Use dap_mock_autowrap_with_static for static libraries to ensure --wrap works
# This is needed because --wrap doesn't work for symbols inside static libraries
# without --whole-archive flag
# Note: dap_server functions are in dap_io, dap_net_server_listen_addr_add_with_callback is in dap_net_server_common
# enc_http_add_proc is in dap_enc_server
# dap_stream_add_proc_http and dap_stream_ctl_add_proc are in dap_stream
# dap_http_client functions are in dap_http_client
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_transport_websocket dap_transport_http dap_http_server dap_net_server_common dap_io dap_enc_server dap_stream dap_http_client)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.c
    # Add header file for mock scanner (it won't be compiled, just scanned for DAP_MOCK_DECLARE)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h
)
# Mark header file as header-only (not compiled)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h PROPERTIES HEADER_FILE_ONLY TRUE)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
# Link with dap_http_client and other libs needed by dap_transport_test_mocks.c
target_link_libraries(${PROJECT_NAME} PRIVATE dap_http_client dap_http_server dap_http_common dap_enc_server)

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Use dap_mock_autowrap_with_static for static libraries to ensure --wrap works
# This is needed because --wrap doesn't work for symbols inside static libraries
# without --whole-archive flag
# dap_server_listen_addr_add is in dap_io, dap_net_server_listen_addr_add_with_callback is in dap_net_server_common
# Also wrap dap_http_server as it may call these functions
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_net_server_common dap_io dap_http_server dap_http_client)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.c
    # Add header file for mock scanner (it won't be compiled, just scanned for DAP_MOCK_DECLARE)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h
)
# Mark header file as header-only (not compiled)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_transport_test_mocks.h PROPERTIES HEADER_FILE_ONLY TRUE)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
# Link with dap_http_client and other libs needed by dap_transport_test_mocks.c
target_link_libraries(${PROJECT_NAME} PRIVATE dap_http_client dap_http_server dap_http_common dap_enc_server)

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${TRANSPORT_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Use dap_mock_autowrap_with_static for static libraries to ensure --wrap works
# This is needed because --wrap doesn't work for symbols inside static libraries
# without --whole-archive flag
# dap_server_listen_addr_add is in dap_io, dap_net_server_listen_addr_add_with_callback is in dap_net_server_common
# Also wrap dap_http_server as it may call these functions
dap_mock_autowrap_with_static(${PROJECT_NAME} dap_net_server_common dap_io dap_http_server dap_http_client)

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "unit;net;transport;dns"
)

