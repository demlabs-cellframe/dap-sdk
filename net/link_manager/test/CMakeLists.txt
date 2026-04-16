cmake_minimum_required(VERSION 3.10)
project(dap_link_manager_test)

add_executable(${PROJECT_NAME} dap_link_manager_test.c)

target_link_libraries(${PROJECT_NAME}
    dap_core dap_crypto dap_io dap_test
    dap_link_manager dap_stream_ch dap_stream dap_session
    dap_client dap_global_db dap_http_server dap_enc_server dap_http_common
    dap_http_client dap_net_trans dap_trans_http dap_trans_websocket dap_net_server_common
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_link_manager_test
    COMMAND dap_link_manager_test
)
