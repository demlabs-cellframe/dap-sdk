# ShadowTLS v3 transport module (SOLID isolation, plan phase 4):
#   shadowtls core   -- OpenSSL memory-BIO session (relayed handshake + HMAC framing)
#   shadowtls adapter-- trust + carrier bridge + carrier_dap glue
#
# Dual mode: top-level standalone core build; in-tree SDK module
# dap_trans_shadowtls gated by DAP_SDK_NET_TRANS_SHADOWTLS (auto with DAP_TLS13_OPENSSL).

cmake_minimum_required(VERSION 3.16)

if(TARGET dap_core)
    if(DAP_TLS13_OPENSSL AND NOT DEFINED DAP_SDK_NET_TRANS_SHADOWTLS)
        set(DAP_SDK_NET_TRANS_SHADOWTLS ON CACHE BOOL "ShadowTLS v3 client transport adapter" FORCE)
    else()
        option(DAP_SDK_NET_TRANS_SHADOWTLS "ShadowTLS v3 client transport adapter" OFF)
    endif()

    if(DAP_SDK_NET_TRANS_SHADOWTLS)
        if(NOT TARGET dap_crypto_tls)
            message(FATAL_ERROR "DAP_SDK_NET_TRANS_SHADOWTLS requires BUILD_WITH_TLS=ON "
                                "with DAP_TLS_BACKEND=openssl or boringssl")
        endif()
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

        set(SHADOWTLS_MODULE_SRCS
            core/shadowtls_session.c
            core/shadowtls_tls13.c
            adapter/dap_net_trans_shadowtls.c
        )
        create_object_library(dap_trans_shadowtls DAP_INTERNAL_MODULES "${SHADOWTLS_MODULE_SRCS}" HEADERS "")
        target_compile_definitions(dap_trans_shadowtls PRIVATE DAP_SDK_NET_TRANS_SHADOWTLS=1)
        target_include_directories(dap_trans_shadowtls PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier_dap
            ${CMAKE_CURRENT_SOURCE_DIR}/../tls
        )
        dap_link_libraries(dap_trans_shadowtls INTERFACE
            dap_core dap_crypto dap_crypto_tls dap_io dap_stream dap_client
            dap_net_trans dap_trans_carrier dap_trans_carrier_dap
        )
        dap_register_module(
            MODULE_NAME "trans_shadowtls"
            VERSION 1
            INIT_FUNCTION "dap_net_trans_shadowtls_register"
            DEINIT_FUNCTION "dap_net_trans_shadowtls_unregister"
            DEPENDENCIES "dap_net_trans"
            INCLUDE_HEADER "dap_net_trans.h"
        )
        message(STATUS "SHADOWTLS client transport adapter: ON")
    endif()
    return()
endif()

project(shadowtls_transport C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED OPENSSL_ROOT)
    message(FATAL_ERROR "Pass -DOPENSSL_ROOT=<prefix with include/ and lib/>")
endif()
find_library(SSL_LIB ssl PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
find_library(CRYPTO_LIB crypto PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
if(NOT CRYPTO_LIB)
    find_library(CRYPTO_LIB crypto)
endif()

add_library(shadowtls_core STATIC core/shadowtls_session.c
            core/shadowtls_tls13.c)
target_include_directories(shadowtls_core PUBLIC include)
target_include_directories(shadowtls_core PRIVATE ${OPENSSL_ROOT}/include)
target_link_libraries(shadowtls_core PUBLIC ${SSL_LIB} ${CRYPTO_LIB})
if(NOT MSVC)
    target_compile_options(shadowtls_core PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
endif()
if(WIN32)
    target_link_libraries(shadowtls_core PUBLIC ws2_32)
endif()