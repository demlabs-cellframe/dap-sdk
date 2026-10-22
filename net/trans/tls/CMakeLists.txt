cmake_minimum_required(VERSION 3.10)
project(dap_trans_tls C)

option(DAP_TLS13_OPENSSL "Build the separate OpenSSL TLS13 server transport" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

set(TLS_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_stream_trans_tls.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans_tls_server.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_tls_mimicry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_tls_ja3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_tls_fingerprint.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/chrome_120.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/firefox_121.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge_120.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/safari_17.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/android_14.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/telegram_android.c"
)

file(GLOB TLS_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(dap_trans_tls DAP_INTERNAL_MODULES ${TLS_SRCS} HEADERS ${TLS_HDRS})

target_include_directories(dap_trans_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(dap_trans_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dap_trans_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(DAP_TLS13_OPENSSL)
    if(NOT TARGET dap_crypto_tls)
        message(FATAL_ERROR "TLS13 requires BUILD_WITH_TLS=ON and DAP_TLS_BACKEND=openssl")
    endif()
    if(NOT DAP_TLS_BACKEND STREQUAL "openssl")
        message(FATAL_ERROR "TLS13 memory BIO transport requires explicit DAP_TLS_BACKEND=openssl")
    endif()
    target_compile_definitions(dap_trans_tls PRIVATE DAP_TLS13_OPENSSL=1)
    target_link_libraries(dap_trans_tls INTERFACE dap_crypto_tls)
    target_include_directories(dap_trans_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/tls/include)
endif()

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
