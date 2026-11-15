# Trojan transport module (SOLID isolation, plan phase 1):
#   trojan core   -- codec + session, ZERO SDK includes (DI io/tls ports)
#   trojan port   -- dap_tls-backed TLS port (SDK-aware)
#   trojan adapter-- trust + carrier bridge + carrier_dap glue (SDK-aware)
#
# Dual mode:
#  - top-level: standalone core build + unit tests (no SDK, mock TLS port);
#  - in-tree: SDK module dap_trans_trojan, gated by DAP_SDK_NET_TRANS_TROJAN
#    (auto-enabled with DAP_TLS13_OPENSSL, same profile as REALITY).
#
# Standalone: cmake -S . -B /tmp/trojan-build && cmake --build /tmp/trojan-build
#             ctest --test-dir /tmp/trojan-build

cmake_minimum_required(VERSION 3.16)

if(TARGET dap_core)
    # ================= in-tree dap-sdk module =================
    if(DAP_TLS13_OPENSSL AND NOT DEFINED DAP_SDK_NET_TRANS_TROJAN)
        set(DAP_SDK_NET_TRANS_TROJAN ON CACHE BOOL "Trojan client transport adapter" FORCE)
    else()
        option(DAP_SDK_NET_TRANS_TROJAN "Trojan client transport adapter" OFF)
    endif()

    if(DAP_SDK_NET_TRANS_TROJAN)
        if(NOT TARGET dap_crypto_tls)
            message(FATAL_ERROR "DAP_SDK_NET_TRANS_TROJAN requires BUILD_WITH_TLS=ON "
                                "with DAP_TLS_BACKEND=openssl or boringssl")
        endif()
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

        set(TROJAN_MODULE_SRCS
            core/trojan_codec.c
            core/trojan_session.c
            port/trojan_tls_dap.c
            adapter/dap_net_trans_trojan.c
        )
        create_object_library(dap_trans_trojan DAP_INTERNAL_MODULES "${TROJAN_MODULE_SRCS}" HEADERS "")
        target_compile_definitions(dap_trans_trojan PRIVATE DAP_SDK_NET_TRANS_TROJAN=1)
        target_include_directories(dap_trans_trojan PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../carrier_dap
            ${CMAKE_CURRENT_SOURCE_DIR}/../tls
        )
        dap_link_libraries(dap_trans_trojan INTERFACE
            dap_core dap_crypto dap_crypto_tls dap_io dap_stream dap_client
            dap_net_trans dap_trans_carrier dap_trans_carrier_dap
        )
        dap_register_module(
            MODULE_NAME "trans_trojan"
            VERSION 1
            INIT_FUNCTION "dap_net_trans_trojan_register"
            DEINIT_FUNCTION "dap_net_trans_trojan_unregister"
            DEPENDENCIES "dap_net_trans"
            INCLUDE_HEADER "dap_net_trans.h"
        )
        message(STATUS "TROJAN client transport adapter: ON")
    endif()
    return()
endif()

project(trojan_transport C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(TROJAN_BUILD_TESTS "Build trojan core unit tests" ON)

# SDK-free core (codec + session). The dap_tls port + adapter are in-tree only.
add_library(trojan_core STATIC core/trojan_codec.c core/trojan_session.c)
target_include_directories(trojan_core PUBLIC include)
if(NOT MSVC)
    target_compile_options(trojan_core PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(trojan_core PUBLIC ws2_32)
endif()

if(TROJAN_BUILD_TESTS)
    enable_testing()
    add_executable(test_trojan_unit tests/test_unit.c)
    target_link_libraries(test_trojan_unit PRIVATE trojan_core)
    add_test(NAME trojan_unit COMMAND test_trojan_unit)
    set_tests_properties(trojan_unit PROPERTIES TIMEOUT 30)
endif()