# XHTTP (Xray splithttp) transport module (SOLID isolation, plan phase 3):
#   xhttp core   -- VLESS codec + nghttp2-backed session
#   xhttp port   -- dap_tls-backed TLS port (h2 ALPN)
#   xhttp adapter-- trust + carrier bridge + carrier_dap glue
#   nghttp2      -- vendored HTTP/2 C library (cross-platform)
#
# Dual mode: top-level standalone core + unit tests; in-tree SDK module
# dap_trans_xhttp gated by DAP_SDK_NET_TRANS_XHTTP (auto with DAP_TLS13_OPENSSL).

cmake_minimum_required(VERSION 3.16)

# ---- vendored nghttp2 (static) ----
file(GLOB NGHTTP2_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/*.c)
add_library(nghttp2_vendor STATIC ${NGHTTP2_SRCS})
target_include_directories(nghttp2_vendor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/includes)
target_compile_definitions(nghttp2_vendor PUBLIC NGHTTP2_STATICLIB)
if(NOT MSVC)
    target_compile_options(nghttp2_vendor PRIVATE -w)
endif()

if(TARGET dap_core)
    if(DAP_TLS13_OPENSSL AND NOT DEFINED DAP_SDK_NET_TRANS_XHTTP)
        set(DAP_SDK_NET_TRANS_XHTTP ON CACHE BOOL "XHTTP client transport adapter" FORCE)
    else()
        option(DAP_SDK_NET_TRANS_XHTTP "XHTTP client transport adapter" OFF)
    endif()

    if(DAP_SDK_NET_TRANS_XHTTP)
        if(NOT TARGET dap_crypto_tls)
            message(FATAL_ERROR "DAP_SDK_NET_TRANS_XHTTP requires BUILD_WITH_TLS=ON "
                                "with DAP_TLS_BACKEND=openssl or boringssl")
        endif()
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

        set(XHTTP_MODULE_SRCS
            core/xhttp_codec.c
            core/xhttp_session.c
            port/xhttp_tls_dap.c
            adapter/dap_net_trans_xhttp.c
        )
        create_object_library(dap_trans_xhttp DAP_INTERNAL_MODULES "${XHTTP_MODULE_SRCS}" HEADERS "")
        target_compile_definitions(dap_trans_xhttp PRIVATE DAP_SDK_NET_TRANS_XHTTP=1)
        target_include_directories(dap_trans_xhttp PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier_dap
            ${CMAKE_CURRENT_SOURCE_DIR}/../tls
        )
        target_link_libraries(dap_trans_xhttp PRIVATE nghttp2_vendor)
        dap_link_libraries(dap_trans_xhttp INTERFACE
            dap_core dap_crypto dap_crypto_tls dap_io dap_stream dap_client
            dap_net_trans dap_trans_carrier dap_trans_carrier_dap nghttp2_vendor
        )
        dap_register_module(
            MODULE_NAME "trans_xhttp"
            VERSION 1
            INIT_FUNCTION "dap_net_trans_xhttp_register"
            DEINIT_FUNCTION "dap_net_trans_xhttp_unregister"
            DEPENDENCIES "dap_net_trans"
            INCLUDE_HEADER "dap_net_trans.h"
        )
        message(STATUS "XHTTP client transport adapter: ON")
    endif()
    return()
endif()

project(xhttp_transport C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(XHTTP_BUILD_TESTS "Build xhttp core unit tests" ON)

add_library(xhttp_core STATIC core/xhttp_codec.c core/xhttp_session.c)
target_include_directories(xhttp_core PUBLIC include)
target_link_libraries(xhttp_core PUBLIC nghttp2_vendor)
if(NOT MSVC)
    target_compile_options(xhttp_core PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(xhttp_core PUBLIC ws2_32)
endif()

if(XHTTP_BUILD_TESTS)
    enable_testing()
    add_executable(test_xhttp_unit tests/test_unit.c)
    target_link_libraries(test_xhttp_unit PRIVATE xhttp_core)
    add_test(NAME xhttp_unit COMMAND test_xhttp_unit)
    set_tests_properties(xhttp_unit PROPERTIES TIMEOUT 30)
endif()