project(test_stream_integration)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_stream_integration.c
)

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_client
    dap_stream
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../net/stream/stream/include
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

