project(dap_client_test)

# Include DAP Mock Framework
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME} test_http_client.c)

target_link_libraries(${PROJECT_NAME} dap_core dap_client dap_test dap_test_mocks dap_io dap_http_common)

# Include mock framework headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_client_test
    COMMAND dap_client_test
)
