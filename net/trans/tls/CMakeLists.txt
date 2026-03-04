cmake_minimum_required(VERSION 3.10)
project(dap_trans_tls C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

set(TLS_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_stream_trans_tls.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_tls_mimicry.c"
)

file(GLOB TLS_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(dap_trans_tls DAP_INTERNAL_MODULES ${TLS_SRCS} HEADERS ${TLS_HDRS})

target_include_directories(dap_trans_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(dap_trans_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dap_trans_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

dap_register_module(
    MODULE_NAME "trans_tls"
    VERSION 1
    INIT_FUNCTION "dap_stream_trans_tls_register"
    DEINIT_FUNCTION "dap_stream_trans_tls_unregister"
    DEPENDENCIES "dap_net_trans"
    INCLUDE_HEADER "dap_stream_trans_tls.h"
)

dap_link_libraries(dap_trans_tls INTERFACE
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_net_trans
)
