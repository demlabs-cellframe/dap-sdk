project(test_http_client_integration)

add_executable(${PROJECT_NAME}
    test_http_client_integration.c
)

# Link both client AND server libraries (true integration test)
target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    dap_io
    dap_client
    dap_server
    dap_http_server
    dap_http_common
    dap_enc_server
    dap_session
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/http_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/server/enc_server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/session/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "integration;net;client;server"
)

