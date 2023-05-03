cmake_minimum_required(VERSION 3.10)
project(dap_stream C)
  
file(GLOB STREAM_SRCS *.c)
file(GLOB STREAM_HDRS include/*.h)

add_library(${PROJECT_NAME} STATIC ${STREAM_SRCS} ${STREAM_HDRS})

target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_crypto
    dap_http_server dap_enc_server dap_session dap_stream_ch dap_client)

if(DAP_STREAM_TEST)
    target_link_libraries(${PROJECT_NAME} dap_stream_test)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
