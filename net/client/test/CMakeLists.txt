project(dap_client_test)

add_executable(${PROJECT_NAME} test_http_client.c)

target_link_libraries(${PROJECT_NAME} dap_core dap_client dap_test dap_io dap_http_common)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_client_test
    COMMAND dap_client_test
)
