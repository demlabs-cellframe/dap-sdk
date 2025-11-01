cmake_minimum_required(VERSION 3.10)

# DAP SDK Stream Transport Unit Tests
# Includes comprehensive unit tests with full mocking

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Test: Stream Transport Full (with mocks)
# ============================================================================
project(test_stream_transport_full)

add_executable(${PROJECT_NAME}
    test_stream_transport_full.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_stream_ch
    dap_session
    dap_http_server
    dap_http_common
    dap_enc_server
    dap_link_manager
    dap_client
    dap_global_db
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/ch/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/link_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../io/include
)

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 60
    LABELS "unit;net;stream;transport"
)

