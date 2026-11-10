# Standalone build for the REALITY transport module (SOLID isolation):
#   reality_core      -- pure protocol core, ZERO SDK includes
#   reality_port_ossl -- OpenSSL + PQClean ML-KEM + socket/time ports
#   reality_tests     -- unit tests (no network) and interop test (live node)
#
# Dual mode:
#  - top-level (cmake -S net/trans/reality): standalone build/tests, see below;
#  - included from the dap-sdk tree (add_subdirectory): builds the SDK module
#    dap_trans_reality (core + ports + SDK adapter), gated by the option
#    DAP_SDK_NET_TRANS_REALITY (auto-enabled together with DAP_TLS13_OPENSSL;
#    requires BUILD_WITH_TLS=ON with DAP_TLS_BACKEND=openssl/boringssl).
#
# Standalone usage:
#   cmake -S . -B /tmp/reality-build -DOPENSSL_ROOT=<prefix>
#   cmake --build /tmp/reality-build && ctest --test-dir /tmp/reality-build

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # ================= in-tree dap-sdk module =================
    # Auto-enable together with DAP_TLS13_OPENSSL (same dependency profile:
    # BUILD_WITH_TLS=ON + openssl/boringssl backend) unless explicitly set.
    if(DAP_TLS13_OPENSSL AND NOT DEFINED DAP_SDK_NET_TRANS_REALITY)
        set(DAP_SDK_NET_TRANS_REALITY ON CACHE BOOL "REALITY (VLESS) client transport adapter" FORCE)
    else()
        option(DAP_SDK_NET_TRANS_REALITY "REALITY (VLESS) client transport adapter" OFF)
    endif()

    if(DAP_SDK_NET_TRANS_REALITY)
        if(NOT TARGET dap_crypto_tls)
            message(FATAL_ERROR "DAP_SDK_NET_TRANS_REALITY requires BUILD_WITH_TLS=ON "
                                "with DAP_TLS_BACKEND=openssl or boringssl")
        endif()

        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

        file(GLOB SRC_MLKEM_INTREE port/mlkem/*.c)
        set(REALITY_MODULE_SRCS
            core/reality_fp_chrome.c
            core/reality_fp_build.c
            core/reality_auth.c
            core/vless_codec.c
            core/vless_vision.c
            core/rate_limiter.c
            core/reality_tls13.c
            core/reality_session.c
            ${SRC_MLKEM_INTREE}
            port/reality_crypto_openssl.c
            port/reality_io_socket.c
            port/reality_time_std.c
            port/reality_bridge.c
            port/reality_forwarder.c
            adapter/dap_net_trans_reality.c
        )

        create_object_library(dap_trans_reality DAP_INTERNAL_MODULES "${REALITY_MODULE_SRCS}" HEADERS "")
        target_compile_definitions(dap_trans_reality PRIVATE DAP_SDK_NET_TRANS_REALITY=1)
        target_include_directories(dap_trans_reality PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/port
            ${CMAKE_CURRENT_SOURCE_DIR}/../tls
        )
        # pthread (bridge pump thread) comes through dap_io's "pthread" link
        # string in the chain below — do NOT reference the Threads::Threads
        # imported target here: it exists only in this directory scope and
        # breaks consumers (cellframe-sdk/node) that re-link the INTERFACE
        # dependency chain from sibling directories.
        dap_link_libraries(dap_trans_reality INTERFACE
            dap_core
            dap_crypto
            dap_crypto_tls
            dap_io
            dap_stream
            dap_client
            dap_net_trans
        )

        dap_register_module(
            MODULE_NAME "trans_reality"
            VERSION 1
            INIT_FUNCTION "dap_net_trans_reality_register"
            DEPENDENCIES "dap_net_trans"
            INCLUDE_HEADER "dap_net_trans.h"
        )
        message(STATUS "REALITY (VLESS) transport adapter: ON")
    endif()
    return()
endif()

project(reality_transport C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REALITY_BUILD_TESTS "Build reality transport tests" ON)

if(NOT DEFINED OPENSSL_ROOT)
    message(FATAL_ERROR "Pass -DOPENSSL_ROOT=<prefix with include/ and lib/>")
endif()

# --- vendored ML-KEM-768 (PQClean clean, CC0) ---
file(GLOB SRC_MLKEM port/mlkem/*.c)

add_library(reality_core STATIC
    core/reality_fp_chrome.c
    core/reality_fp_build.c
    core/reality_auth.c
    core/vless_codec.c
    core/vless_vision.c
    core/rate_limiter.c
    core/reality_tls13.c
    core/reality_session.c
)
target_include_directories(reality_core PUBLIC include core)
target_compile_options(reality_core PRIVATE -Wall -Wextra)

add_library(reality_port_ossl STATIC
    ${SRC_MLKEM}
    port/reality_crypto_openssl.c
    port/reality_io_socket.c
    port/reality_time_std.c
    port/reality_bridge.c
    port/reality_forwarder.c
)
target_include_directories(reality_port_ossl PUBLIC include port)
target_include_directories(reality_port_ossl PRIVATE ${OPENSSL_ROOT}/include core)
target_compile_options(reality_port_ossl PRIVATE -Wall)

find_library(CRYPTO_LIB crypto PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
find_library(SSL_LIB ssl PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
if(NOT CRYPTO_LIB)
    find_library(CRYPTO_LIB crypto)
endif()
target_link_libraries(reality_port_ossl PUBLIC ${CRYPTO_LIB})
find_package(Threads REQUIRED)
target_link_libraries(reality_port_ossl PUBLIC Threads::Threads)

if(REALITY_BUILD_TESTS)
    enable_testing()

    add_executable(test_reality_unit tests/test_unit.c)
    target_link_libraries(test_reality_unit PRIVATE reality_core reality_port_ossl)
    add_test(NAME unit COMMAND test_reality_unit)

    add_executable(test_reality_interop tests/test_interop.c)
    target_link_libraries(test_reality_interop PRIVATE reality_core reality_port_ossl)
    add_test(NAME interop COMMAND test_reality_interop)

    add_executable(test_reality_bridge tests/test_bridge.c)
    target_link_libraries(test_reality_bridge PRIVATE reality_core reality_port_ossl)
    add_test(NAME bridge COMMAND test_reality_bridge)

    add_executable(test_reality_forwarder tests/test_forwarder.c)
    target_link_libraries(test_reality_forwarder PRIVATE reality_core reality_port_ossl)
    add_test(NAME forwarder COMMAND test_reality_forwarder)

    set_tests_properties(interop bridge forwarder PROPERTIES
        ENVIRONMENT "REALITY_NODE=95.179.231.80;REALITY_PORT=443;REALITY_SNI=www.samsung.com;REALITY_PUBKEY=wOhxSy2vj3r1DMsUoBuNwPyElcrvj3dGbr_Mv4i4TQQ;REALITY_SHORTID=b7598d95;REALITY_UUID=95e571e3-af83-42c4-8eb6-0c06b189b0dc")
endif()
