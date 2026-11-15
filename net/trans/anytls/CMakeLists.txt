# AnyTLS transport module (SOLID isolation, plan phase 2):
#   anytls core   -- codec + session (single stream), ZERO SDK includes
#   anytls port   -- dap_tls-backed TLS port (SDK-aware)
#   anytls adapter-- trust + carrier bridge + carrier_dap glue (SDK-aware)
#
# Dual mode: top-level standalone core + unit tests; in-tree SDK module
# dap_trans_anytls gated by DAP_SDK_NET_TRANS_ANYTLS (auto with DAP_TLS13_OPENSSL).

cmake_minimum_required(VERSION 3.16)

if(TARGET dap_core)
    if(DAP_TLS13_OPENSSL AND NOT DEFINED DAP_SDK_NET_TRANS_ANYTLS)
        set(DAP_SDK_NET_TRANS_ANYTLS ON CACHE BOOL "AnyTLS client transport adapter" FORCE)
    else()
        option(DAP_SDK_NET_TRANS_ANYTLS "AnyTLS client transport adapter" OFF)
    endif()

    if(DAP_SDK_NET_TRANS_ANYTLS)
        if(NOT TARGET dap_crypto_tls)
            message(FATAL_ERROR "DAP_SDK_NET_TRANS_ANYTLS requires BUILD_WITH_TLS=ON "
                                "with DAP_TLS_BACKEND=openssl or boringssl")
        endif()
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

        set(ANYTLS_MODULE_SRCS
            core/anytls_codec.c
            core/anytls_session.c
            port/anytls_tls_dap.c
            adapter/dap_net_trans_anytls.c
        )
        create_object_library(dap_trans_anytls DAP_INTERNAL_MODULES "${ANYTLS_MODULE_SRCS}" HEADERS "")
        target_compile_definitions(dap_trans_anytls PRIVATE DAP_SDK_NET_TRANS_ANYTLS=1)
        target_include_directories(dap_trans_anytls PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier_dap
            ${CMAKE_CURRENT_SOURCE_DIR}/../tls
        )
        dap_link_libraries(dap_trans_anytls INTERFACE
            dap_core dap_crypto dap_crypto_tls dap_io dap_stream dap_client
            dap_net_trans dap_trans_carrier dap_trans_carrier_dap
        )
        dap_register_module(
            MODULE_NAME "trans_anytls"
            VERSION 1
            INIT_FUNCTION "dap_net_trans_anytls_register"
            DEINIT_FUNCTION "dap_net_trans_anytls_unregister"
            DEPENDENCIES "dap_net_trans"
            INCLUDE_HEADER "dap_net_trans.h"
        )
        message(STATUS "ANYTLS client transport adapter: ON")
    endif()
    return()
endif()

project(anytls_transport C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(ANYTLS_BUILD_TESTS "Build anytls core unit tests" ON)

add_library(anytls_core STATIC core/anytls_codec.c core/anytls_session.c)
target_include_directories(anytls_core PUBLIC include)
if(NOT MSVC)
    target_compile_options(anytls_core PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(anytls_core PUBLIC ws2_32)
endif()

if(ANYTLS_BUILD_TESTS)
    enable_testing()
    add_executable(test_anytls_unit tests/test_unit.c)
    target_link_libraries(test_anytls_unit PRIVATE anytls_core)
    add_test(NAME anytls_unit COMMAND test_anytls_unit)
    set_tests_properties(anytls_unit PROPERTIES TIMEOUT 30)
endif()