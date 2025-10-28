cmake_minimum_required(VERSION 3.10)

# DAP SDK Unit Tests

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Test: Stream Transport Layer
# ============================================================================
project(test_stream_transport)

add_executable(${PROJECT_NAME}
    test_stream_transport.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_session
    dap_enc_server
    dap_http_server
    dap_http_common
    dap_client
    dap_global_db
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../io/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../global-db/include
)

# Automatic linker wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (5 minutes)
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)

# ============================================================================
# Test: Stream Obfuscation Layer
# ============================================================================
project(test_stream_obfuscation)

add_executable(${PROJECT_NAME}
    test_stream_obfuscation.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_test
)

# Include test framework headers and fixtures
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../io/include
)

# Automatic linker wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (5 minutes)
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)

