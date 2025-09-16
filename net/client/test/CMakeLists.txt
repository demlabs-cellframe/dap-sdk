project(dap_client_test)

# Original external test (commented out for CI stability)
# add_executable(dap_client_test_external test_http_client.c)
# target_link_libraries(dap_client_test_external dap_core dap_client dap_test dap_io dap_http_common)

# Local test using dap_http_server
add_executable(${PROJECT_NAME} test_http_client_local.c)

target_link_libraries(${PROJECT_NAME} 
    dap_core 
    dap_client 
    dap_test 
    dap_io 
    dap_http_common
    dap_http_server
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_client_test
    COMMAND dap_client_test
)
