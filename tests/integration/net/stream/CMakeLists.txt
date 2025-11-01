project(test_stream)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_client
    dap_stream
    dap_stream_ch
    dap_session
    dap_http_server
    dap_http_common
    dap_enc_server
    dap_link_manager
    dap_global_db
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/stream/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/ch/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/common/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/link_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../global-db/include
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "integration;net;stream"
)

